#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from moveit_msgs.srv import GetPositionIK
from geometry_msgs.msg import PoseStamped
import time

class TestPickAndPlace(Node):
    def __init__(self):
        super().__init__('test_pick_and_place')
        self.get_logger().info("Test Node initialized. We will just check if we can connect to MoveIt services or spin.")

def main(args=None):
    rclpy.init(args=args)
    
    # 1. Very basic test node to ensure Python can initialize in this environment
    node = TestPickAndPlace()
    
    node.get_logger().info("--- Pick and Place Environment Pre-check ---")
    node.get_logger().info("Please ensure MoveIt2 is running. This script simply validates the environment for python scripts.")
    node.get_logger().info("Actual pick and place is executed via the C++ node `ros2 run ros2srrc_execution pick_and_place`.")
    node.get_logger().info("Test finished.")
    
    rclpy.shutdown()

if __name__ == '__main__':
    main()
