"""
シミュレータで計画を実行し、RViz で見る.

起動するもの: module_sim, state_estimator, move_server, rviz2, plan_executor (plan_delay 秒後)

  ros2 launch mocube_sim demo.launch.py plan_file:=$PWD/plan.json
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    plan_file = LaunchConfiguration('plan_file')
    rviz_config = os.path.join(
        get_package_share_directory('mocube_sim'), 'rviz', 'mocube_sim.rviz')

    return LaunchDescription([
        DeclareLaunchArgument(
            'plan_file', description='plan で作った計画の JSON (絶対パス)。初期配置にも使う'),
        DeclareLaunchArgument(
            'ir_range', default_value='0.1',
            description='赤外線を受ける面の中心のずれ (一辺に対する割合)'),
        DeclareLaunchArgument(
            'sim_speed_gain_mm', default_value='0.2',
            description='シミュレータの本当の速度係数 [mm/s/deg]。推定とずらして試せる'),
        DeclareLaunchArgument(
            'run_plan', default_value='true', description='plan_executor を起動するか'),
        DeclareLaunchArgument(
            'plan_delay', default_value='5.0',
            description='plan_executor を起動するまでの時間 [s] (RViz の起動を待つ)'),
        DeclareLaunchArgument('rviz', default_value='true', description='RViz を起動するか'),

        Node(
            package='mocube_sim', executable='module_sim', output='screen',
            parameters=[{
                'layout_file': plan_file,
                'ir_range': LaunchConfiguration('ir_range'),
                'speed_gain_mm': LaunchConfiguration('sim_speed_gain_mm'),
            }]),
        Node(package='mocube_estimator', executable='state_estimator', output='screen'),
        Node(package='mocube_executor', executable='move_server', output='screen'),
        Node(
            package='rviz2', executable='rviz2', arguments=['-d', rviz_config],
            condition=IfCondition(LaunchConfiguration('rviz'))),
        TimerAction(
            period=LaunchConfiguration('plan_delay'),
            actions=[Node(
                package='mocube_planner', executable='plan_executor', output='screen',
                parameters=[{'plan_file': plan_file}],
                condition=IfCondition(LaunchConfiguration('run_plan')))]),
    ])
