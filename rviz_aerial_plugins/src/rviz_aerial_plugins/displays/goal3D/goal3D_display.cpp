// Copyright 2019 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rviz_aerial_plugins/displays/goal3D/goal3D_display.hpp"
#include "rviz_common/load_resource.hpp"

namespace rviz_aerial_plugins
{

namespace displays
{

Goal3DDisplay::Goal3DDisplay(QWidget* parent):
  rviz_common::Panel(parent), rviz_ros_node_()

{
  vehicle_gps_position_name_ = "/iris_0/vehicle_gps_position";
  vehicle_command_name_ = "/iris_0/vehicle_command";
  vehicle_status_name_ = "/iris_0/vehicle_status";
  attitude_topic_name_ = "/iris_0/vehicle_attitude";
  odometry_topic_name_ = "/iris_0/vehicle_odometry";
  position_setpoint_topic_name_ = "/iris_0/position_setpoint";
  vehicle_land_detected_topic_name_ = "/iris_0/vehicle_land_detected";
  pose_stamped_name_ = "/iris_0/command_pose";

  arming_state_ = 0;
  latitude_ = 0;
  longitude_ = 0;
  altitude_ = 0;
  heading_ = 0;
  flying_ = false;
}

void Goal3DDisplay::add_namespaces_to_combobox()
{
  auto names_and_namespaces = rviz_ros_node_.lock()->get_raw_node()->get_node_names();

  std::set<std::string> namespaces = get_namespaces(names_and_namespaces);

  namespace_->blockSignals(true);
  namespace_->clear();
  for(auto n: namespaces){
    namespace_->addItem(QString(n.c_str()));
  }
  namespace_->blockSignals(false);
}

int Goal3DDisplay::getTargetSystem()
{
  int result = -1;
  std::string current_namespace(namespace_->currentText().toUtf8().constData());

  std::vector<std::string> namespace_tokens = split (current_namespace, '_');
  if(namespace_tokens.size() > 1){
    result = atoi(namespace_tokens[1].c_str());
  }
  return result + 1;
}

void Goal3DDisplay::onInitialize()
{
  rviz_ros_node_ = getDisplayContext()->getRosNodeAbstraction();
  server_ = std::make_unique<interactive_markers::InteractiveMarkerServer>("drone_goal", rviz_ros_node_.lock()->get_raw_node());

  namespace_ = new QComboBox();
  add_namespaces_to_combobox();
  QPushButton* refresh_button = new QPushButton("Refresh");
  refresh_button->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/Refresh.png"));

  QGridLayout *grid = new QGridLayout;

  label_arming_state_ = new QLabel();
  label_name_arming_state_ = new QLabel("Arming state:");
  label_vehicle_type_ = new QLabel();
  label_name_vehicle_type_ = new QLabel("Vehicle type:");

  button_arm_ = new QPushButton("Arm");
  button_arm_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/PowerOn.png"));
  button_takeoff_ = new QPushButton("Takeoff");
  button_takeoff_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/Takeoff.png"));
  button_takeoff_->setEnabled(false);
  button_position_setpoint_ = new QPushButton("Go to");
  button_position_setpoint_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/Goal3D.png"));

  grid->addWidget(namespace_, 0, 0);
  grid->addWidget(refresh_button, 0, 1);
  grid->addWidget(label_name_arming_state_, 1, 0);
  grid->addWidget(label_arming_state_, 1, 1);
  grid->addWidget(label_name_vehicle_type_, 2, 0);
  grid->addWidget(label_vehicle_type_, 2, 1);
  grid->addWidget(button_arm_, 3, 0, 1, 2);
  grid->addWidget(button_takeoff_, 4, 0, 1, 2);
  grid->addWidget(button_position_setpoint_, 5, 0, 1, 2);

  setLayout(grid);
  QObject::connect(namespace_, SIGNAL(currentIndexChanged(QString)),this, SLOT(on_changed_namespace(QString)));
  QObject::connect(refresh_button, SIGNAL(clicked()),this, SLOT(on_click_refresheButton()));
  QObject::connect(button_arm_, SIGNAL(clicked()),this, SLOT(on_click_armButton()));
  QObject::connect(button_takeoff_, SIGNAL(clicked()),this, SLOT(on_click_takeoffButton()));
  QObject::connect(button_position_setpoint_, SIGNAL(clicked()),this, SLOT(on_click_position_setpointButton()));
  QObject::connect(this, SIGNAL(valueChangedInterface_signal()),this, SLOT(valueChangedInterface()));

  tf_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(rviz_ros_node_.lock()->get_raw_node());

  auto clock_ros = rviz_ros_node_.lock()->get_raw_node()->get_clock();
  buffer_ = std::make_shared<tf2_ros::Buffer>(clock_ros);
  buffer_->setUsingDedicatedThread(true);
  auto timer_interface = std::make_shared<tf2_ros::CreateTimerROS>(
    rviz_ros_node_.lock()->get_raw_node()->get_node_base_interface(),
    rviz_ros_node_.lock()->get_raw_node()->get_node_timers_interface());
  buffer_->setCreateTimerInterface(timer_interface);

  tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*buffer_);

