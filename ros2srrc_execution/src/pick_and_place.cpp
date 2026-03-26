#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <future>
#include <cmath>

#include <rclcpp/rclcpp.hpp>
#include <moveit/move_group_interface/move_group_interface_improved.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/msg/collision_object.hpp>
#include <shape_msgs/msg/solid_primitive.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <tf2/LinearMath/Quaternion.h>

#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
#include <linkattacher_msgs/srv/attach_link.hpp>
#include <linkattacher_msgs/srv/detach_link.hpp>
#endif
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
#include <ros_gz_interfaces/srv/spawn_entity.hpp>
#include <ros_gz_interfaces/srv/set_entity_pose.hpp>
#endif

using std::placeholders::_1;
using namespace std::chrono_literals;

class PickAndPlace : public rclcpp::Node
{
public:
  PickAndPlace(const rclcpp::NodeOptions &options)
      : Node("pick_and_place", options)
  {
    if (!this->has_parameter("use_sim_time")) {
      this->declare_parameter<bool>("use_sim_time", true);
    }
    robot_model_name_ = this->declare_parameter<std::string>("gazebo.robot_model_name", "ur5e");
    object_link_name_ = this->declare_parameter<std::string>("gazebo.object_link_name", "link");
    spawn_service_name_ = this->declare_parameter<std::string>("gazebo.spawn_service", "/world/default/create");
    set_pose_service_name_ = this->declare_parameter<std::string>("gazebo.set_pose_service", "/world/default/set_pose");
    grasp_offset_z_ = this->declare_parameter<double>("gazebo.grasp_offset_z", -0.06);
    cube_x_ = this->declare_parameter<double>("cube.x", 0.5);
    cube_y_ = this->declare_parameter<double>("cube.y", 0.0);
    cube_z_ = this->declare_parameter<double>("cube.z", 0.775);
    grasp_xy_tolerance_ = this->declare_parameter<double>("grasp.xy_tolerance", 0.04);
    grasp_min_z_offset_ = this->declare_parameter<double>("grasp.min_z_offset", 0.04);
    grasp_max_z_offset_ = this->declare_parameter<double>("grasp.max_z_offset", 0.12);
    pick_primary_z_offset_ = this->declare_parameter<double>("pick.primary_z_offset", 0.08);
    pick_retry_z_offset_ = this->declare_parameter<double>("pick.retry_z_offset", 0.10);
#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
    attach_client_ = this->create_client<linkattacher_msgs::srv::AttachLink>("/ATTACHLINK");
    detach_client_ = this->create_client<linkattacher_msgs::srv::DetachLink>("/DETACHLINK");
#endif
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
    spawn_entity_client_ = this->create_client<ros_gz_interfaces::srv::SpawnEntity>(spawn_service_name_);
    set_entity_pose_client_ = this->create_client<ros_gz_interfaces::srv::SetEntityPose>(set_pose_service_name_);
#endif
  }

