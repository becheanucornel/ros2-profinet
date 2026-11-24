# SPDX-License-Identifier: Apache-2.0
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("config", description="Path of config_profinet.xml"),
        Node(package="profinet_bridge", executable="profinet_bridge", output="screen",
             parameters=[{"config": LaunchConfiguration("config")}]),
    ])
