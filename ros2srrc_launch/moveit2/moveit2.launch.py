#!/usr/bin/python3

# ===================================== COPYRIGHT ===================================== #
#                                                                                       #
#  IFRA (Intelligent Flexible Robotics and Assembly) Group, CRANFIELD UNIVERSITY        #
#  Created on behalf of the IFRA Group at Cranfield University, United Kingdom          #
#  E-mail: IFRA@cranfield.ac.uk                                                         #
#                                                                                       #
#  Licensed under the Apache-2.0 License.                                               #
#  You may not use this file except in compliance with the License.                     #
#  You may obtain a copy of the License at: http://www.apache.org/licenses/LICENSE-2.0  #
#                                                                                       #
#  Unless required by applicable law or agreed to in writing, software distributed      #
#  under the License is distributed on an "as-is" basis, without warranties or          #
#  conditions of any kind, either express or implied. See the License for the specific  #
#  language governing permissions and limitations under the License.                    #
#                                                                                       #
#  IFRA Group - Cranfield University                                                    #
#  AUTHORS: Mikel Bueno Viso - Mikel.Bueno-Viso@cranfield.ac.uk                         #
#           Dr. Seemal Asif  - s.asif@cranfield.ac.uk                                   #
#           Prof. Phil Webb  - p.f.webb@cranfield.ac.uk                                 #
#                                                                                       #
#  Date: June, 2024.                                                                    #
#                                                                                       #
# ===================================== COPYRIGHT ===================================== #

# ======= CITE OUR WORK ======= #
# You can cite our work with the following statement:
# IFRA-Cranfield (2023) ROS 2 Sim-to-Real Robot Control. URL: https://github.com/IFRA-Cranfield/ros2_SimRealRobotControl.

# moveit2.launch.py:
# Launch file for the Robot's GAZEBO SIMULATION + MoveIt!2 Framework in ROS2 Humble:

# Import libraries:
import os, sys, xacro, yaml
from ament_index_python.packages import get_package_share_directory, PackageNotFoundError
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import IncludeLaunchDescription, RegisterEventHandler, TimerAction, DeclareLaunchArgument
from launch.event_handlers import OnProcessExit
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch.launch_description_sources import PythonLaunchDescriptionSource
from moveit_configs_utils import MoveItConfigsBuilder


# LOAD FILE:
def load_file(package_name, file_path):
    package_path = get_package_share_directory(package_name)
    absolute_file_path = os.path.join(package_path, file_path)
    try:
        with open(absolute_file_path, 'r') as file:
            return file.read()
    except EnvironmentError:
        # parent of IOError, OSError *and* WindowsError where available.
        return None
# LOAD YAML:
def load_yaml(package_name, file_path):
    package_path = get_package_share_directory(package_name)
    absolute_file_path = os.path.join(package_path, file_path)
    try:
        with open(absolute_file_path, 'r') as file:
            return yaml.safe_load(file)
    except EnvironmentError:
        # parent of IOError, OSError *and* WindowsError where available.
        return None

# ===== REQUIRED TO GET THE ROBOT CONFIGURATION === #

# EVALUATE INPUT ARGUMENTS:
def AssignArgument(ARGUMENT):
    ARGUMENTS = sys.argv
    for y in ARGUMENTS:
        if (ARGUMENT + ":=") in y:
            ARG = y.replace((ARGUMENT + ":="),"")
            return(ARG)

# GET CONFIGURATION from YAML:
def GetCONFIG(CONFIGURATION, PKG_PATH):
    
    RESULT = {"Success": False, "ID": "", "Name": "", "urdf": "", "ee": ""}
    
    YAML_PATH = PKG_PATH + "/config/configurations.yaml"
    
    if not os.path.exists(YAML_PATH):
        return (RESULT)
    
    with open(YAML_PATH, 'r') as YAML:
        cYAML = yaml.safe_load(YAML)

    for x in cYAML["Configurations"]:

        if x["ID"] == CONFIGURATION:
            RESULT["Success"] = True
            RESULT["ID"] = x["ID"]
            RESULT["Name"] = x["Name"]
            RESULT["urdf"] = x["urdf"]
            RESULT["rob"] = x["rob"]
            RESULT["ee"] = x["ee"]

    return(RESULT)

# GET EE-Controllers LIST:
def GetEEctr(EEName):
    
    RESULT = []

    PATH = os.path.join(get_package_share_directory('ros2srrc_endeffectors'), EEName, 'config')
    YAML_PATH = PATH + "/controller_moveit2.yaml"
    
    with open(YAML_PATH, 'r') as YAML:
        cYAML = yaml.safe_load(YAML)

    for x in cYAML["controller_names"]:
        RESULT.append(x)

    return(RESULT)

