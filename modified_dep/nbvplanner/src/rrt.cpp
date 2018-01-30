/*
 * Copyright 2015 Andreas Bircher, ASL, ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RRTTREE_HPP_
#define RRTTREE_HPP_

#include <cstdlib>
#include <multiagent_collision_check/multiagent_collision_checker.h>
#include <nbvplanner/rrt.h>
#include <nbvplanner/tree.hpp>
#include <rviz_visual_tools/rviz_visual_tools.h>
#include <iostream>
extern visualization_msgs::Marker origin_destList;
extern visualization_msgs::MarkerArray origin_destList_text;
static int global_id;

nbvInspection::RrtTree::RrtTree()
    : nbvInspection::TreeBase<StateVec>::TreeBase()
{
  kdTree_ = kd_create(3);
  iterationCount_ = 0;
  for (int i = 0; i < 4; i++) {
    inspectionThrottleTime_.push_back(ros::Time::now().toSec());
  }

  // If logging is required, set up files here
  bool ifLog = false;
  std::string ns = ros::this_node::getName();
  ros::param::get(ns + "/nbvp/log/on", ifLog);
  if (ifLog) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime(&rawtime);
    logFilePath_ = ros::package::getPath("nbvplanner") + "/data/"
        + std::to_string(ptm->tm_year + 1900) + "_" + std::to_string(ptm->tm_mon + 1) + "_"
        + std::to_string(ptm->tm_mday) + "_" + std::to_string(ptm->tm_hour) + "_"
        + std::to_string(ptm->tm_min) + "_" + std::to_string(ptm->tm_sec);
    system(("mkdir -p " + logFilePath_).c_str());
    logFilePath_ += "/";
    fileResponse_.open((logFilePath_ + "response.txt").c_str(), std::ios::out);
    filePath_.open((logFilePath_ + "path.txt").c_str(), std::ios::out);
  }
}

nbvInspection::RrtTree::RrtTree(mesh::StlMesh * mesh, volumetric_mapping::OctomapManager * manager)
{
  mesh_ = mesh;
  manager_ = manager;
  kdTree_ = kd_create(3);
  iterationCount_ = 0;
  for (int i = 0; i < 4; i++) {
    inspectionThrottleTime_.push_back(ros::Time::now().toSec());
  }

  // If logging is required, set up files here
  bool ifLog = false;
  std::string ns = ros::this_node::getName();
  ros::param::get(ns + "/nbvp/log/on", ifLog);
  if (ifLog) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime(&rawtime);
    logFilePath_ = ros::package::getPath("nbvplanner") + "/data/"
        + std::to_string(ptm->tm_year + 1900) + "_" + std::to_string(ptm->tm_mon + 1) + "_"
        + std::to_string(ptm->tm_mday) + "_" + std::to_string(ptm->tm_hour) + "_"
        + std::to_string(ptm->tm_min) + "_" + std::to_string(ptm->tm_sec);
    system(("mkdir -p " + logFilePath_).c_str());
    logFilePath_ += "/";
    fileResponse_.open((logFilePath_ + "response.txt").c_str(), std::ios::out);
    filePath_.open((logFilePath_ + "path.txt").c_str(), std::ios::out);
  }
}

nbvInspection::RrtTree::~RrtTree()
{
  delete rootNode_;
  kd_free(kdTree_);
  if (fileResponse_.is_open()) {
    fileResponse_.close();
  }
  if (fileTree_.is_open()) {
    fileTree_.close();
  }
  if (filePath_.is_open()) {
    filePath_.close();
  }
}

void nbvInspection::RrtTree::setStateFromPoseMsg(
    const geometry_msgs::PoseWithCovarianceStamped& pose)
{
  // Get latest transform to the planning frame and transform the pose
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }
  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose.pose, poseTF);
  tf::Vector3 position = poseTF.getOrigin();
  position = transform * position;
  tf::Quaternion quat = poseTF.getRotation();
  quat = transform * quat;
  root_[0] = position.x();
  root_[1] = position.y();
  root_[2] = position.z();
  root_[3] = tf::getYaw(quat);

  // Log the vehicle response in the planning frame
  static double logThrottleTime = ros::Time::now().toSec();
  if (ros::Time::now().toSec() - logThrottleTime > params_.log_throttle_) {
    logThrottleTime += params_.log_throttle_;
    if (params_.log_) {
      for (int i = 0; i < root_.size() - 1; i++) {
        fileResponse_ << root_[i] << ",";
      }
      fileResponse_ << root_[root_.size() - 1] << "\n";
    }
  }
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[0] > params_.inspection_throttle_) {
    inspectionThrottleTime_[0] += params_.inspection_throttle_;
    if (mesh_) {
      geometry_msgs::Pose poseTransformed;
      tf::poseTFToMsg(transform * poseTF, poseTransformed);
      mesh_->setPeerPose(poseTransformed, 0);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, 0);
      // Publish the mesh marker for visualization in rviz
      visualization_msgs::Marker inspected;
      inspected.ns = "meshInspected";
      inspected.id = 0;
      inspected.header.seq = inspected.id;
      inspected.header.stamp = pose.header.stamp;
      inspected.header.frame_id = params_.navigationFrame_;
      inspected.type = visualization_msgs::Marker::TRIANGLE_LIST;
      inspected.lifetime = ros::Duration(10);
      inspected.action = visualization_msgs::Marker::ADD;
      inspected.pose.position.x = 0.0;
      inspected.pose.position.y = 0.0;
      inspected.pose.position.z = 0.0;
      inspected.pose.orientation.x = 0.0;
      inspected.pose.orientation.y = 0.0;
      inspected.pose.orientation.z = 0.0;
      inspected.pose.orientation.w = 1.0;
      inspected.scale.x = 1.0;
      inspected.scale.y = 1.0;
      inspected.scale.z = 1.0;
      visualization_msgs::Marker uninspected = inspected;
      uninspected.header.seq++;
      uninspected.id++;
      uninspected.ns = "meshUninspected";
      mesh_->assembleMarkerArray(inspected, uninspected);
      if (inspected.points.size() > 0) {
        params_.inspectionPath_.publish(inspected);
      }
      if (uninspected.points.size() > 0) {
        params_.inspectionPath_.publish(uninspected);
      }
    }
  }
}

void nbvInspection::RrtTree::setStateFromOdometryMsg(
    const nav_msgs::Odometry& pose)
{
  // Get latest transform to the planning frame and transform the pose
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }
  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose.pose, poseTF);
  tf::Vector3 position = poseTF.getOrigin();
  position = transform * position;
  tf::Quaternion quat = poseTF.getRotation();
  quat = transform * quat;
  root_[0] = position.x();
  root_[1] = position.y();
  root_[2] = position.z();
  root_[3] = tf::getYaw(quat);
 //ROS_INFO_STREAM("super root"<< position.x()<<" " << position.y() << " " << position.z());
  // Log the vehicle response in the planning frame
  static double logThrottleTime = ros::Time::now().toSec();
  if (ros::Time::now().toSec() - logThrottleTime > params_.log_throttle_) {
    logThrottleTime += params_.log_throttle_;
    if (params_.log_) {
      for (int i = 0; i < root_.size() - 1; i++) {
        fileResponse_ << root_[i] << ",";
      }
      fileResponse_ << root_[root_.size() - 1] << "\n";
    }
  }
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[0] > params_.inspection_throttle_) {
    inspectionThrottleTime_[0] += params_.inspection_throttle_;
    if (mesh_) {
      geometry_msgs::Pose poseTransformed;
      tf::poseTFToMsg(transform * poseTF, poseTransformed);
      mesh_->setPeerPose(poseTransformed, 0);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, 0);
      // Publish the mesh marker for visualization in rviz
      visualization_msgs::Marker inspected;
      inspected.ns = "meshInspected";
      inspected.id = 0;
      inspected.header.seq = inspected.id;
      inspected.header.stamp = pose.header.stamp;
      inspected.header.frame_id = params_.navigationFrame_;
      inspected.type = visualization_msgs::Marker::TRIANGLE_LIST;
      inspected.lifetime = ros::Duration(10);
      inspected.action = visualization_msgs::Marker::ADD;
      inspected.pose.position.x = 0.0;
      inspected.pose.position.y = 0.0;
      inspected.pose.position.z = 0.0;
      inspected.pose.orientation.x = 0.0;
      inspected.pose.orientation.y = 0.0;
      inspected.pose.orientation.z = 0.0;
      inspected.pose.orientation.w = 1.0;
      inspected.scale.x = 1.0;
      inspected.scale.y = 1.0;
      inspected.scale.z = 1.0;
      visualization_msgs::Marker uninspected = inspected;
      uninspected.header.seq++;
      uninspected.id++;
      uninspected.ns = "meshUninspected";
      mesh_->assembleMarkerArray(inspected, uninspected);
      if (inspected.points.size() > 0) {
        params_.inspectionPath_.publish(inspected);
      }
      if (uninspected.points.size() > 0) {
        params_.inspectionPath_.publish(uninspected);
      }
    }
  }
}

void nbvInspection::RrtTree::setPeerStateFromPoseMsg(
    const geometry_msgs::PoseWithCovarianceStamped& pose, int n_peer)
{
  // Get latest transform to the planning frame and transform the pose
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(params_.navigationFrame_, pose.header.frame_id, pose.header.stamp,
                             transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return;
  }
  tf::Pose poseTF;
  tf::poseMsgToTF(pose.pose.pose, poseTF);
  geometry_msgs::Pose poseTransformed;
  tf::poseTFToMsg(transform * poseTF, poseTransformed);
  // Update the inspected parts of the mesh using the current position
  if (ros::Time::now().toSec() - inspectionThrottleTime_[n_peer] > params_.inspection_throttle_) {
    inspectionThrottleTime_[n_peer] += params_.inspection_throttle_;
    if (mesh_) {
      mesh_->setPeerPose(poseTransformed, n_peer);
      mesh_->incorporateViewFromPoseMsg(poseTransformed, n_peer);
    }
  }
}

void nbvInspection::RrtTree::iterate(int iterations)
{

    
    
    
    // In this function a new configuration is sampled and added to the tree.
  StateVec newState;

// Sample over a sphere with the radius of the maximum diagonal of the exploration
// space. Throw away samples outside the sampling region it exiting is not allowed
// by the corresponding parameter. This method is to not bias the tree towards the
// center of the exploration space.
  //ROS_INFO("max and minx %G %G %G %G %G %G", params_.minX_, params_.maxX_, params_.minY_ , params_.maxY_, params_.minZ_ , params_.maxZ_);
  //ROS_INFO_STREAM("max and minx STREAM"<<params_.minX_<< " "<<params_.maxX_<< " "<< params_.minY_<< " " <<params_.maxY_<< " "<< params_.minZ_ << " "<<params_.maxZ_);
  double radius = sqrt(
      SQ(params_.minX_ - params_.maxX_) + SQ(params_.minY_ - params_.maxY_)
      + SQ(params_.minZ_ - params_.maxZ_));

  bool solutionFound = false;
  //OS_INFO_STREAM("the raidus is:"<< radius); 
  
  while (!solutionFound) {
    for (int i = 0; i < 3; i++) {
      newState[i] = 2.0 * radius * (((double) rand()) / ((double) RAND_MAX) - 0.5);
      //behzad change 
      if (i == 2) {
        newState[i] = 2.0 * 2 * (((double) rand()) / ((double) RAND_MAX) - 0.5);
      }
      
      //newState[i] = (-2); //+ 2.0 * radius * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    
    }
    if (SQ(newState[0]) + SQ(newState[1]) + SQ(newState[2]) > pow(radius, 2.0))
      continue;
    // Offset new state by root
   //ROS_INFO_STREAM("root Node state"<< rootNode_->state_.x()<< " "<< rootNode_->state_.y() << " " << rootNode_->state_.z());
    newState += rootNode_->state_;
    //behzad change: this makes sure that 
    if (!params_.softBounds_) {
      if (newState.x() < params_.minX_ + 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() < params_.minY_ + 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() < params_.minZ_ + 0.5 * params_.boundingBox_.z()) {
        continue;
      } else if (newState.x() > params_.maxX_ - 0.5 * params_.boundingBox_.x()) {
        continue;
      } else if (newState.y() > params_.maxY_ - 0.5 * params_.boundingBox_.y()) {
        continue;
      } else if (newState.z() > params_.maxZ_ - 0.5 * params_.boundingBox_.z()) {
        continue;
      }
      
    }
    solutionFound = true;
  }
  //ROS_INFO_STREAM("solution_found"); 
  //ROS_ERROR("solution found in iterate%d", solutionFound);

// Find nearest neighbour
  kdres * nearest = kd_nearest3(kdTree_, newState.x(), newState.y(), newState.z());
  if (kd_res_size(nearest) <= 0) {
    kd_res_free(nearest);
    ROS_INFO("could not find the nearest neighbour"); 
    return;
  }
  nbvInspection::Node<StateVec> * newParent = (nbvInspection::Node<StateVec> *) kd_res_item_data(
      nearest);
  kd_res_free(nearest);

// Check for collision of new connection plus some overshoot distance.
  //ROS_ERROR("cehcking up collision for the new connection"); 
  Eigen::Vector3d origin(newParent->state_[0], newParent->state_[1], newParent->state_[2]);
  Eigen::Vector3d direction(newState[0] - origin[0], newState[1] - origin[1],
                            newState[2] - origin[2]);
   
  
  if (direction.norm() > params_.extensionRange_) {
    direction = params_.extensionRange_ * direction.normalized();
  }
   
  
  newState[0] = origin[0] + direction[0];
  newState[1] = origin[1] + direction[1];
  newState[2] = origin[2] + direction[2];
  //ROS_INFO_STREAM("origin (should be in the kdtree already"<< origin[0] << " "<< origin[1] << " "<< origin[2]);
  //ROS_INFO_STREAM("possibly the new node to add "<< newState[0] << " "<< newState[1] << " "<< newState[2]);
  //ros::shutdown(); 
  //behzad_change 
  //publish the origin and destination in riviz
  //origin_dest= nh.advertise<visualization_msgs::Marker>("origin_dest", 100);
  geometry_msgs::Point p1, p2;
  origin_destList.header.frame_id = "world";
  origin_destList.type = visualization_msgs::Marker::LINE_LIST;
  origin_destList.action = visualization_msgs::Marker::ADD;
  origin_destList.scale.x = 0.1;
  origin_destList.pose.orientation.w = 1;
  
  
  //ROS_INFO("origin %f %f %f",  origin[0], origin[1], origin[2]);
  //ROS_INFO("dest %f %f %f",  newState[0], newState[1], newState[2]);
 
  bool drew_solution = false; 


  if (volumetric_mapping::OctomapManager::CellStatus::kFree
      == manager_->getLineStatusBoundingBox(
          origin, direction + origin + direction.normalized() * params_.dOvershoot_,
          params_.boundingBox_)
      && !multiagent::isInCollision(newParent->state_, newState, params_.boundingBox_, segments_)) {
      
      drew_solution = true; 
      /* 
      origin_destList.color.g = 0;
      origin_destList.color.r = 0;
      origin_destList.color.b = 1;
      origin_destList.color.a = 1;
      */ 
            /*
      ROS_INFO_STREAM("-----------------");
      ROS_INFO_STREAM("inside rrt" << p1.x<< " " <<p1.y <<" "<<p1.z);
      ROS_INFO_STREAM("inside rrt" << p2.x<< " " <<p2.y <<" "<<p2.z);
     */

      // Sample the new orientation
     //ROS_ERROR("creating new nodes");
      newState[3] = 2.0 * M_PI * (((double) rand()) / ((double) RAND_MAX) - 0.5);
    // Create new node and insert into tree
    
    nbvInspection::Node<StateVec> * newNode = new nbvInspection::Node<StateVec>;
    newNode->state_ = newState;
    newNode->parent_ = newParent;
    newNode->distance_ = newParent->distance_ + direction.norm();
    newParent->children_.push_back(newNode);
    newNode->gain_ = newParent->gain_
        + gain(newNode->state_) * exp(-params_.degressiveCoeff_ * newNode->distance_);

    kd_insert3(kdTree_, newState.x(), newState.y(), newState.z(), newNode);

    // Display new node
    publishNode(newNode);

      p1.x = origin[0];
      p1.y = origin[1];
      p1.z = origin[2];
      p2.x = newState[0];
      p2.y = newState[1];
      p2.z = newState[2];
      origin_destList.points.push_back(p1);
      origin_destList.points.push_back(p2);
      std_msgs::ColorRGBA c;
      c.g = 0;
      c.r = 1;
      c.b = 0;
      c.a = 1;
      origin_destList.colors.push_back(c);
      origin_destList.colors.push_back(c);
      //origin_destList.scale.x = 
      //origin_destList.scale.y = (newNode->gain_/100.0)*2;
      params_.origin_dest_.publish(origin_destList);
      
      
      visualization_msgs::Marker origin_destList_text_marker;
      origin_destList_text_marker.header.seq++;
      origin_destList_text_marker.id = global_id++;
      origin_destList_text_marker.header.frame_id = "world";
      origin_destList_text_marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
      origin_destList_text_marker.action = visualization_msgs::Marker::ADD;
      origin_destList_text_marker.scale.x = .9;
      origin_destList_text_marker.scale.y = .9;
      origin_destList_text_marker.scale.z = .9;
      origin_destList_text_marker.pose.orientation.w = 1;
      origin_destList_text_marker.pose.position.x = p2.x;
      origin_destList_text_marker.pose.position.y = p2.y;
      origin_destList_text_marker.pose.position.z = p2.z;
      
      std::stringstream gain_text_stream; //converting to string
      gain_text_stream << std::fixed << std::setprecision(3) <<  newNode->gain_;
      //gain_text_stream << int(newNode->gain_);
      origin_destList_text_marker.text = gain_text_stream.str();
      origin_destList_text_marker.color.a = 1.0; 
      origin_destList_text_marker.color.r = 0; 
      origin_destList_text_marker.color.b = 0; 
      origin_destList_text_marker.color.g = 0; 
      
      origin_destList_text.markers.push_back(origin_destList_text_marker); 
      //ROS_INFO_STREAM("size of stuff"<< origin_destList_text.markers.size()); 
      //for(auto  origin_destList_text_el: origin_destList_text.markers) {
        //params_.origin_dest_text_.publish(origin_destList_text);
          //ROS_INFO_STREAM("her ewe go " << origin_destList_text_el.text);
          params_.origin_dest_text_.publish(origin_destList_text);
      //}
    
      // Update best IG and node if applicable
    if (newNode->gain_ > bestGain_) {
      bestGain_ = newNode->gain_;
      bestNode_ = newNode;
      ROS_INFO_STREAM("best gain so far:"<<newNode->gain_); 
    }
    counter_++;
  }

  //behzad added
  //visualizing the failed points
  if (!drew_solution){
      p1.x = origin[0];
      p1.y = origin[1];
      p1.z = origin[2];
      p2.x = newState[0];
      p2.y = newState[1];
      p2.z = newState[2];
      
      origin_destList.points.push_back(p1);
      origin_destList.points.push_back(p2);
      std_msgs::ColorRGBA c;
      c.g = 0;
      c.r = .2;
      c.b = 0;
      c.a = .3;
      origin_destList.colors.push_back(c);
      origin_destList.colors.push_back(c);
      params_.origin_dest_.publish(origin_destList);
      //origin_destList.points.clear();
  }
  

}