  auto timer_callback =
     [this]() -> void {
       visualization_msgs::msg::InteractiveMarker int_marker;
       server_->get("quadrocopter", int_marker);

       auto odom_tf_msg = std::make_shared<geometry_msgs::msg::TransformStamped>();
       odom_tf_msg->header.frame_id = "map";
       odom_tf_msg->child_frame_id = "quadcopter_goal";
       // Stuff and publish /tf
       odom_tf_msg->header.stamp = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();
       odom_tf_msg->transform.translation.x = int_marker.pose.position.x;
       odom_tf_msg->transform.translation.y = int_marker.pose.position.y;
       odom_tf_msg->transform.translation.z = int_marker.pose.position.z;
       odom_tf_msg->transform.rotation.x = int_marker.pose.orientation.x;
       odom_tf_msg->transform.rotation.y = int_marker.pose.orientation.y;
       odom_tf_msg->transform.rotation.z = int_marker.pose.orientation.z;
       odom_tf_msg->transform.rotation.w = int_marker.pose.orientation.w;

       tf_broadcaster_->sendTransform(*odom_tf_msg);

     };
  timer_ = rviz_ros_node_.lock()->get_raw_node()->create_wall_timer(10ms, timer_callback);

  subcribe2topics();

  makeQuadrocopterMarker(tf2::Vector3(0, 0, 3));
  server_->applyChanges();
}

void Goal3DDisplay::on_click_position_setpointButton()
{
  visualization_msgs::msg::InteractiveMarker int_marker;
  server_->get("quadrocopter", int_marker);

  std::string str_test =   std::string(namespace_->currentText().toUtf8().constData()) + "/odom";
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "map %s", str_test.c_str());

  // if(buffer_->canTransform("map",
  //                         str_test,
  //                          tf2::TimePoint(), tf2::durationFromSec(1.0)))
  // buffer_->waitForTransform(
  //       "map",
  //       str_test,
  //          tf2::TimePoint(), tf2::durationFromSec(1.0),
  //         [this, int_marker](const tf2_ros::TransformStampedFuture & future)
  //     {
  //       RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "waitForTransform");
  //       geometry_msgs::msg::TransformStamped transform_callback_result;
  //       transform_callback_result = future.get();
  //       RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "%.5f\t%.5f\t%.5f",
  //                   transform_callback_result.transform.translation.x,
  //                   transform_callback_result.transform.translation.y,
  //                   transform_callback_result.transform.translation.z);

    geometry_msgs::msg::TransformStamped transform_callback_result = buffer_->lookupTransform("map", str_test, tf2::TimePoint());

        geometry_msgs::msg::PoseStamped msg;
        msg.header.stamp = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();
        // msg.pose.position.x = transform_callback_result.transform.translation.x;
        // msg.pose.position.y = transform_callback_result.transform.translation.y;
        // msg.pose.position.z = int_marker.pose.position.z;
        //
        // msg.pose.orientation.x = transform_callback_result.transform.rotation.x;
        // msg.pose.orientation.y = transform_callback_result.transform.rotation.y;
        // msg.pose.orientation.z = transform_callback_result.transform.rotation.z;
        // msg.pose.orientation.w = transform_callback_result.transform.rotation.w;

        msg.pose.position.x = int_marker.pose.position.x - transform_callback_result.transform.translation.x;
        msg.pose.position.y = int_marker.pose.position.y - transform_callback_result.transform.translation.y;
        msg.pose.position.z = int_marker.pose.position.z;

