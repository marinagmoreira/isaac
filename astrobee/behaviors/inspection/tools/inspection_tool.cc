/* Copyright (c) 2021, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 *
 * All rights reserved.
 *
 * The "ISAAC - Integrated System for Autonomous and Adaptive Caretaking
 * platform" software is licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

// Command line flags
#include <gflags/gflags.h>
#include <gflags/gflags_completions.h>

//  ROS includes
#include <ros/ros.h>
#include <ros/package.h>

// FSW includes
#include <config_reader/config_reader.h>
#include <ff_util/ff_names.h>
#include <isaac_util/isaac_names.h>
#include <ff_util/ff_action.h>
#include <ff_util/config_client.h>


// Action
#include <isaac_msgs/InspectionAction.h>

// TF2 support
#include <tf2_ros/transform_listener.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

// C++ STL includes
#include <boost/thread/thread.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <memory>
#include <vector>
#include <cmath>

#define DEG2RAD 3.1415/180.0

// Robot namespace
DEFINE_string(ns, "", "Robot namespace");

// Toggle based commands
DEFINE_bool(pause, false, "Send a pause command");
DEFINE_bool(resume, false, "Send an unpause command");
DEFINE_bool(repeat, false, "Repeat the last pose in the queue command");
DEFINE_bool(skip, false, "Skip the pose currently in the queue command");
DEFINE_bool(save, false, "Save the current status command");
DEFINE_bool(anomaly, false, "Send the inspection command");
DEFINE_bool(geometry, false, "Send the inspection command");
DEFINE_bool(panorama, false, "Send the inspection command");
DEFINE_bool(volumetric, false, "Send the inspection command");

// Configurable Parameters
DEFINE_string(camera, "sci_cam", "Camera to use");
DEFINE_double(tilt_max, 90.0, "Panorama: maximum tilt");
DEFINE_double(tilt_min, -90.0, "Panorama: minimum tilt");
DEFINE_double(pan_max, 180.0, "Panorama: maximum pan");
DEFINE_double(pan_min, -180.0, "Panorama: minimum pan");
DEFINE_double(overlap, 0.5, "Panorama: overlap between images");

// Plan files
DEFINE_string(anomaly_poses, "/resources/vent_jpm.txt", "Vent pose list to inspect");
DEFINE_string(geometry_poses, "/resources/survey_bay_6.txt", "Geometry poses list to map");
DEFINE_string(panorama_poses, "/resources/panorama_jpm.txt", "Panorama poses list to map");
DEFINE_string(volumetric_poses, "/resources/wifi_jpm.txt", "Wifi poses list to map");

// Timeout values for action
DEFINE_double(connect, 10.0, "Action connect timeout");
DEFINE_double(active, 10.0, "Action active timeout");
DEFINE_double(response, 200.0, "Action response timeout");
DEFINE_double(deadline, -1.0, "Action deadline timeout");

// Match the internal states and responses with the message definition
using STATE = isaac_msgs::InspectionState;
bool stopflag_ = false;

bool has_only_whitespace_or_comments(const std::string & str) {
  for (std::string::const_iterator it = str.begin(); it != str.end(); it++) {
    if (*it == '#') return true;  // No need to check further
    if (*it != ' ' && *it != '\t' && *it != '\n' && *it != '\r') return false;
  }
  return true;
}

void ReadFile(std::string file, isaac_msgs::InspectionGoal &goal) {
    geometry_msgs::Pose pose;
    goal.inspect_poses.header.frame_id = FLAGS_camera;
    // Read file geometry
    std::ifstream ifs((file).c_str());
    if (!ifs.is_open()) {
      std::cout << "Could not open file: " << file << std::endl;
      return;
    }
    std::string line;
    tf2::Quaternion quat_robot;
    while (getline(ifs, line)) {
      if (has_only_whitespace_or_comments(line)) continue;

      std::istringstream is(line);
      double origin_x, origin_y, origin_z;
      double euler_roll, euler_pitch, euler_yaw;
      double quat_x, quat_y, quat_z, quat_w;
      if ((is >> origin_x >> origin_y >> origin_z >> quat_x >> quat_y >> quat_z >> quat_w)) {
          // Position
          pose.position.x = origin_x;
          pose.position.y = origin_y;
          pose.position.z = origin_z;

          // Orientation
          pose.orientation.x = quat_x;
          pose.orientation.y = quat_y;
          pose.orientation.z = quat_z;
          pose.orientation.w = quat_w;
          goal.inspect_poses.poses.push_back(pose);

      } else {
        std::istringstream is(line);
        if ((is >> origin_x >> origin_y >> origin_z >> euler_roll >> euler_pitch >> euler_yaw)) {
          // Position
          pose.position.x = origin_x;
          pose.position.y = origin_y;
          pose.position.z = origin_z;

          quat_robot.setRPY(euler_roll * DEG2RAD,
                            euler_pitch * DEG2RAD,
                            euler_yaw * DEG2RAD);
          // Orientation
          pose.orientation.x = quat_robot.x();
          pose.orientation.y = quat_robot.y();
          pose.orientation.z = quat_robot.z();
          pose.orientation.w = quat_robot.w();
          goal.inspect_poses.poses.push_back(pose);

        } else {
          std::cout << "Ignoring invalid line: " << line  << std::endl;
          continue;
        }
      }
    }
}


// Inspection action feedback
void FeedbackCallback(isaac_msgs::InspectionFeedbackConstPtr const& feedback) {
  std::cout << "\r                                         "
            << "\rFSM: " << feedback->state.fsm_event
            << " -> " << feedback->state.fsm_state
            << " (" << feedback->state.fsm_subevent
            << " -> " << feedback->state.fsm_substate << ")" << std::flush;
}

// Inspection action result
void ResultCallback(ff_util::FreeFlyerActionState::Enum code,
  isaac_msgs::InspectionResultConstPtr const& result) {
  std::cout << std::endl << "Result: ";
  // Print general response code
  switch (code) {
  case ff_util::FreeFlyerActionState::Enum::SUCCESS:
    std::cout << "[SUCCESS] ";
    if (FLAGS_anomaly) {
      for (int i = 0; i < result->anomaly_result.size(); i++) {
          std::cout << "Vent " << i <<" is " << result->anomaly_result[i].classifier_result << std::endl;
      }
    }

    if (FLAGS_geometry) {
      for (int i = 0; i < result->inspection_result.size(); i++) {
        switch (result->inspection_result[i]) {
        case isaac_msgs::InspectionResult::PIC_ACQUIRED:
          std::cout << "Pic " << i <<" was processed " << std::endl; break;
        }
      }
    }

    break;
  case ff_util::FreeFlyerActionState::Enum::PREEMPTED:
    std::cout << "[PREEMPT] ";               break;
  case ff_util::FreeFlyerActionState::Enum::ABORTED:
    std::cout << "[ABORTED] ";   break;
  case ff_util::FreeFlyerActionState::Enum::TIMEOUT_ON_CONNECT:
    std::cout << "Action timed out on connect";        goto teardown;
  case ff_util::FreeFlyerActionState::Enum::TIMEOUT_ON_ACTIVE:
    std::cout << "Action timed out on active";         goto teardown;
  case ff_util::FreeFlyerActionState::Enum::TIMEOUT_ON_RESPONSE:
    std::cout << "Action timed out on response";       goto teardown;
  case ff_util::FreeFlyerActionState::Enum::TIMEOUT_ON_DEADLINE:
    std::cout << "Action timed out on deadline";       goto teardown;
  }
  // If we get there then we have some response data
  std::cout << result->fsm_result
            << " (Code " << result->response << ")" << std::endl;
teardown:
  std::cout << std::endl;
  // In the case we must shutdown
  if (result->fsm_result == "Inspection Over") {
    stopflag_ = true;
    ros::shutdown();
  }
}

// Send the inspection goal to the server
void SendGoal(ff_util::FreeFlyerActionClient<isaac_msgs::InspectionAction> *client) {
  // Prepare the goal
  isaac_msgs::InspectionGoal goal;
    std::string path = std::string(ros::package::getPath("inspection"));
  if (FLAGS_pause) {
    goal.command = isaac_msgs::InspectionGoal::PAUSE;
  } else if (FLAGS_resume) {
    goal.command = isaac_msgs::InspectionGoal::RESUME;
  } else if (FLAGS_repeat) {
    goal.command = isaac_msgs::InspectionGoal::REPEAT;
  } else if (FLAGS_skip) {
    goal.command = isaac_msgs::InspectionGoal::SKIP;
  } else if (FLAGS_save) {
    goal.command = isaac_msgs::InspectionGoal::SAVE;
  } else if (FLAGS_anomaly) {
    // Fill in command type
    goal.command = isaac_msgs::InspectionGoal::ANOMALY;

    // Read file
    // std::cout << "Reading: " << FLAGS_anomaly_poses << std::endl;
    ReadFile(path + FLAGS_anomaly_poses, goal);

  } else if (FLAGS_geometry) {
    // Fill in command type
    goal.command = isaac_msgs::InspectionGoal::GEOMETRY;

    // Read file
    // std::cout << "Reading: " << FLAGS_geometry_poses << std::endl;
    ReadFile(path + FLAGS_geometry_poses, goal);

  } else if (FLAGS_panorama) {
    // Fill in command type
    goal.command = isaac_msgs::InspectionGoal::PANORAMA;

    // Read file
    // std::cout << "Reading: " << FLAGS_panorama_poses << std::endl;
    ReadFile(path + FLAGS_panorama_poses, goal);

  } else if (FLAGS_volumetric) {
    // Fill in command type
    goal.command = isaac_msgs::InspectionGoal::VOLUMETRIC;

    // Read file
    // std::cout << "Reading: " << FLAGS_volumetric_poses << std::endl;
    ReadFile(path + FLAGS_volumetric_poses, goal);
  }

  client->SendGoal(goal);
}

void GetInput(ff_util::FreeFlyerActionClient<isaac_msgs::InspectionAction> *client) {
  while (!stopflag_ && ros::ok()) {
    std::string line, val;
    std::getline(std::cin, line);
    try {
      switch (std::stoi(line)) {
        case 0:
          std::cout << "Input: " << line << " - Exiting" << std::endl;
          stopflag_ = true;
          break;
        case 1:
          std::cout << "Input: " << line << " - Pausing" << std::endl;
          FLAGS_pause = true;
          SendGoal(client);
          break;
        case 2:
          std::cout << "Input: " << line << " - Resuming" << std::endl;
          FLAGS_pause = false;
          FLAGS_resume = true;
          SendGoal(client);
          break;
        case 3:
          std::cout << "Input: " << line << " - Repeating" << std::endl;
          FLAGS_pause = false;
          FLAGS_resume = false;
          FLAGS_repeat = true;
          SendGoal(client);
          break;
        case 4:
          std::cout << "Input: " << line << " - Skipping" << std::endl;
          FLAGS_pause = false;
          FLAGS_resume = false;
          FLAGS_repeat = false;
          FLAGS_skip = true;
          SendGoal(client);
          break;
        case 5:
          std::cout << "Input: " << line << " - Saving" << std::endl;
          FLAGS_pause = false;
          FLAGS_resume = false;
          FLAGS_repeat = false;
          FLAGS_skip = false;
          FLAGS_save = true;
          SendGoal(client);
          break;
        default:
          std::cout << "Invalid option" << std::endl;
      }
    } catch (const std::invalid_argument&) {
      continue;    }
  }
  return;
}

// Ensure all clients are connected
void ConnectedCallback(
  ff_util::FreeFlyerActionClient<isaac_msgs::InspectionAction> *client) {
  // Check to see if connected
  if (!client->IsConnected()) return;
  // Print out a status message
  std::cout << "\r                                                   "
            << "\rState: CONNECTED" << std::flush;
  SendGoal(client);
}

// Main entry point for application
int main(int argc, char *argv[]) {
  // Initialize a ros node
  ros::init(argc, argv, "inspection_tool", ros::init_options::AnonymousName);
  // Gather some data from the command
  google::SetUsageMessage("Usage: rosrun inspection inspection_tool <opts>");
  google::SetVersionString("0.1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);
  // Some simple checks
  uint8_t cmd = 0;
  if (FLAGS_pause) cmd++;
  if (FLAGS_resume) cmd++;
  if (FLAGS_repeat) cmd++;
  if (FLAGS_skip) cmd++;
  if (FLAGS_save) cmd++;
  if (FLAGS_anomaly)   cmd++;
  if (FLAGS_geometry) cmd++;
  if (FLAGS_panorama) cmd++;
  if (FLAGS_volumetric) cmd++;
  // Check we have specified one of the required switches
  if (cmd != 1) {
    std::cerr << "You must specify one inspection goal -vent -geometry -volumetric -pause or -resume" << std::endl;
    return 1;
  }

  // Action clients
  ff_util::FreeFlyerActionClient<isaac_msgs::InspectionAction> client;
  // Create a node handle
  ros::NodeHandle nh(std::string("/") + FLAGS_ns);
  // Setup SWITCH action
  client.SetConnectedTimeout(FLAGS_connect);
  client.SetActiveTimeout(FLAGS_active);
  client.SetResponseTimeout(FLAGS_response);
  if (FLAGS_deadline > 0)
    client.SetDeadlineTimeout(FLAGS_deadline);
  client.SetFeedbackCallback(std::bind(FeedbackCallback,
    std::placeholders::_1));
  client.SetResultCallback(std::bind(ResultCallback,
    std::placeholders::_1, std::placeholders::_2));
  client.SetConnectedCallback(std::bind(ConnectedCallback, &client));
  client.Create(&nh, ACTION_BEHAVIORS_INSPECTION);
  // Configure inspection parameters
  if (FLAGS_panorama) {
    ff_util::ConfigClient cfg(&nh, NODE_INSPECTION);
    cfg.Set<double>("pan_min", FLAGS_pan_min);
    cfg.Set<double>("pan_max", FLAGS_pan_max);
    cfg.Set<double>("tilt_min", FLAGS_tilt_min);
    cfg.Set<double>("tilt_max", FLAGS_tilt_max);
    cfg.Set<double>("overlap", FLAGS_overlap);
    if (!cfg.Reconfigure()) {
      std::cout << "Could not reconfigure the inspection node " << std::endl;
      ros::shutdown();
    }
  }

std::cout << "\r "
            << "Available actions:\n"
            << "0) Exit \n"
            << "1) Pause \n"
            << "2) Resume \n"
            << "3) Repeat \n"
            << "4) Skip \n"
            << "5) Save \n"
            << "Specify the number of the command to publish and hit 'enter'.\n"<< std::endl;

  // Start input thread
  boost::thread inp(GetInput, &client);
  // Synchronous mode
  while (!stopflag_) {
    ros::spinOnce();
  }
  // Wait for thread to exit
  inp.join();

  // Finish commandline flags
  google::ShutDownCommandLineFlags();
  // Make for great success
  return 0;
}