void nbvInspection::RrtTree::initialize()
{
    //behzad change, for deleting the previous visulization markers
    //origin_destList.points.clear();
    /* 
    origin_destList.header.frame_id = "world";
    origin_destList.type = visualization_msgs::Marker::LINE_LIST;
    origin_destList.action = visualization_msgs::Marker::DELETEALL;
    origin_destList.scale.x = 0.1;
    origin_destList.pose.orientation.w = 1;
    p_dlt_hlpr.x = 0;
    p_dlt_hlpr.y = 0;
    p_dlt_hlpr.z = 0;
    origin_destList.points.push_back(p_dlt_hlpr);
    params_.origin_dest_.publish(origin_destList);
    */ 
    //rviz_visual_tools::RvizVisualTools rviz_interface("world","/origin_dest");
    //rviz_interface.deleteAllMarkers();
    /* 
    origin_destList.header.frame_id = "world";
    origin_destList.type = visualization_msgs::Marker::LINE_LIST;
    origin_destList.action = visualization_msgs::Marker::DELETE;
    origin_destList.scale.x = 0.1;
    origin_destList.pose.orientation.w = 1;
    geometry_msgs::Point p_dlt_hlpr;
     
    for (auto pt : origin_destList.points) {
        p_dlt_hlpr = pt;    
        ROS_INFO_STREAM("pts are" << pt.x);
        ROS_INFO_STREAM("pts are" << pt.y);
        ROS_INFO_STREAM("pts are" << pt.z);
        origin_destList.points.push_back(p_dlt_hlpr); 
    }
    params_.origin_dest_.publish(origin_destList);
    */
    //ROS_INFO_STREAM("origin_destList size before"<<origin_destList.points.size()); 
    //origin_destList.points.clear();
    //ROS_INFO_STREAM("origin_destList size after"<<origin_destList.points.size()); 
    //params_.origin_dest_.publish(origin_destList);
    //origin_destList.action = visualization_msgs::Marker::ADD;
    
    // This function is to initialize the tree, including insertion of remainder of previous best branch.
  g_ID_ = 0;
// Remove last segment from segment list (multi agent only)
  int i;
  for (i = 0; i < agentNames_.size(); i++) {
    if (agentNames_[i].compare(params_.navigationFrame_) == 0) {
       ROS_ERROR("breaking out");
        break;
    }
  }
  if (i < agentNames_.size()) {
    segments_[i]->clear();
  }
// Initialize kd-tree with root node and prepare log file
  kdTree_ = kd_create(3);

  if (params_.log_) {
    if (fileTree_.is_open()) {
      fileTree_.close();
    }
    fileTree_.open((logFilePath_ + "tree" + std::to_string(iterationCount_) + ".txt").c_str(),
                   std::ios::out);
  }

  rootNode_ = new Node<StateVec>;
  rootNode_->distance_ = 0.0;
  rootNode_->gain_ = params_.zero_gain_;
  rootNode_->parent_ = NULL;

  if (params_.exact_root_) {
    if (iterationCount_ <= 1) {
      exact_root_ = root_;
    }
    rootNode_->state_ = exact_root_;
  } else {
    rootNode_->state_ = root_;
  }
  kd_insert3(kdTree_, rootNode_->state_.x(), rootNode_->state_.y(), rootNode_->state_.z(),
             rootNode_);
  iterationCount_++;
  //ROS_ERROR("inserted a rootNode");

  //behzad change (for visualization purposes)
  geometry_msgs::Point p1, p2;
  origin_destList.header.frame_id = "world";
  origin_destList.type = visualization_msgs::Marker::LINE_LIST;
  origin_destList.action = visualization_msgs::Marker::ADD;
  origin_destList.scale.x = 0.1;
  origin_destList.pose.orientation.w = 1;


// Insert all nodes of the remainder of the previous best branch, checking for collisions and
// recomputing the gain.
  for (typename std::vector<StateVec>::reverse_iterator iter = bestBranchMemory_.rbegin();
      iter != bestBranchMemory_.rend(); ++iter) {
    StateVec newState = *iter;
    kdres * nearest = kd_nearest3(kdTree_, newState.x(), newState.y(), newState.z());
     
    if (kd_res_size(nearest) <= 0) {
      kd_res_free(nearest);
      continue;
    }
    nbvInspection::Node<StateVec> * newParent = (nbvInspection::Node<StateVec> *) kd_res_item_data(
        nearest);
    kd_res_free(nearest);

    // Check for collision
    Eigen::Vector3d origin(newParent->state_[0], newParent->state_[1], newParent->state_[2]);
    Eigen::Vector3d direction(newState[0] - origin[0], newState[1] - origin[1],
                              newState[2] - origin[2]);
    if (direction.norm() > params_.extensionRange_) {
      direction = params_.extensionRange_ * direction.normalized();
    }
    newState[0] = origin[0] + direction[0];
    newState[1] = origin[1] + direction[1];
    
    //newState[2] = origin[2] + direction[2];
    newState[2] = origin[2]; 
        
    bool drew_solution = false; 
    
    //ROS_ERROR("before checking the cells"); 
    if (volumetric_mapping::OctomapManager::CellStatus::kFree
        == manager_->getLineStatusBoundingBox(
            origin, direction + origin + direction.normalized() * params_.dOvershoot_,
            params_.boundingBox_)
        && !multiagent::isInCollision(newParent->state_, newState, params_.boundingBox_,
                                      segments_)) {
        //behzad change to draw 
        drew_solution = true; 
        /* 
        origin_destList.color.g = 0;
        origin_destList.color.r = 0;
        origin_destList.color.b = 1;
        origin_destList.color.a = 1;
        */ 
        p1.x = origin[0];
        p1.y = origin[1];
        p1.z = origin[2];
        p2.x = newState[0];
        p2.y = newState[1];
        p2.z = newState[2];
        origin_destList.points.push_back(p1);
        origin_destList.points.push_back(p2);
        std_msgs::ColorRGBA c;
        c.g = 0;
        c.r = 0;
        c.b = 1;
        c.a = 1;
        origin_destList.colors.push_back(c);
        origin_destList.colors.push_back(c);
        params_.origin_dest_.publish(origin_destList);
        
        
        
        //origin_destList.points.clear();
    
      // Create new node and insert into tree
      //ROS_ERROR("creating new nodes");
      nbvInspection::Node<StateVec> * newNode = new nbvInspection::Node<StateVec>;
      newNode->state_ = newState;
      newNode->parent_ = newParent;
      newNode->distance_ = newParent->distance_ + direction.norm();
      newParent->children_.push_back(newNode);
      newNode->gain_ = newParent->gain_
          + gain(newNode->state_) * exp(-params_.degressiveCoeff_ * newNode->distance_);

      kd_insert3(kdTree_, newState.x(), newState.y(), newState.z(), newNode);

      // Display new node
      publishNode(newNode);

        visualization_msgs::Marker origin_destList_text_marker;
        origin_destList_text_marker.header.seq++;
        origin_destList_text_marker.id = global_id++;
        origin_destList_text_marker.header.frame_id = "world";
        origin_destList_text_marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        origin_destList_text_marker.action = visualization_msgs::Marker::ADD;
        origin_destList_text_marker.scale.x = .9;
        origin_destList_text_marker.scale.y = .9;
        origin_destList_text_marker.scale.z = .9;
        origin_destList_text_marker.pose.orientation.w = 1;
        origin_destList_text_marker.pose.position.x = p2.x;
        origin_destList_text_marker.pose.position.y = p2.y;
        origin_destList_text_marker.pose.position.z = p2.z;
        std::stringstream gain_text_stream; //converting to string
        gain_text_stream << std::fixed << std::setprecision(3) <<  newNode->gain_;
        //gain_text_stream << int(newNode->gain_);
        origin_destList_text_marker.text = gain_text_stream.str();
        origin_destList_text_marker.color.a = 1.0; 
        origin_destList_text_marker.color.r = 0; 
        origin_destList_text_marker.color.b = 0; 
        origin_destList_text_marker.color.g = 0; 
        origin_destList_text.markers.push_back(origin_destList_text_marker); 
        

      // Update best IG and node if applicable
      if (newNode->gain_ > bestGain_) {
        bestGain_ = newNode->gain_;
        bestNode_ = newNode;
      }
      counter_++;
    }

    if (!drew_solution) {
        /* 
        origin_destList.color.g = 0;
        origin_destList.color.r = 0;
        origin_destList.color.b = 1;
        origin_destList.color.a = .4;
        */ 
        p1.x = origin[0];
        p1.y = origin[1];
        p1.z = origin[2];
        p2.x = newState[0];
        p2.y = newState[1];
        p2.z = newState[2];
        origin_destList.points.push_back(p1);
        origin_destList.points.push_back(p2);
        std_msgs::ColorRGBA c;
        c.g = 0;
        c.r = 0;
        c.b = 1;
        c.a = .4;
        origin_destList.colors.push_back(c);
        origin_destList.colors.push_back(c);

        
        
        params_.origin_dest_.publish(origin_destList);
        //origin_destList.points.clear();
    }
  }
  //ROS_INFO("after the for loop in initializatoin");

// Publish visualization of total exploration area
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = 0;
  p.header.frame_id = params_.navigationFrame_;
  p.id = 0;
  p.ns = "workspace";
  p.type = visualization_msgs::Marker::CUBE;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = 0.5 * (params_.minX_ + params_.maxX_);
  p.pose.position.y = 0.5 * (params_.minY_ + params_.maxY_);
  p.pose.position.z = 0.5 * (params_.minZ_ + params_.maxZ_);
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, 0.0);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = params_.maxX_ - params_.minX_;
  p.scale.y = params_.maxY_ - params_.minY_;
  p.scale.z = params_.maxZ_ - params_.minZ_;
  p.color.r = 200.0 / 255.0;
  p.color.g = 100.0 / 255.0;
  p.color.b = 0.0;
  p.color.a = 0.1;
  p.lifetime = ros::Duration(0.0);
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);
  //ROS_INFO("done with initialization");
}