        msg.pose.orientation.x = int_marker.pose.orientation.x;
        msg.pose.orientation.y = int_marker.pose.orientation.y;
        msg.pose.orientation.z = int_marker.pose.orientation.z;
        msg.pose.orientation.w = int_marker.pose.orientation.w;

        publisher_pose_stamped_->publish(msg);
  //     });
  // else{
  //   RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "canTransform fails");
  //
  // }

  // msg.pose.position.x = int_marker.pose.position.x;
  // msg.pose.position.y = int_marker.pose.position.y;
  // msg.pose.position.z = int_marker.pose.position.z;
  //
  // msg.pose.orientation.x = int_marker.pose.orientation.x;
  // msg.pose.orientation.y = int_marker.pose.orientation.y;
  // msg.pose.orientation.z = int_marker.pose.orientation.z;
  // msg.pose.orientation.w = int_marker.pose.orientation.w;

}

void Goal3DDisplay::on_click_takeoffButton()
{
  if(latitude_ != 0 && longitude_ != 0){

    RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "flying_ %d", flying_);

    if(!flying_){
      auto time_node = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();
      px4_msgs::msg::VehicleCommand msg_vehicle_command;
      msg_vehicle_command.timestamp = time_node.nanoseconds()/1000;
      msg_vehicle_command.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_TAKEOFF;
      msg_vehicle_command.param1 = 0.1;
      msg_vehicle_command.param2 = 0;
      msg_vehicle_command.param3 = 0;
      msg_vehicle_command.param4 = heading_;
      msg_vehicle_command.param5 = latitude_;
      msg_vehicle_command.param6 = longitude_;
      msg_vehicle_command.param7 = 3.0;
      msg_vehicle_command.confirmation = 1;
      msg_vehicle_command.source_system = 255;
      msg_vehicle_command.target_system = get_target_system(std::string(namespace_->currentText().toUtf8().constData()));
      msg_vehicle_command.target_component = 1;
      msg_vehicle_command.from_external = true;
      publisher_vehicle_command_->publish(msg_vehicle_command);
      RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "VEHICLE_CMD_NAV_TAKEOFF %.5f %.5f", latitude_, longitude_);
    }else{
      auto time_node = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();
      px4_msgs::msg::VehicleCommand msg_vehicle_command;
      msg_vehicle_command.timestamp = time_node.nanoseconds()/1000;
      msg_vehicle_command.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_NAV_LAND;
      msg_vehicle_command.param1 = 0.1;
      msg_vehicle_command.param2 = 0;
      msg_vehicle_command.param3 = 0;
      msg_vehicle_command.param4 = heading_;
      msg_vehicle_command.param5 = latitude_;
      msg_vehicle_command.param6 = longitude_;
      msg_vehicle_command.param7 = 3.0;
      msg_vehicle_command.confirmation = 1;
      msg_vehicle_command.source_system = 255;
      msg_vehicle_command.target_system = get_target_system(std::string(namespace_->currentText().toUtf8().constData()));
      msg_vehicle_command.target_component = 1;
      msg_vehicle_command.from_external = true;
      publisher_vehicle_command_->publish(msg_vehicle_command);
      RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "VEHICLE_CMD_NAV_TAKEOFF %5.f %5.f", latitude_, longitude_);
    }
  }
}

void Goal3DDisplay::on_click_armButton()
{
  auto time_node = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();

  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Arming state %d", arming_state_);

  if(arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_STANDBY){
    px4_msgs::msg::VehicleCommand msg_vehicle_command;
    msg_vehicle_command.timestamp = time_node.nanoseconds()/1000;
    msg_vehicle_command.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;
    msg_vehicle_command.param1 = 1;
    msg_vehicle_command.confirmation = 1;
    msg_vehicle_command.source_system = 255;
    msg_vehicle_command.target_system = get_target_system(std::string(namespace_->currentText().toUtf8().constData()));
    msg_vehicle_command.target_component = 1;
    msg_vehicle_command.from_external = true;
    publisher_vehicle_command_->publish(msg_vehicle_command);
    RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "ARMING_STATE_INIT");

  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED)
  {
    px4_msgs::msg::VehicleCommand msg_vehicle_command;
    msg_vehicle_command.timestamp = time_node.nanoseconds()/1000;
    msg_vehicle_command.command = px4_msgs::msg::VehicleCommand::VEHICLE_CMD_COMPONENT_ARM_DISARM;
    msg_vehicle_command.param1 = 0;
    msg_vehicle_command.confirmation = 1;
    msg_vehicle_command.source_system = 255;
    msg_vehicle_command.target_system = get_target_system(std::string(namespace_->currentText().toUtf8().constData()));
    msg_vehicle_command.target_component = 1;
    msg_vehicle_command.from_external = true;
    publisher_vehicle_command_->publish(msg_vehicle_command);
    RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "VEHICLE_CMD_COMPONENT_ARM_DISARM");
  }
}

