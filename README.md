# UR5e Pick and Place in ROS2 🤖

A comprehensive ROS 2 workspace for simulating and executing Pick and Place operations using a **Universal Robots UR5e** manipulator equipped with a **Robotiq 2F-85** Gripper. 

This repository provides a complete pipeline from Gazebo simulation to MoveIt 2 motion planning, with a robust approach for grasping and manipulating objects in a custom environment.

## 🌟 Key Features

- **Full ROS 2 Integration**: Built natively on ROS 2 (Humble/Iron) using modern `launch.py` structures.
- **Gazebo Classic Simulation**: Realistic physics and environment rendering, complete with a custom desk and target cubes.
- **MoveIt 2 Motion Planning**: Advanced obstacle avoidance, Cartesian path planning, and end-effector control.
- **One-Click Scripts**: Easy-to-use bash scripts (`simulation.sh` and `pick_and_place.sh`) to instantly spin up the environment and run the logic.

---

## 📹 Demonstration

*(Below is a placeholder for your actual simulation GIF/Video. You can replace `assets/demo.gif` with your own screen recording of the Gazebo simulation in action!)*

![Demo](assets/demo.gif)

---

## 🛠 Prerequisites

To run this workspace, ensure you have the following installed:
* **Ubuntu 22.04** (Recommended)
* **ROS 2** (Humble)
* **Gazebo Classic** (ver 11.x)
* **MoveIt 2**
* Standard ROS 2 build tools (`colcon`, `rosdep`)

---

## 🚀 Getting Started

### 1. Build the Workspace

Navigate to the root directory and build the packages:

```bash
cd UR5e_Pick_and_Place_ROS2
colcon build --symlink-install
source install/setup.bash
```

### 2. Launch the Simulation (Gazebo + RViz2 + MoveIt2)

If you only want to start the environment and manually interact via RViz:
```bash
./simulation.sh
```

### 3. Run the Autonomous Pick and Place

To launch both the simulation and the autonomous pick-and-place logic sequence, simply run:
```bash
./pick_and_place.sh
```
Watch the UR5e meticulously approach the cube, grasp it, and move it to the target location!

---

## 📂 Project Structure

- `Universal_Robots_ROS2_Driver/` & `ros2_controllers/`: Core UR5e dependencies.
- `ros2_robotiq_gripper/`: Gripper simulation drivers.
- `ros2srrc_ur5e/`: Custom package containing MoveIt 2 configurations (`ur5e_2` config) and Gazebo worlds.
- `ros2srrc_execution/`: C++ nodes handling the `PlanningSceneInterface` and path planning logic.
- `pick_and_place.sh`: Master script that orchestrates the entire workflow.

---

## 📝 License

This project is open-source and available under the terms of the MIT License. Feel free to fork, modify, and use it for your own robotic manipulation projects!
