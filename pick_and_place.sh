#!/bin/bash
# pick_and_place.sh
# Run this script to execute the simulation, MoveIt2, and the Pick and Place sequence.
set -eo pipefail

# 1. Source workspace
source install/setup.bash

echo "==========================================="
echo " Starting Gazebo Simulation and MoveIt2... "
echo "==========================================="

# 2. Launch the simulation in the background
ros2 launch ros2srrc_launch moveit2.launch.py package:=ros2srrc_ur5e config:=ur5e_2 &
SIM_PID=$!

echo "Simulation launched with PID $SIM_PID. Waiting for 15 seconds for it to settle..."
sleep 15

echo "==========================================="
echo " Starting Pick and Place Sequence...       "
echo "==========================================="

# 3. Run pick-and-place via launch so MoveIt parameters are injected.
#    Running the node directly with `ros2 run` can miss robot_description /
#    robot_description_semantic and fail before planning.
ros2 launch "$(ros2 pkg prefix ros2srrc_launch)/share/ros2srrc_launch/execution/pick_and_place.launch.py"

# Keep the terminal alive if needed or kill the background process when script exits
echo "Pick and Place finished. Press Ctrl+C to stop the simulation."
wait $SIM_PID