void Goal3DDisplay::valueChangedInterface()
{

  if(flying_){
    button_takeoff_->setText("Land");
    button_takeoff_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/Land.png"));
    button_arm_->setDisabled(true);
  }else{
    button_takeoff_->setText("TakeOff");
    button_takeoff_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/Takeoff.png"));
    button_arm_->setDisabled(false);
  }

  if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_INIT){
    label_arming_state_->setText(QString("Init"));
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_STANDBY){
    label_arming_state_->setText(QString("Standby"));
    button_arm_->setText(QString("Arm"));
    button_arm_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/PowerOn.png"));
    button_takeoff_->setEnabled(false);
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_ARMED){
    label_arming_state_->setText(QString("Armed"));
    button_arm_->setText(QString("Disarm"));
    button_arm_->setIcon(rviz_common::loadPixmap("package://rviz_aerial_plugins/icons/classes/PowerOff.png"));
    button_takeoff_->setEnabled(true);
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_STANDBY_ERROR){
    label_arming_state_->setText(QString("Standby error"));
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_SHUTDOWN){
    label_arming_state_->setText(QString("Shutdown"));
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_IN_AIR_RESTORE){
    label_arming_state_->setText(QString("In air restore"));
  }else if (arming_state_ == px4_msgs::msg::VehicleStatus::ARMING_STATE_MAX){
    label_arming_state_->setText(QString("Max"));
  }
}

void Goal3DDisplay::vehicle_status_callback(px4_msgs::msg::VehicleStatus::ConstSharedPtr msg)
{
  if (msg->vehicle_type == px4_msgs::msg::VehicleStatus::VEHICLE_TYPE_ROTARY_WING){
    label_vehicle_type_->setText("Quadcopter");
  }
  else if (msg->vehicle_type == px4_msgs::msg::VehicleStatus::VEHICLE_TYPE_FIXED_WING){
    label_vehicle_type_->setText("Fixed wing");
  }
  else if (msg->vehicle_type == px4_msgs::msg::VehicleStatus::VEHICLE_TYPE_ROVER){
    label_vehicle_type_->setText("Rover");
  }else{
    label_vehicle_type_->setText("Unknown");
  }

  if(arming_state_ != msg->arming_state){
    arming_state_ = msg->arming_state;
    // RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "arming_state %d", arming_state);
    emit valueChangedInterface_signal();
  }
}