  void execute()
  {
    // Initialize MoveGroup Interfaces
    moveit::planning_interface::MoveGroupInterface arm_group(shared_from_this(), "ur5e_arm");
    moveit::planning_interface::MoveGroupInterface gripper_group(shared_from_this(), "robotiq_2f85");
    moveit::planning_interface::PlanningSceneInterface planning_scene_interface;

    arm_group.setMaxVelocityScalingFactor(0.5);
    arm_group.setMaxAccelerationScalingFactor(0.5);
    arm_group.setNumPlanningAttempts(10);
    arm_group.setPlanningTime(5.0);

    gripper_group.setMaxVelocityScalingFactor(1.0);
    gripper_group.setMaxAccelerationScalingFactor(1.0);

    RCLCPP_INFO(this->get_logger(), "Waiting for MoveGroup Action Servers...");
    arm_group.startStateMonitor();
    gripper_group.startStateMonitor();
    
    rclcpp::sleep_for(std::chrono::seconds(2));

    RCLCPP_INFO(this->get_logger(), "MoveGroup Interfaces Initialized.");
    const std::string planning_frame = arm_group.getPlanningFrame();
    const std::string ee_link = arm_group.getEndEffectorLink();
    arm_group.setPoseReferenceFrame(planning_frame);
    RCLCPP_INFO(this->get_logger(), "Planning frame: %s, End-effector link: %s", planning_frame.c_str(), ee_link.c_str());

    ensure_cube_in_gazebo("cube");

    // --- Add Objects to Planning Scene ---
    RCLCPP_INFO(this->get_logger(), "Adding Desk and Cube to Planning Scene...");
    add_collision_objects(planning_scene_interface, planning_frame);
    rclcpp::sleep_for(std::chrono::seconds(2)); // allow time for the scene to update

    // --- Sequence ---

    // 1. Home
    RCLCPP_INFO(this->get_logger(), "Step 1: Moving to Home position");
    move_to_joint_state(arm_group, {0.0, -1.57, 1.57, -1.57, -1.57, 0.0});
    
    // 2. Open Gripper
    RCLCPP_INFO(this->get_logger(), "Step 2: Opening Gripper");
    move_gripper(gripper_group, 0.0);
    
    // Define the grasping orientation (gripper pointing down)
    tf2::Quaternion q;
    q.setRPY(3.14159, 0.0, 0.0); // Roll 180 degrees to point gripper downwards
    
    // 3. Pre-Pick (Above the cube)
    geometry_msgs::msg::Pose pre_pick_pose;
    pre_pick_pose.position.x = cube_x_;
    pre_pick_pose.position.y = cube_y_;
    pre_pick_pose.position.z = cube_z_ + 0.15; // 15cm above cube
    pre_pick_pose.orientation.x = q.x();
    pre_pick_pose.orientation.y = q.y();
    pre_pick_pose.orientation.z = q.z();
    pre_pick_pose.orientation.w = q.w();
    
    RCLCPP_INFO(this->get_logger(), "Step 3: Moving to Pre-Pick position");
    if (!move_to_pose(arm_group, pre_pick_pose)) {
      RCLCPP_ERROR(this->get_logger(), "Failed to reach Pre-Pick pose. Returning Home.");
      move_to_joint_state(arm_group, {0.0, -1.57, 1.57, -1.57, -1.57, 0.0});
      return;
    }

    // 4. Pick (Lower)
    geometry_msgs::msg::Pose pick_pose = pre_pick_pose;
    pick_pose.position.z = cube_z_ + pick_primary_z_offset_;
    
    RCLCPP_INFO(this->get_logger(), "Step 4: Lowering to Pick object");
    bool pick_pose_reached = move_to_pose(arm_group, pick_pose);
    if (!pick_pose_reached) {
      geometry_msgs::msg::Pose retry_pick_pose = pre_pick_pose;
      retry_pick_pose.position.z = cube_z_ + pick_retry_z_offset_;
      RCLCPP_WARN(
        this->get_logger(),
        "Primary pick pose failed. Retrying with higher pick z offset: %.3f",
        pick_retry_z_offset_
      );
      pick_pose_reached = move_to_pose(arm_group, retry_pick_pose);
      if (pick_pose_reached) {
        pick_pose = retry_pick_pose;
      }
    }
    if (!pick_pose_reached) {
      RCLCPP_ERROR(this->get_logger(), "Pick approach failed. Skipping grasp/attach and returning Home.");
      move_gripper(gripper_group, 0.0);
      move_to_joint_state(arm_group, {0.0, -1.57, 1.57, -1.57, -1.57, 0.0});
      return;
    }

    // 5. Close Gripper & Attach
    RCLCPP_INFO(this->get_logger(), "Step 5: Closing Gripper (Grasping) and Attaching Object");
    move_gripper(gripper_group, 0.75); // Ensure gripping width matches physical size. 0.75 is fully closed
    rclcpp::sleep_for(std::chrono::seconds(1));

    const bool grasp_valid = is_grasp_pose_valid(arm_group, ee_link);
    if (!grasp_valid) {
      RCLCPP_WARN(
        this->get_logger(),
        "Grasp validation failed. Detaching strategy: open gripper and return Home (no attach)."
      );
      move_gripper(gripper_group, 0.0);
      move_to_joint_state(arm_group, {0.0, -1.57, 1.57, -1.57, -1.57, 0.0});
      return;
    }

    bool gazebo_link_attached = false;
    bool manual_pose_follow = false;
    if (grasp_valid) {
      gazebo_link_attached = gazebo_attach("cube", ee_link);
      manual_pose_follow = !gazebo_link_attached;
      if (manual_pose_follow) {
        sync_object_to_end_effector(arm_group, ee_link, "cube");
      }
      arm_group.attachObject(
        "cube",
        ee_link,
        {
          "robotiq_85_left_finger_tip_link",
          "robotiq_85_right_finger_tip_link",
          "robotiq_85_left_finger_link",
          "robotiq_85_right_finger_link",
          "robotiq_85_left_inner_knuckle_link",
          "robotiq_85_right_inner_knuckle_link"
        }
      );
    }

    // 6. Post-Pick (Raise)
    RCLCPP_INFO(this->get_logger(), "Step 6: Raising object (Post-Pick)");
    move_to_pose(arm_group, pre_pick_pose);
    if (manual_pose_follow) {
      sync_object_to_end_effector(arm_group, ee_link, "cube");
    }

    // 7. Pre-Place (To the side on the desk)
    geometry_msgs::msg::Pose pre_place_pose = pre_pick_pose;
    pre_place_pose.position.y = -0.3; // Move 30cm to the right
    
    RCLCPP_INFO(this->get_logger(), "Step 7: Moving to Pre-Place position");
    move_to_pose(arm_group, pre_place_pose);
    if (manual_pose_follow) {
      sync_object_to_end_effector(arm_group, ee_link, "cube");
    }

    // 8. Place (Lower)
    geometry_msgs::msg::Pose place_pose = pre_place_pose;
    place_pose.position.z = pick_pose.position.z + 0.01; // Lower it almost back to the desk level
    
    RCLCPP_INFO(this->get_logger(), "Step 8: Lowering to Place object");
    move_to_pose(arm_group, place_pose);
    if (manual_pose_follow) {
      sync_object_to_end_effector(arm_group, ee_link, "cube");
    }

    // 9. Open Gripper & Detach
    RCLCPP_INFO(this->get_logger(), "Step 9: Detaching Object and Opening Gripper (Releasing)");
    if (gazebo_link_attached) {
      gazebo_detach("cube", ee_link);
    }
    arm_group.detachObject("cube");
    move_gripper(gripper_group, 0.0);
    rclcpp::sleep_for(std::chrono::seconds(1));

    // 10. Post-Place (Raise)
    RCLCPP_INFO(this->get_logger(), "Step 10: Raising arm (Post-Place)");
    move_to_pose(arm_group, pre_place_pose);

    // 11. Home
    RCLCPP_INFO(this->get_logger(), "Step 11: Returning Home");
    move_to_joint_state(arm_group, {0.0, -1.57, 1.57, -1.57, -1.57, 0.0});

    RCLCPP_INFO(this->get_logger(), "Pick and Place sequence completed successfully!");
  }

private:
  void add_collision_objects(
    moveit::planning_interface::PlanningSceneInterface& planning_scene_interface,
    const std::string& reference_frame
  )
  {
    const double desk_x = 0.5;
    const double desk_y = 0.0;
    const double desk_size_x = 0.6;
    const double desk_size_y = 0.8;
    const double desk_size_z = 0.75;
    const double cube_size = 0.05;
    const double cube_x = cube_x_;
    const double cube_y = cube_y_;
    const double cube_center_z = cube_z_;

    std::vector<moveit_msgs::msg::CollisionObject> collision_objects;
    
    // --- DESK ---
    moveit_msgs::msg::CollisionObject desk;
    desk.id = "desk";
    desk.header.frame_id = reference_frame;
    
    // Define the primitive and its dimensions
    desk.primitives.resize(1);
    desk.primitives[0].type = shape_msgs::msg::SolidPrimitive::BOX;
    desk.primitives[0].dimensions = { desk_size_x, desk_size_y, desk_size_z }; // x, y, z
    
    // Define the pose of the desk (center)
    desk.primitive_poses.resize(1);
    desk.primitive_poses[0].position.x = desk_x;
    desk.primitive_poses[0].position.y = desk_y;
    desk.primitive_poses[0].position.z = desk_size_z / 2.0;
    desk.primitive_poses[0].orientation.w = 1.0;

    desk.operation = desk.ADD;
    collision_objects.push_back(desk);


    // --- CUBE ---
    moveit_msgs::msg::CollisionObject cube;
    cube.id = "cube";
    cube.header.frame_id = reference_frame;
    
    cube.primitives.resize(1);
    cube.primitives[0].type = shape_msgs::msg::SolidPrimitive::BOX;
    cube.primitives[0].dimensions = { cube_size, cube_size, cube_size };
    
    cube.primitive_poses.resize(1);
    cube.primitive_poses[0].position.x = cube_x;
    cube.primitive_poses[0].position.y = cube_y;
    cube.primitive_poses[0].position.z = cube_center_z;
    cube.primitive_poses[0].orientation.w = 1.0;

    cube.operation = cube.ADD;
    collision_objects.push_back(cube);

    planning_scene_interface.applyCollisionObjects(collision_objects);
  }

