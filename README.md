# このプロジェクトはなに

- mocubeというモジュラロボットを制御するためのROSワークスペースです

# 開発環境

- Ubuntu 24.04
- ros2 jazzy

# このプロジェクトの使い方

## 環境構築

- git clone git@github.com:nalinally/mocube_ws.git
- cd mocube_ws
- git submodule update --init --recursive
- colcon build
- source install/setup.bash

## 詳しい使い方

### 起動

- ros2 run atom_krs_ros_udp_bridge device_manager
- 適切なフォーマットでUDP通信してくれるデバイスが同じネットワーク内に現れると、トピックにいろいろな情報を流し始めます。
- TODO：詳細を書く