std::vector<geometry_msgs::Pose> nbvInspection::RrtTree::getBestEdge(std::string targetFrame)
{
// This function returns the first edge of the best branch
  std::vector<geometry_msgs::Pose> ret;
  nbvInspection::Node<StateVec> * current = bestNode_;
  //behzad change for visualization purposes 
  visualization_msgs::Marker origin_destList_text_marker;
  origin_destList_text_marker.header.seq++;
  origin_destList_text_marker.id = global_id++;
  origin_destList_text_marker.header.frame_id = "world";
  origin_destList_text_marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
  origin_destList_text_marker.action = visualization_msgs::Marker::ADD;
  origin_destList_text_marker.scale.x = 2;
  origin_destList_text_marker.scale.y = 2;
  origin_destList_text_marker.scale.z = .9;
  origin_destList_text_marker.pose.orientation.w = 1;
  origin_destList_text_marker.pose.position.x = current->state_[0];
  origin_destList_text_marker.pose.position.y = current->state_[1];
  origin_destList_text_marker.pose.position.z = current->state_[2];
  
  std::stringstream gain_text_stream; //converting to string
  gain_text_stream << std::fixed << std::setprecision(3) <<  bestNode_->gain_;
  //gain_text_stream << int(bestNode_->gain_);
  origin_destList_text_marker.text = gain_text_stream.str();
  origin_destList_text_marker.color.a = 1.0; 
  origin_destList_text_marker.color.r = 1; 
  origin_destList_text_marker.color.b = 1; 
  origin_destList_text_marker.color.g = 1; 
  origin_destList_text.markers.push_back(origin_destList_text_marker); 
  params_.origin_dest_text_.publish(origin_destList_text);

  
  if (current->parent_ != NULL) {
    while (current->parent_ != rootNode_ && current->parent_ != NULL) {
      current = current->parent_;
    }
    ret = samplePath(current->parent_->state_, current->state_, targetFrame);
    history_.push(current->parent_->state_);
    exact_root_ = current->state_;
  }
  
  

  //behzad change, to visualize the best edge
  geometry_msgs::Point p1, p2;
  
  origin_destList.header.frame_id = "world";
  origin_destList.type = visualization_msgs::Marker::LINE_LIST;
  origin_destList.action = visualization_msgs::Marker::ADD;
  origin_destList.scale.x = 0.1;
  origin_destList.pose.orientation.w = 1;
  /* 
  origin_destList.color.g = 1;
  origin_destList.color.r = 0;
  origin_destList.color.b = 0;
  origin_destList.color.a = 1;
  */ 
  p1.x = current->state_[0];
  p1.y = current->state_[1];
  p1.z = current->state_[2];
  p2.x = current->parent_->state_[0];
  p2.y = current->parent_->state_[1];
  p2.z = current->parent_->state_[2];
  origin_destList.points.push_back(p1);
  origin_destList.points.push_back(p2);
  std_msgs::ColorRGBA c;
  c.g = 1;
  c.r = 0;
  c.b = 0;
  c.a = 1;
  origin_destList.colors.push_back(c);
  origin_destList.colors.push_back(c);
  params_.origin_dest_.publish(origin_destList);
  //origin_destList.points.clear();
  return ret;
}

