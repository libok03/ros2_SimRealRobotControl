#!/bin/bash
# simulation.sh
# Run this script to execute the simulation locally using ros2srrc_ur5e and ur5e_2 configuration

source install/setup.bash


echo "Starting MoveIt2 and RViz2..."
ros2 launch ros2srrc_launch moveit2.launch.py package:=ros2srrc_ur5e config:=ur5e_2