  void move_to_joint_state(moveit::planning_interface::MoveGroupInterface &group, const std::vector<double> &joint_positions)
  {
    group.stop();
    group.clearPoseTargets();
    group.setStartStateToCurrentState();
    group.setJointValueTarget(joint_positions);
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    bool success = (group.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);

    if (success) {
      const auto exec_res = group.execute(my_plan);
      if (exec_res != moveit::core::MoveItErrorCode::SUCCESS) {
        RCLCPP_ERROR(this->get_logger(), "Joint trajectory execution failed.");
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to plan joint state trajectory.");
    }
    group.stop();
    group.clearPoseTargets();
  }

  bool move_to_pose(moveit::planning_interface::MoveGroupInterface &group, const geometry_msgs::msg::Pose &pose)
  {
    group.stop();
    group.clearPoseTargets();
    group.setStartStateToCurrentState();

    // Try to plan a Cartesian path directly to the pose
    std::vector<geometry_msgs::msg::Pose> waypoints;
    waypoints.push_back(pose);
    
    moveit_msgs::msg::RobotTrajectory trajectory;
    const double jump_threshold = 0.0;
    const double eef_step = 0.01;
    
    // computeCartesianPath is often smoother for simple line movements (pick/place approaches)
    double fraction = group.computeCartesianPath(waypoints, eef_step, jump_threshold, trajectory);
    
    if (fraction > 0.9) {
      moveit::planning_interface::MoveGroupInterface::Plan my_plan;
      my_plan.trajectory_ = trajectory;
      const auto exec_res = group.execute(my_plan);
      if (exec_res != moveit::core::MoveItErrorCode::SUCCESS) {
        RCLCPP_ERROR(this->get_logger(), "Cartesian execution failed.");
        group.stop();
        group.clearPoseTargets();
        return false;
      }
      group.stop();
      group.clearPoseTargets();
      return true;
    } else {
      RCLCPP_WARN(this->get_logger(), "Cartesian path failed. Trying standard pose target...");
      group.setStartStateToCurrentState();
      group.setPoseTarget(pose);
      moveit::planning_interface::MoveGroupInterface::Plan fallback_plan;
      if (group.plan(fallback_plan) == moveit::core::MoveItErrorCode::SUCCESS) {
        const auto exec_res = group.execute(fallback_plan);
        if (exec_res != moveit::core::MoveItErrorCode::SUCCESS) {
          RCLCPP_ERROR(this->get_logger(), "Pose target execution failed.");
          group.stop();
          group.clearPoseTargets();
          return false;
        }
        group.stop();
        group.clearPoseTargets();
        return true;
      } else {
        RCLCPP_ERROR(this->get_logger(), "Failed to plan pose trajectory.");
        group.stop();
        group.clearPoseTargets();
        return false;
      }
    }
  }

  void move_gripper(moveit::planning_interface::MoveGroupInterface &group, double position)
  {
    group.stop();
    group.clearPoseTargets();
    group.setStartStateToCurrentState();

    std::vector<double> gripper_joints;
    auto joint_names = group.getJointNames();
    
    for (size_t i = 0; i < joint_names.size(); ++i) {
        gripper_joints.push_back(position);
    }

    group.setJointValueTarget(gripper_joints);
    moveit::planning_interface::MoveGroupInterface::Plan my_plan;
    bool success = (group.plan(my_plan) == moveit::core::MoveItErrorCode::SUCCESS);

    if (success) {
      const auto exec_res = group.execute(my_plan);
      if (exec_res != moveit::core::MoveItErrorCode::SUCCESS) {
        RCLCPP_ERROR(this->get_logger(), "Gripper trajectory execution failed.");
      }
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to plan gripper trajectory.");
    }
    group.stop();
    group.clearPoseTargets();
  }

  bool wait_for_link_attacher_services()
  {
#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
    const auto timeout = 2s;
    const bool attach_ok = attach_client_->wait_for_service(timeout);
    const bool detach_ok = detach_client_->wait_for_service(timeout);
    if (!attach_ok || !detach_ok) {
      RCLCPP_WARN(
        this->get_logger(),
        "Gazebo link attacher services not available. /ATTACHLINK=%s /DETACHLINK=%s",
        attach_ok ? "ready" : "missing",
        detach_ok ? "ready" : "missing"
      );
      return false;
    }
    return true;
#else
    RCLCPP_WARN_ONCE(
      this->get_logger(),
      "linkattacher_msgs is not available at build time; Gazebo attach/detach is disabled."
    );
    return false;
#endif
  }

  bool gazebo_attach(const std::string& object_model, const std::string& ee_link)
  {
#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
    if (!wait_for_link_attacher_services()) {
      return false;
    }

    // Try common link names used by IFRA object spawner and by static SDF worlds.
    for (const std::string& object_link : {object_link_name_, object_model, std::string("link")}) {
      auto request = std::make_shared<linkattacher_msgs::srv::AttachLink::Request>();
      request->model1_name = robot_model_name_;
      request->link1_name = ee_link;
      request->model2_name = object_model;
      request->link2_name = object_link;

      auto future = attach_client_->async_send_request(request);
      if (future.wait_for(2s) == std::future_status::ready) {
        const auto response = future.get();
        if (response->success) {
          RCLCPP_INFO(
            this->get_logger(),
            "Gazebo attach success: %s/%s <-> %s/%s",
            request->model1_name.c_str(), request->link1_name.c_str(),
            request->model2_name.c_str(), request->link2_name.c_str()
          );
          return true;
        }
        RCLCPP_WARN(
          this->get_logger(),
          "Gazebo attach failed with link2='%s': %s",
          object_link.c_str(), response->message.c_str()
        );
      } else {
        RCLCPP_WARN(this->get_logger(), "Gazebo attach timed out for link2='%s'", object_link.c_str());
      }
    }
    return false;
#else
    (void)object_model;
    (void)ee_link;
    return false;
#endif
  }

  bool gazebo_detach(const std::string& object_model, const std::string& ee_link)
  {
#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
    if (!wait_for_link_attacher_services()) {
      return false;
    }

    for (const std::string& object_link : {object_link_name_, object_model, std::string("link")}) {
      auto request = std::make_shared<linkattacher_msgs::srv::DetachLink::Request>();
      request->model1_name = robot_model_name_;
      request->link1_name = ee_link;
      request->model2_name = object_model;
      request->link2_name = object_link;

      auto future = detach_client_->async_send_request(request);
      if (future.wait_for(2s) == std::future_status::ready) {
        const auto response = future.get();
        if (response->success) {
          RCLCPP_INFO(
            this->get_logger(),
            "Gazebo detach success: %s/%s -/-> %s/%s",
            request->model1_name.c_str(), request->link1_name.c_str(),
            request->model2_name.c_str(), request->link2_name.c_str()
          );
          return true;
        }
        RCLCPP_WARN(
          this->get_logger(),
          "Gazebo detach failed with link2='%s': %s",
          object_link.c_str(), response->message.c_str()
        );
      } else {
        RCLCPP_WARN(this->get_logger(), "Gazebo detach timed out for link2='%s'", object_link.c_str());
      }
    }
    return false;
#else
    (void)object_model;
    (void)ee_link;
    return false;
#endif
  }

  bool sync_object_to_end_effector(
    moveit::planning_interface::MoveGroupInterface& arm_group,
    const std::string& ee_link,
    const std::string& object_model
  )
  {
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
    if (!set_entity_pose_client_->wait_for_service(500ms)) {
      RCLCPP_WARN_ONCE(
        this->get_logger(),
        "Gazebo SetEntityPose service '%s' is not available.",
        set_pose_service_name_.c_str()
      );
      return false;
    }

    const auto ee_pose = arm_group.getCurrentPose(ee_link).pose;
    geometry_msgs::msg::Pose target_pose = ee_pose;
    target_pose.position.z += grasp_offset_z_;

    auto request = std::make_shared<ros_gz_interfaces::srv::SetEntityPose::Request>();
    request->entity.name = object_model;
    request->entity.type = request->entity.MODEL;
    request->pose = target_pose;

    auto future = set_entity_pose_client_->async_send_request(request);
    if (future.wait_for(1s) != std::future_status::ready) {
      RCLCPP_WARN(this->get_logger(), "SetEntityPose timeout for model '%s'", object_model.c_str());
      return false;
    }

    const auto response = future.get();
    if (!response->success) {
      RCLCPP_WARN(this->get_logger(), "SetEntityPose failed for model '%s'", object_model.c_str());
    }
    return response->success;
#else
    (void)arm_group;
    (void)ee_link;
    (void)object_model;
    return false;
#endif
  }

  bool set_gazebo_model_pose(const std::string& model_name, const geometry_msgs::msg::Pose& pose)
  {
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
    if (!set_entity_pose_client_->wait_for_service(500ms)) {
      return false;
    }

    auto request = std::make_shared<ros_gz_interfaces::srv::SetEntityPose::Request>();
    request->entity.name = model_name;
    request->entity.type = request->entity.MODEL;
    request->pose = pose;

    auto future = set_entity_pose_client_->async_send_request(request);
    if (future.wait_for(1s) != std::future_status::ready) {
      return false;
    }
    return future.get()->success;
#else
    (void)model_name;
    (void)pose;
    return false;
#endif
  }

  bool is_grasp_pose_valid(
    moveit::planning_interface::MoveGroupInterface& arm_group,
    const std::string& ee_link
  )
  {
    const auto ee_pose = arm_group.getCurrentPose(ee_link).pose;
    if (!std::isfinite(ee_pose.position.x) || !std::isfinite(ee_pose.position.y) || !std::isfinite(ee_pose.position.z)) {
      RCLCPP_WARN(this->get_logger(), "Grasp check failed: end-effector pose has non-finite values.");
      return false;
    }
    if (std::abs(ee_pose.position.x) < 1e-4 && std::abs(ee_pose.position.y) < 1e-4 && std::abs(ee_pose.position.z) < 1e-4) {
      RCLCPP_WARN(this->get_logger(), "Grasp check failed: end-effector pose is near origin (likely stale robot state).");
      return false;
    }

    const double dx = ee_pose.position.x - cube_x_;
    const double dy = ee_pose.position.y - cube_y_;
    const double xy_dist = std::sqrt((dx * dx) + (dy * dy));
    const double z_offset = ee_pose.position.z - cube_z_;

    const bool xy_ok = xy_dist <= grasp_xy_tolerance_;
    const bool z_ok = (z_offset >= grasp_min_z_offset_) && (z_offset <= grasp_max_z_offset_);

    RCLCPP_INFO(
      this->get_logger(),
      "Grasp check: xy_dist=%.4f (<=%.4f), z_offset=%.4f ([%.4f, %.4f])",
      xy_dist, grasp_xy_tolerance_, z_offset, grasp_min_z_offset_, grasp_max_z_offset_
    );
    return xy_ok && z_ok;
  }

  void ensure_cube_in_gazebo(const std::string& cube_name)
  {
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
    geometry_msgs::msg::Pose cube_pose;
    cube_pose.position.x = cube_x_;
    cube_pose.position.y = cube_y_;
    cube_pose.position.z = cube_z_;
    cube_pose.orientation.w = 1.0;

    if (set_gazebo_model_pose(cube_name, cube_pose)) {
      RCLCPP_INFO(this->get_logger(), "Gazebo cube '%s' exists. Pose reset done.", cube_name.c_str());
      return;
    }

    if (!spawn_entity_client_->wait_for_service(1s)) {
      RCLCPP_INFO(
        this->get_logger(),
        "Spawn service '%s' unavailable. Assuming cube is spawned by launch/world.",
        spawn_service_name_.c_str()
      );
      return;
    }

    auto request = std::make_shared<ros_gz_interfaces::srv::SpawnEntity::Request>();
    request->entity_factory.name = cube_name;
    request->entity_factory.allow_renaming = false;
    request->entity_factory.pose = cube_pose;
    request->entity_factory.sdf =
      "<sdf version='1.8'>"
      "  <model name='cube'>"
      "    <pose>0 0 0 0 0 0</pose>"
      "    <link name='link'>"
      "      <inertial>"
      "        <mass>0.1</mass>"
      "        <inertia>"
      "          <ixx>0.001</ixx><iyy>0.001</iyy><izz>0.001</izz>"
      "        </inertia>"
      "      </inertial>"
      "      <collision name='collision'>"
      "        <geometry><box><size>0.05 0.05 0.05</size></box></geometry>"
      "      </collision>"
      "      <visual name='visual'>"
      "        <geometry><box><size>0.05 0.05 0.05</size></box></geometry>"
      "        <material><ambient>1 0 0 1</ambient><diffuse>1 0 0 1</diffuse></material>"
      "      </visual>"
      "    </link>"
      "  </model>"
      "</sdf>";

    auto future = spawn_entity_client_->async_send_request(request);
    if (future.wait_for(2s) != std::future_status::ready) {
      RCLCPP_WARN(this->get_logger(), "Gazebo cube spawn timed out.");
      return;
    }

    if (future.get()->success) {
      RCLCPP_INFO(this->get_logger(), "Gazebo cube '%s' spawned.", cube_name.c_str());
    } else {
      RCLCPP_WARN(this->get_logger(), "Gazebo cube '%s' spawn failed.", cube_name.c_str());
    }
#else
    (void)cube_name;
#endif
  }

#ifdef ROS2SRRC_HAS_LINKATTACHER_MSGS
  std::string robot_model_name_;
  std::string object_link_name_;
  std::string spawn_service_name_;
  std::string set_pose_service_name_;
  double grasp_offset_z_{-0.06};
  double cube_x_{0.5};
  double cube_y_{0.0};
  double cube_z_{0.775};
  double grasp_xy_tolerance_{0.04};
  double grasp_min_z_offset_{0.04};
  double grasp_max_z_offset_{0.12};
  double pick_primary_z_offset_{0.08};
  double pick_retry_z_offset_{0.10};
  rclcpp::Client<linkattacher_msgs::srv::AttachLink>::SharedPtr attach_client_;
  rclcpp::Client<linkattacher_msgs::srv::DetachLink>::SharedPtr detach_client_;
#else
  std::string robot_model_name_;
  std::string object_link_name_;
  std::string spawn_service_name_;
  std::string set_pose_service_name_;
  double grasp_offset_z_{-0.06};
  double cube_x_{0.5};
  double cube_y_{0.0};
  double cube_z_{0.775};
  double grasp_xy_tolerance_{0.04};
  double grasp_min_z_offset_{0.04};
  double grasp_max_z_offset_{0.12};
  double pick_primary_z_offset_{0.08};
  double pick_retry_z_offset_{0.10};
#endif
#ifdef ROS2SRRC_HAS_ROS_GZ_INTERFACES
  rclcpp::Client<ros_gz_interfaces::srv::SpawnEntity>::SharedPtr spawn_entity_client_;
  rclcpp::Client<ros_gz_interfaces::srv::SetEntityPose>::SharedPtr set_entity_pose_client_;
#endif
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  
  rclcpp::NodeOptions node_options;
  node_options.automatically_declare_parameters_from_overrides(true);

  auto node = std::make_shared<PickAndPlace>(node_options);

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  std::thread spinner([&executor]() { executor.spin(); });

  std::this_thread::sleep_for(2s);

  node->execute();

  rclcpp::shutdown();
  spinner.join();
  return 0;
}
