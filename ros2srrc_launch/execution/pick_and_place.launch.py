#!/usr/bin/python3
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node
from moveit_configs_utils import MoveItConfigsBuilder
import yaml

def load_yaml(package_name, file_path):
    package_path = get_package_share_directory(package_name)
    absolute_file_path = os.path.join(package_path, file_path)
    try:
        with open(absolute_file_path, 'r') as file:
            return yaml.safe_load(file)
    except EnvironmentError:
        return None

def generate_launch_description():
    # Build a rapid MoveIt configuration dictionary using the same paths as simulation.
    ROBOT = "ur5e"
    EE_NAME = "robotiq_2f85"

    moveit_config_builder = (
        MoveItConfigsBuilder(ROBOT, package_name="ros2srrc_robots")
        .robot_description(file_path=os.path.join(get_package_share_directory("ros2srrc_ur5e"), "urdf", "ur5e_robotiq_2f85.urdf.xacro"), mappings={"EE": "true", "EE_name": EE_NAME})
        .robot_description_kinematics(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/kinematics.yaml"))
        .planning_pipelines(pipelines=["ompl", "pilz_industrial_motion_planner"])
        .pilz_cartesian_limits(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/pilz_cartesian_limits.yaml"))
        .robot_description_semantic(file_path=os.path.join(get_package_share_directory("ros2srrc_moveit"), "config", f"{ROBOT}_{EE_NAME}.srdf"))
        .joint_limits(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/joint_limits.yaml"))
        .trajectory_execution(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/controller_moveit2.yaml"))
    )

    moveit_config = moveit_config_builder.to_moveit_configs()
    
    # Merge joint limits for End Effector
    YAML_EE_LIMITS = load_yaml("ros2srrc_endeffectors", EE_NAME + "/config/joint_limits.yaml")["joint_limits"]
    moveit_config.joint_limits["robot_description_planning"]["joint_limits"].update(YAML_EE_LIMITS)

    moveit_config_dict = moveit_config.to_dict()
    moveit_config_dict.update({"use_sim_time": True})

    # Pick and Place Standalone Execute Node
    pick_and_place_node = Node(
        name="pick_and_place",
        package="ros2srrc_execution",
        executable="pick_and_place",
        output="screen",
        parameters=[moveit_config_dict],
    )

    return LaunchDescription([pick_and_place_node])