void Goal3DDisplay::subcribe2topics()
{
  vehicle_gps_position_sub_ = rviz_ros_node_.lock()->get_raw_node()->
      template create_subscription<px4_msgs::msg::VehicleGpsPosition>(
        vehicle_gps_position_name_,
      10,
      [this](px4_msgs::msg::VehicleGpsPosition::ConstSharedPtr msg) {
        latitude_ = msg->lat*1E-7;
        longitude_ = msg->lon*1E-7;
        altitude_ = msg->alt*1E-3;
    });

  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Subscribe to: " + vehicle_gps_position_name_);

  vehicle_attitude_sub_ = rviz_ros_node_.lock()->get_raw_node()->
      template create_subscription<px4_msgs::msg::VehicleAttitude>(
        attitude_topic_name_,
      10,
      [this](px4_msgs::msg::VehicleAttitude::ConstSharedPtr msg) {

        geometry_msgs::msg::Quaternion q;
        q.x = msg->q[1];
        q.y = msg->q[2];
        q.z = msg->q[3];
        q.w = msg->q[0];
        double yaw, pitch, roll;
        tf2::getEulerYPR(q, yaw, pitch, roll);
        heading_ = yaw;
    });
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Subscribe to: " + attitude_topic_name_);

  vehicle_odometry_sub_ = rviz_ros_node_.lock()->get_raw_node()->
        template create_subscription<px4_msgs::msg::VehicleOdometry>(
          odometry_topic_name_,
        10,
        [this](px4_msgs::msg::VehicleOdometry::ConstSharedPtr msg) {
          altitude_rel_ = -msg->z;
      });
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Subscribe to: " + odometry_topic_name_);

  vehicle_land_detected_sub_ = rviz_ros_node_.lock()->get_raw_node()->
          template create_subscription<px4_msgs::msg::VehicleLandDetected>(
            vehicle_land_detected_topic_name_,
          10,
          [this](px4_msgs::msg::VehicleLandDetected::ConstSharedPtr msg) {
            if(msg->landed != !flying_){
              flying_ = !msg->landed;
              emit valueChangedInterface_signal();
            }
        });
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Subscribe to: " + vehicle_land_detected_topic_name_);

  vehicle_status_sub_ = rviz_ros_node_.lock()->get_raw_node()->
      template create_subscription<px4_msgs::msg::VehicleStatus>(
        vehicle_status_name_,
      10, std::bind(&Goal3DDisplay::vehicle_status_callback, this, std::placeholders::_1));

  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Subscribe to: " + vehicle_status_name_);

  publisher_vehicle_command_ =
    rviz_ros_node_.lock()->get_raw_node()->
        create_publisher<px4_msgs::msg::VehicleCommand>(vehicle_command_name_, 10);
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Publish to: " + vehicle_command_name_);

  publisher_setpoint_ =
    rviz_ros_node_.lock()->get_raw_node()->
      create_publisher<px4_msgs::msg::PositionSetpoint>(position_setpoint_topic_name_, 10);
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Publish to: " + position_setpoint_topic_name_);

  publisher_pose_stamped_ =
    rviz_ros_node_.lock()->get_raw_node()->
      create_publisher<geometry_msgs::msg::PoseStamped>(pose_stamped_name_, 10);
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "Publish to: " + pose_stamped_name_);

}

void Goal3DDisplay::on_changed_namespace(const QString& text)
{
  std::string namespace_str(text.toUtf8().constData());

  vehicle_gps_position_name_ = "/" + namespace_str + "/vehicle_gps_position";
  vehicle_command_name_ = "/" + namespace_str + "/vehicle_command";
  vehicle_status_name_ = "/" + namespace_str + "/vehicle_status";
  attitude_topic_name_ = "/" + namespace_str + "/vehicle_attitude";
  odometry_topic_name_ = "/" + namespace_str + "/vehicle_odometry";
  position_setpoint_topic_name_ = "/" + namespace_str + "/position_setpoint";
  vehicle_land_detected_topic_name_ = "/" + namespace_str + "/vehicle_land_detected";
  pose_stamped_name_ = "/" + namespace_str + "/command_pose";

  vehicle_gps_position_sub_.reset();
  vehicle_status_sub_.reset();
  vehicle_attitude_sub_.reset();
  vehicle_land_detected_sub_.reset();
  vehicle_odometry_sub_.reset();
  publisher_vehicle_command_.reset();
  publisher_setpoint_.reset();

  subcribe2topics();
}

void Goal3DDisplay::on_click_refresheButton()
{
  add_namespaces_to_combobox();

  auto time_node = rviz_ros_node_.lock()->get_raw_node()->get_clock()->now();
  px4_msgs::msg::VehicleCommand msg_vehicle_command;
  msg_vehicle_command.timestamp = time_node.nanoseconds()/1000;
  msg_vehicle_command.command = 175;//px4_msgs::msg::VehicleCommand::VEHICLE_CMD_DO_SET_HOME;
  msg_vehicle_command.confirmation = 1;
  msg_vehicle_command.source_system = 255;
  msg_vehicle_command.target_system = get_target_system(std::string(namespace_->currentText().toUtf8().constData()));
  msg_vehicle_command.target_component = 1;
  msg_vehicle_command.from_external = true;
  publisher_vehicle_command_->publish(msg_vehicle_command);
  RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), "VEHICLE_CMD_DO_SET_HOME");
}