float nbvInspection::RrtTree::coverage()
{
  int covered = 0;
  int uncovered = 0; 
  const double disc = manager_->getResolution();
  Eigen::Vector3d vec;
  for (vec[0] = params_.minX_;
      vec[0] < params_.maxX_; vec[0] +=disc) {
      for (vec[1] = params_.minY_;
        vec[1] < params_.maxY_; vec[1] += disc) {
        for (vec[2] = params_.minZ_;
          vec[2] < params_.maxZ_; vec[2] += disc) {
        
        double probability;
        volumetric_mapping::OctomapManager::CellStatus node = manager_->getCellProbabilityPoint(
           vec, &probability);
        if (node == volumetric_mapping::OctomapManager::CellStatus::kUnknown ) {
            uncovered +=1;
            // Rayshooting to evaluate inspectability of cell
         } else {
             covered +=1;
         }
        
        }
    }
  }
  return ((float)covered/(float)(uncovered + covered))*100;
}


double nbvInspection::RrtTree::gain(StateVec state)
{
// This function computes the gain
  double gain = 0.0;
  const double disc = manager_->getResolution();
  Eigen::Vector3d origin(state[0], state[1], state[2]);
  Eigen::Vector3d vec;
  double rangeSq = pow(params_.gainRange_, 2.0);
// Iterate over all nodes within the allowed distance
  for (vec[0] = std::max(state[0] - params_.gainRange_, params_.minX_);
      vec[0] < std::min(state[0] + params_.gainRange_, params_.maxX_); vec[0] += disc) {
    for (vec[1] = std::max(state[1] - params_.gainRange_, params_.minY_);
        vec[1] < std::min(state[1] + params_.gainRange_, params_.maxY_); vec[1] += disc) {
      for (vec[2] = std::max(state[2] - params_.gainRange_, params_.minZ_);
          vec[2] < std::min(state[2] + params_.gainRange_, params_.maxZ_); vec[2] += disc) {
        Eigen::Vector3d dir = vec - origin;
        // Skip if distance is too large
        if (dir.transpose().dot(dir) > rangeSq) {
          continue;
        }
        bool insideAFieldOfView = false;
        // Check that voxel center is inside one of the fields of view.
        for (typename std::vector<std::vector<Eigen::Vector3d>>::iterator itCBN = params_
            .camBoundNormals_.begin(); itCBN != params_.camBoundNormals_.end(); itCBN++) {
          bool inThisFieldOfView = true;
          for (typename std::vector<Eigen::Vector3d>::iterator itSingleCBN = itCBN->begin();
              itSingleCBN != itCBN->end(); itSingleCBN++) {
            Eigen::Vector3d normal = Eigen::AngleAxisd(state[3], Eigen::Vector3d::UnitZ())
                * (*itSingleCBN);
            double val = dir.dot(normal.normalized());
            if (val < SQRT2 * disc) {
              inThisFieldOfView = false;
              break;
            }
          }
          if (inThisFieldOfView) {
            insideAFieldOfView = true;
            break;
          }
        }
        if (!insideAFieldOfView) {
          continue;
        }
        // Check cell status and add to the gain considering the corresponding factor.
        double probability;
        volumetric_mapping::OctomapManager::CellStatus node = manager_->getCellProbabilityPoint(
            vec, &probability);
        if (node == volumetric_mapping::OctomapManager::CellStatus::kUnknown) {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igUnmapped_;
            // TODO: Add probabilistic gain
            // gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
        } else if (node == volumetric_mapping::OctomapManager::CellStatus::kOccupied) {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igOccupied_;
            // TODO: Add probabilistic gain
            // gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
        } else {
          // Rayshooting to evaluate inspectability of cell
          if (volumetric_mapping::OctomapManager::CellStatus::kOccupied
              != this->manager_->getVisibility(origin, vec, false)) {
            gain += params_.igFree_;
            // TODO: Add probabilistic gain
            // gain += params_.igProbabilistic_ * PROBABILISTIC_MODEL(probability);
          }
        }
      }
    }
  }
// Scale with volume
  gain *= pow(disc, 3.0);
// Check the gain added by inspectable surface
  if (mesh_) {
    tf::Transform transform;
    transform.setOrigin(tf::Vector3(state.x(), state.y(), state.z()));
    tf::Quaternion quaternion;
    quaternion.setEuler(0.0, 0.0, state[3]);
    transform.setRotation(quaternion);
    gain += params_.igArea_ * mesh_->computeInspectableArea(transform);
  }
  return gain;
}