# CHECK if CONTROLLER file exists for EE:
def EEctrlEXISTS(EEName):
    
    PATH = os.path.join(get_package_share_directory('ros2srrc_endeffectors'), EEName, 'config')
    YAML_PATH = PATH + "/controller.yaml"
    
    RES = os.path.exists(YAML_PATH)
    return(RES)

# ========== **GENERATE LAUNCH DESCRIPTION** ========== #
def generate_launch_description():

    LD = LaunchDescription()
    
    # === INPUT ARGUMENT: ROS 2 PACKAGE === #
    PACKAGE_NAME = AssignArgument("package")
    if PACKAGE_NAME != None:
        None
    else:
        print("")
        print("ERROR: package INPUT ARGUMENT has not been defined. Please try again.")
        print("Closing... BYE!")
        exit()
        
    # CHECK if -> PACKAGE EXISTS, and GET PATH:
    try:
        PKG_PATH = get_package_share_directory(PACKAGE_NAME)
    except PackageNotFoundError:
        print("")
        print("ERROR: The defined ROS 2 Package was not found. Please try again.")
        print("Closing... BYE!")
        exit()
    except ValueError:
        print("")
        print("ERROR: The defined ROS 2 Package name is not valid. Please try again.")
        print("Closing... BYE!")
        exit()
    
    # === INPUT ARGUMENT: CONFIGURATION === #
    CONFIG = AssignArgument("config")
    CONFIGURATION = GetCONFIG(CONFIG, PKG_PATH)

    if CONFIGURATION["Success"] == False:
        print("")
        print("ERROR: config INPUT ARGUMENT has not been correctly defined. Please try again.")
        print("Closing... BYE!")
        exit()   

    # ========== CELL INFORMATION ========== #
    print("")
    print("===== GAZEBO: Robot Simulation + MoveIt!2 Framework (" + PACKAGE_NAME + ") =====")
    print("Robot configuration:")
    print(CONFIGURATION["ID"] + " -> " + CONFIGURATION["Name"])
    print("")
    
    # ***** GAZEBO ***** #   
    # DECLARE Gazebo WORLD file:
    world_gazebo = os.path.join(
        get_package_share_directory('ros2srrc_gazebo'),
        'worlds',
        'ros2srrc_gazebo.world')
    # DECLARE Gazebo LAUNCH file:
    gazebo = IncludeLaunchDescription(
                PythonLaunchDescriptionSource([os.path.join(get_package_share_directory('ros_gz_sim'), 'launch'), '/gz_sim.launch.py']),
                launch_arguments={'gz_args': f"-r {world_gazebo}"}.items(),
            )
            
    # BRIDGE for clock (uses use_sim_time)
    bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        arguments=['/clock@rosgraph_msgs/msg/Clock[ignition.msgs.Clock'],
        output='screen'
    )

    # ***** ROBOT DESCRIPTION ***** #
    # Robot Description file package:
    robot_description_path = os.path.join(get_package_share_directory(PACKAGE_NAME))
    # ROBOT urdf file path:
    xacro_file = os.path.join(robot_description_path,'urdf',CONFIGURATION["urdf"])
    # Generate ROBOT_DESCRIPTION variable:
    doc = xacro.parse(open(xacro_file))
    
    if CONFIGURATION["ee"] == "none":
        EE = "false"
    else: 
        EE = "true"
    
    xacro.process_doc(doc, mappings={
        "EE": EE,
        "EE_name": CONFIGURATION["ee"],
    })
    
    # EE -> Controller file needed?
    EE_CONFIG_STATE = EE
    if EE == "true":
        if EEctrlEXISTS(CONFIGURATION["ee"]) == False:
            EE_CONFIG_STATE = "true-NOctr"
    
    robot_description_config = doc.toxml()
    robot_description = {'robot_description': robot_description_config}

    # ROBOT STATE PUBLISHER NODE:
    node_robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        output='both',
        parameters=[
            robot_description,
            {"use_sim_time": True}
        ]
    )
    static_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="static_transform_publisher",
        output="log",
        arguments=["0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "world", "base_link"],
    )
    
    # JOINT STATE PUBLISHER (Requested by User):
    node_joint_state_publisher = Node(
        package='joint_state_publisher',
        executable='joint_state_publisher',
        name='joint_state_publisher',
        parameters=[
            {'source_list': ['/joint_states']},
            {'use_sim_time': True}
        ],
        output='log'
    )

    # SPAWN ROBOT TO GAZEBO:
    spawn_entity = Node(package='ros_gz_sim', executable='create',
                        arguments=['-name', CONFIGURATION["rob"],
                                   '-topic', 'robot_description',
                                   '-x', '0.0', '-y', '0.0', '-z', '0.0'],
                        output='both')
    cube_sdf = (
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
        "</sdf>"
    )
    spawn_cube = Node(
        package='ros_gz_sim',
        executable='create',
        arguments=['-name', 'cube', '-string', cube_sdf, '-x', '0.5', '-y', '0.0', '-z', '0.775'],
        output='both'
    )

    # ***** CONTROLLERS ***** #
    # Joint STATE BROADCASTER:
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster", "--controller-manager", "/controller_manager"],
    )
    # Joint TRAJECTORY Controller:
    joint_trajectory_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_trajectory_controller", "-c", "/controller_manager"],
    )

    # EE CONTROLLERS:
    if EE == "true":
        CONTROLLERS = GetEEctr(CONFIGURATION["ee"])
        CONTROLLER_NODES = []

        for x in CONTROLLERS:
            CONTROLLER_NODES.append(
                Node(
                    package="controller_manager",
                    executable="spawner",
                    arguments=[x, "-c", "/controller_manager"],
                )
            )

    # *********************** MoveIt!2 *********************** #   

    # *** PLANNING CONTEXT *** #
    # We will build up MoveItConfigsBuilder dynamically based on IFRA Configurations
    
    # Extract Robot and EE variables
    ROBOT = CONFIGURATION["rob"]
    EE_NAME = CONFIGURATION["ee"]
        
    moveit_config_builder = (
        MoveItConfigsBuilder(ROBOT, package_name="ros2srrc_robots")
        .robot_description(file_path=os.path.join(get_package_share_directory(PACKAGE_NAME), "urdf", CONFIGURATION["urdf"]), mappings={"EE": EE, "EE_name": EE_NAME})
        .robot_description_kinematics(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/kinematics.yaml"))
        .planning_pipelines(pipelines=["ompl", "pilz_industrial_motion_planner"])
        .pilz_cartesian_limits(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/pilz_cartesian_limits.yaml"))
    )
    
    # 1. Semantic description
    if (EE == "false"):
        moveit_config_builder.robot_description_semantic(file_path=os.path.join(get_package_share_directory("ros2srrc_moveit"), "config", f"{ROBOT}.srdf"))
    else:
        moveit_config_builder.robot_description_semantic(file_path=os.path.join(get_package_share_directory("ros2srrc_moveit"), "config", f"{ROBOT}_{EE_NAME}.srdf"))
        
    # Build core using builder
    moveit_config_builder.joint_limits(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/joint_limits.yaml"))
    moveit_config_builder.trajectory_execution(file_path=os.path.join(get_package_share_directory("ros2srrc_robots"), f"{ROBOT}/config/controller_moveit2.yaml"))

    # Extract config
    moveit_config = moveit_config_builder.to_moveit_configs()

    # PILZ Additions
    pilz_planning_pipeline_config = {
        "move_group": {
            "planning_plugin": "pilz_industrial_motion_planner/CommandPlanner",
            "request_adapters": """ """,
            "start_state_max_bounds_error": 0.1,
            "default_planner_config": "PTP",
        }
    }
    
    moveit_controllers = {
        "moveit_simple_controller_manager": moveit_config.trajectory_execution,
        "moveit_controller_manager": "moveit_simple_controller_manager/MoveItSimpleControllerManager",
    }
    
    # 2 & 3. Combine Joint Limits and Trajectory Execution Controllers manually if EE is active
    if (EE_CONFIG_STATE != "false") and (EE_CONFIG_STATE != "true-NOctr"):
        YAML_EE_LIMITS = load_yaml("ros2srrc_endeffectors", EE_NAME + "/config/joint_limits.yaml")["joint_limits"]
        moveit_config.joint_limits["robot_description_planning"]["joint_limits"].update(YAML_EE_LIMITS)
        
        YAML_EE_CTRL = load_yaml("ros2srrc_endeffectors", EE_NAME + "/config/controller_moveit2.yaml")
        for x in moveit_controllers["moveit_simple_controller_manager"]["controller_names"]:
            if x not in YAML_EE_CTRL["controller_names"]:
                YAML_EE_CTRL["controller_names"].append(x)
        combin_ctrls = moveit_controllers["moveit_simple_controller_manager"] | YAML_EE_CTRL
        moveit_controllers["moveit_simple_controller_manager"] = combin_ctrls
    
    trajectory_execution = {
        "moveit_manage_controllers": True,
        "trajectory_execution.allowed_execution_duration_scaling": 1.2,
        "trajectory_execution.allowed_goal_duration_margin": 0.5,
        "trajectory_execution.allowed_start_tolerance": 0.01,
    }
    
    planning_scene_monitor_parameters = {
        "publish_planning_scene": True,
        "publish_geometry_updates": True,
        "publish_state_updates": True,
        "publish_transforms_updates": True,
    }

    moveit_config_dict = moveit_config.to_dict()
    moveit_config_dict.update(pilz_planning_pipeline_config)
    moveit_config_dict.update(moveit_controllers)
    moveit_config_dict.update(trajectory_execution)
    moveit_config_dict.update(planning_scene_monitor_parameters)
    moveit_config_dict.update({"use_sim_time": True})

    # MoveGroup Node:
    run_move_group_node = Node(
        package="moveit_ros_move_group",
        executable="move_group",
        output="screen",
        parameters=[
            moveit_config_dict
        ],
    )

    # RVIZ:
    rviz_base = os.path.join(get_package_share_directory("ros2srrc_moveit"), "config")
    if EE == "false":
        rviz_full_config = os.path.join(rviz_base, CONFIGURATION["rob"] + ".rviz")
    else:
        rviz_full_config = os.path.join(rviz_base, CONFIGURATION["rob"] + "_" + CONFIGURATION["ee"] + ".rviz")

    rviz_node_full = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        output="log",
        arguments=["-d", rviz_full_config],
        parameters=[
            moveit_config_dict,
        ]
    )

    # =================================================================================================== #
    # ============================= ros2srrc_execution -> CUSTOM INTERFACES ============================= #

    # Move and Sequence:
    # if EE == "true":
    #     MoveInterface = Node(
    #         name="move",
    #         package="ros2srrc_execution",
    #         executable="move",
    #         output="screen",
    #         parameters=[robot_description, robot_description_semantic, kinematics_yaml, {"use_sim_time": True}, {"ROB_PARAM": CONFIGURATION["rob"]}, {"EE_PARAM": CONFIGURATION["ee"]}, {"ENV_PARAM": "gazebo"}],
    #     )
    # else:
    #     MoveInterface = Node(
    #         name="move",
    #         package="ros2srrc_execution",
    #         executable="move",
    #         output="screen",
    #         parameters=[robot_description, robot_description_semantic, kinematics_yaml, {"use_sim_time": True}, {"ROB_PARAM": CONFIGURATION["rob"]}, {"EE_PARAM": "none"}, {"ENV_PARAM": "gazebo"}],
    #     )

    # RobMove and RobPose:
    # RobMoveInterface = Node(
    #     name="robmove",
    #     package="ros2srrc_execution",
    #     executable="robmove",
    #     output="screen",
    #     parameters=[robot_description, robot_description_semantic, kinematics_yaml, {"use_sim_time": True}, {"ROB_PARAM": CONFIGURATION["rob"]}],
    # )
    # RobPoseInterface = Node(
    #     name="robpose",
    #     package="ros2srrc_execution",
    #     executable="robpose",
    #     output="screen",
    #     parameters=[robot_description, robot_description_semantic, kinematics_yaml, {"use_sim_time": True}, {"ROB_PARAM": CONFIGURATION["rob"]}],
    # )
    
    # =============================================== #
    # ========== RETURN LAUNCH DESCRIPTION ========== #

    # Add ROS 2 Nodes to LaunchDescription() element:
    LD.add_action(gazebo)
    LD.add_action(bridge)
    LD.add_action(node_robot_state_publisher)
    LD.add_action(static_tf)
    LD.add_action(node_joint_state_publisher)
    LD.add_action(spawn_entity)

    LD.add_action(RegisterEventHandler(
        OnProcessExit(
            target_action = spawn_entity,
            on_exit = [
                joint_state_broadcaster_spawner,
                ]
            )
        )
    )

    LD.add_action(RegisterEventHandler(
        OnProcessExit(
            target_action = spawn_entity,
            on_exit = [
                joint_trajectory_controller_spawner,
                ]
            )
        )
    )

    if EE == "true":

        for x in CONTROLLER_NODES:

            LD.add_action(RegisterEventHandler(
                OnProcessExit(
                    target_action = joint_trajectory_controller_spawner,
                    on_exit = [
                        x,
                        ]
                    )
                )
            )

    LD.add_action(RegisterEventHandler(
        OnProcessExit(
            target_action = spawn_entity,
            on_exit = [
                
                # MoveIt!2:
                TimerAction(
                    period=2.0,
                    actions=[
                        rviz_node_full,
                        run_move_group_node,
                    ]
                ),
                
                ]
            )
        )
    )

    LD.add_action(RegisterEventHandler(
        OnProcessExit(
            target_action = spawn_entity,
            on_exit = [
                TimerAction(
                    period=1.0,
                    actions=[
                        spawn_cube,
                    ]
                ),
            ]
        )
    ))

    LD.add_action(RegisterEventHandler(
        OnProcessExit(
            target_action = spawn_entity,
            on_exit = [
                
                # Interfaces:
                TimerAction(
                    period=5.0,
                    actions=[
                        # MoveInterface,
                        # RobMoveInterface,
                        # RobPoseInterface,
                    ]
                ),
                
                ]
            )
        )
    )

    # ***** RETURN  ***** #
    return(LD)