geometry_msgs::msg::TransformStamped toMsg(const tf2::Stamped<tf2::Transform>& in)
{
  geometry_msgs::msg::TransformStamped out;
  out.header.stamp = tf2_ros::toMsg(in.stamp_);
  out.header.frame_id = in.frame_id_;
  out.transform.translation.x = in.getOrigin().getX();
  out.transform.translation.y = in.getOrigin().getY();
  out.transform.translation.z = in.getOrigin().getZ();
  out.transform.rotation = toMsg(in.getRotation());
  return out;
}

void Goal3DDisplay::processFeedback(
  const visualization_msgs::msg::InteractiveMarkerFeedback::ConstSharedPtr & feedback)
{
  std::ostringstream oss;
  oss << "Feedback from marker '" << feedback->marker_name << "' "
      << " / control '" << feedback->control_name << "'";

  switch (feedback->event_type) {
    case visualization_msgs::msg::InteractiveMarkerFeedback::POSE_UPDATE:
      oss << ": pose changed"
          << "\nposition = "
          << feedback->pose.position.x
          << ", " << feedback->pose.position.y
          << ", " << feedback->pose.position.z
          << "\norientation = "
          << feedback->pose.orientation.w
          << ", " << feedback->pose.orientation.x
          << ", " << feedback->pose.orientation.y
          << ", " << feedback->pose.orientation.z
          << "\nframe: " << feedback->header.frame_id;
      RCLCPP_INFO(rviz_ros_node_.lock()->get_raw_node()->get_logger(), oss.str());
      break;
  }

  server_->applyChanges();
}

visualization_msgs::msg::Marker
Goal3DDisplay::makeBox(const visualization_msgs::msg::InteractiveMarker & msg)
{
  visualization_msgs::msg::Marker marker;

  marker.type = visualization_msgs::msg::Marker::MESH_RESOURCE;
  marker.mesh_resource = "package://mavlink_sitl_gazebo/models/rotors_description/meshes/iris.stl";
  marker.scale.x = msg.scale * 0.45;
  marker.scale.y = msg.scale * 0.45;
  marker.scale.z = msg.scale * 0.45;
  marker.color.r = 0.5;
  marker.color.g = 0.5;
  marker.color.b = 0.5;
  marker.color.a = 1.0;

  return marker;
}

visualization_msgs::msg::InteractiveMarkerControl &
Goal3DDisplay::makeBoxControl(visualization_msgs::msg::InteractiveMarker & msg)
{
  visualization_msgs::msg::InteractiveMarkerControl control;
  control.always_visible = true;
  control.markers.push_back(makeBox(msg));
  msg.controls.push_back(control);

  return msg.controls.back();
}

void Goal3DDisplay::makeQuadrocopterMarker(const tf2::Vector3 & position)
{
  visualization_msgs::msg::InteractiveMarker int_marker;
  int_marker.header.frame_id = "map";
  int_marker.pose.position.x = position.getX();
  int_marker.pose.position.y = position.getY();
  int_marker.pose.position.z = position.getZ();
  int_marker.scale = 1;

  int_marker.name = "quadrocopter";
  int_marker.description = "Quadrocopter";

  makeBoxControl(int_marker);

  visualization_msgs::msg::InteractiveMarkerControl control;

  tf2::Quaternion orien(0.0, 1.0, 0.0, 1.0);
  orien.normalize();
  control.orientation = tf2::toMsg(orien);
  control.interaction_mode = visualization_msgs::msg::InteractiveMarkerControl::MOVE_ROTATE;
  int_marker.controls.push_back(control);
  control.interaction_mode = visualization_msgs::msg::InteractiveMarkerControl::MOVE_AXIS;
  int_marker.controls.push_back(control);

  server_->insert(int_marker);
  server_->setCallback(int_marker.name, std::bind(&Goal3DDisplay::processFeedback, this, std::placeholders::_1));
}

Goal3DDisplay::~Goal3DDisplay()
{
  server_.reset();
}

} // namespace displays

} // namespace rviz_aerial_plugins

#include <pluginlib/class_list_macros.hpp>  // NOLINT
PLUGINLIB_EXPORT_CLASS(rviz_aerial_plugins::displays::Goal3DDisplay, rviz_common::Panel)