std::vector<geometry_msgs::Pose> nbvInspection::RrtTree::getPathBackToPrevious(
    std::string targetFrame)
{
  std::vector<geometry_msgs::Pose> ret;
  if (history_.empty()) {
    return ret;
  }
  ret = samplePath(root_, history_.top(), targetFrame);
  history_.pop();
  return ret;
}

void nbvInspection::RrtTree::memorizeBestBranch()
{
  bestBranchMemory_.clear();
  Node<StateVec> * current = bestNode_;
  while (current->parent_ && current->parent_->parent_) {
    bestBranchMemory_.push_back(current->state_);
    current = current->parent_;
  }
}

void nbvInspection::RrtTree::clear()
{
  delete rootNode_;
  rootNode_ = NULL;

  counter_ = 0;
  bestGain_ = params_.zero_gain_;
  bestNode_ = NULL;

  kd_free(kdTree_);
}

void nbvInspection::RrtTree::publishNode(Node<StateVec> * node)
{
  visualization_msgs::Marker p;
  p.header.stamp = ros::Time::now();
  p.header.seq = g_ID_;
  p.header.frame_id = params_.navigationFrame_;
  p.id = g_ID_;
  g_ID_++;
  p.ns = "vp_tree";
  p.type = visualization_msgs::Marker::ARROW;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = node->state_[0];
  p.pose.position.y = node->state_[1];
  p.pose.position.z = node->state_[2];
  tf::Quaternion quat;
  quat.setEuler(0.0, 0.0, node->state_[3]);
  p.pose.orientation.x = quat.x();
  p.pose.orientation.y = quat.y();
  p.pose.orientation.z = quat.z();
  p.pose.orientation.w = quat.w();
  p.scale.x = std::max(node->gain_ / 20.0, 0.05);
  p.scale.y = 0.1;
  p.scale.z = 0.1;
  p.color.r = 167.0 / 255.0;
  p.color.g = 167.0 / 255.0;
  p.color.b = 0.0;
  p.color.a = 1.0;
  p.lifetime = ros::Duration(10.0);
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

  if (!node->parent_)
    return;

  p.id = g_ID_;
  g_ID_++;
  p.ns = "vp_branches";
  p.type = visualization_msgs::Marker::ARROW;
  p.action = visualization_msgs::Marker::ADD;
  p.pose.position.x = node->parent_->state_[0];
  p.pose.position.y = node->parent_->state_[1];
  p.pose.position.z = node->parent_->state_[2];
  Eigen::Quaternion<float> q;
  Eigen::Vector3f init(1.0, 0.0, 0.0);
  Eigen::Vector3f dir(node->state_[0] - node->parent_->state_[0],
                      node->state_[1] - node->parent_->state_[1],
                      node->state_[2] - node->parent_->state_[2]);
  q.setFromTwoVectors(init, dir);
  q.normalize();
  p.pose.orientation.x = q.x();
  p.pose.orientation.y = q.y();
  p.pose.orientation.z = q.z();
  p.pose.orientation.w = q.w();
  p.scale.x = dir.norm();
  p.scale.y = 0.03;
  p.scale.z = 0.03;
  p.color.r = 100.0 / 255.0;
  p.color.g = 100.0 / 255.0;
  p.color.b = 0.7;
  p.color.a = 1.0;
  p.lifetime = ros::Duration(10.0);
  p.frame_locked = false;
  params_.inspectionPath_.publish(p);

  if (params_.log_) {
    for (int i = 0; i < node->state_.size(); i++) {
      fileTree_ << node->state_[i] << ",";
    }
    fileTree_ << node->gain_ << ",";
    for (int i = 0; i < node->parent_->state_.size(); i++) {
      fileTree_ << node->parent_->state_[i] << ",";
    }
    fileTree_ << node->parent_->gain_ << "\n";
  }
}

std::vector<geometry_msgs::Pose> nbvInspection::RrtTree::samplePath(StateVec start, StateVec end,
                                                                    std::string targetFrame)
{
  std::vector<geometry_msgs::Pose> ret;
  static tf::TransformListener listener;
  tf::StampedTransform transform;
  try {
    listener.lookupTransform(targetFrame, params_.navigationFrame_, ros::Time(0), transform);
  } catch (tf::TransformException ex) {
    ROS_ERROR("%s", ex.what());
    return ret;
  }
  Eigen::Vector3d distance(end[0] - start[0], end[1] - start[1], end[2] - start[2]);
  double yaw_direction = end[3] - start[3];
  if (yaw_direction > M_PI) {
    yaw_direction -= 2.0 * M_PI;
  }
  if (yaw_direction < -M_PI) {
    yaw_direction += 2.0 * M_PI;
  }
  double disc = std::min(params_.dt_ * params_.v_max_ / distance.norm(),
                         params_.dt_ * params_.dyaw_max_ / abs(yaw_direction));
  assert(disc > 0.0);
  //disc = .05 ;
  for (double it = 0.0; it <= 1.0; it += disc) {
    tf::Vector3 origin((1.0 - it) * start[0] + it * end[0], (1.0 - it) * start[1] + it * end[1],
                       (1.0 - it) * start[2] + it * end[2]);
    double yaw = start[3] + yaw_direction * it;
    if (yaw > M_PI)
      yaw -= 2.0 * M_PI;
    if (yaw < -M_PI)
      yaw += 2.0 * M_PI;
    tf::Quaternion quat;
    quat.setEuler(0.0, 0.0, yaw);
    origin = transform * origin;
    quat = transform * quat;
    tf::Pose poseTF(quat, origin);
    geometry_msgs::Pose pose;
    tf::poseTFToMsg(poseTF, pose);
    ret.push_back(pose);
    if (params_.log_) {
      filePath_ << poseTF.getOrigin().x() << ",";
      filePath_ << poseTF.getOrigin().y() << ",";
      filePath_ << poseTF.getOrigin().z() << ",";
      filePath_ << tf::getYaw(poseTF.getRotation()) << "\n";
    }
  }
  return ret;
}

#endif
