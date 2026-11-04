"""
module_sim ノード: device_manager と実機のモジュールの代わりになるシミュレータ.

device_manager と同じトピックを使う。
  購読: /servo_commands (ServoCommands)
  出力: /modules (Modules)
これに加えて、デバッグ用に本当の位置を出す。
  出力: /sim/true_states (ModuleStates)。grid_* は最も近い格子点
  サービス: /sim/set_grid_positions (SetGridPositions)。モジュールを格子点へ置き直す

指令の検査とパケットへの変換は mocube_bridge の device_manager と同じ関数を使い、
パケットの解釈はファーム (ServoController) の写しで行う。
"""

import time

from geometry_msgs.msg import Point
from mocube_bridge.device_manager import to_servo_packet
from mocube_bridge.protocol import ProtocolError
from mocube_msgs.msg import IrReceiver, Module, Modules, ModuleState, ModuleStates, ServoCommands
from mocube_msgs.srv import SetGridPositions
from mocube_sim.world import load_layout, World
import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node


class ModuleSim(Node):

    def __init__(self):
        super().__init__('module_sim')

        layout_file = self.declare_parameter('layout_file', '').value
        edge_length = self.declare_parameter('edge_length_mm', 113.1).value / 1000.0
        speed_gain = self.declare_parameter('speed_gain_mm', 0.2).value / 1000.0
        ir_range = self.declare_parameter('ir_range', 0.1).value
        contact_tolerance = self.declare_parameter('contact_tolerance', 0.1).value
        beacon_period = self.declare_parameter('beacon_period', 0.1).value
        heartbeat_period = self.declare_parameter('heartbeat_period', 1.0).value
        timeout = self.declare_parameter('timeout', 3.0).value
        watchdog = self.declare_parameter('watchdog', 1.0).value
        publish_period = self.declare_parameter('publish_period', 0.01).value
        true_state_period = self.declare_parameter('true_state_period', 0.05).value

        if not layout_file:
            raise ValueError('layout_file パラメータで初期配置を指定してください')
        layout = load_layout(layout_file)
        self._world = World(
            layout, edge_length, speed_gain, time.time(),
            ir_range=ir_range, contact_tolerance=contact_tolerance,
            beacon_period=beacon_period, heartbeat_period=heartbeat_period,
            timeout=timeout, watchdog=watchdog)
        self.get_logger().info(
            f'Simulating {len(layout)} modules from {layout_file} '
            f'(speed_gain_mm={speed_gain * 1000.0:g}, ir_range={ir_range:g})')

        self._servo_subscription = self.create_subscription(
            ServoCommands, 'servo_commands', self._on_servo_commands, 10)
        self._publisher = self.create_publisher(Modules, 'modules', 10)
        self._true_publisher = self.create_publisher(ModuleStates, 'sim/true_states', 10)
        self._service = self.create_service(
            SetGridPositions, 'sim/set_grid_positions', self._on_set_grid_positions)
        self._timer = self.create_timer(publish_period, self._on_timer)
        self._true_timer = self.create_timer(true_state_period, self._on_true_timer)

    def _on_timer(self):
        now = time.time()
        self._world.advance(now)
        msg = Modules()
        msg.t = now
        for module in self._world.modules.values():
            state = Module(
                id=module.id, mac=module.mac, addr='127.0.0.1',
                last_seen=float(module.last_seen), online=module.online)
            for face, (address, t) in module.receivers.items():
                state.receivers.append(IrReceiver(face=face, address=int(address), t=float(t)))
            msg.modules.append(state)
        self._publisher.publish(msg)

    def _on_true_timer(self):
        msg = ModuleStates()
        msg.t = self._world.time
        grids = self._world.grid_positions()
        for module_id, position in sorted(self._world.positions().items()):
            grid = grids[module_id]
            msg.modules.append(ModuleState(
                id=module_id, localized=True,
                position=Point(x=float(position[0]), y=float(position[1]), z=float(position[2])),
                grid_x=grid[0], grid_y=grid[1], grid_z=grid[2], grid_t=msg.t))
        self._true_publisher.publish(msg)

    def _on_servo_commands(self, msg):
        # device_manager と同じく、受けた順にモジュールへ送る
        self._world.advance(time.time())
        for command in msg.commands:
            try:
                packet = to_servo_packet(command)
            except ProtocolError as e:
                self.get_logger().warning(
                    f'Bad servo command to module ID={command.module_id}: {e}')
                continue
            if not self._world.send(command.module_id, packet):
                self.get_logger().warning(f'Unknown module ID={command.module_id}')

    def _on_set_grid_positions(self, request, response):
        positions = {p.id: (p.x, p.y, p.z) for p in request.positions}
        try:
            self._world.advance(time.time())
            self._world.set_grid_positions(positions)
        except ValueError as e:
            response.success = False
            response.message = str(e)
            return response
        response.success = True
        response.message = f'placed {len(positions)} modules'
        self.get_logger().info(f'Placed modules: {positions}')
        return response


def main(args=None):
    rclpy.init(args=args)

    try:
        module_sim = ModuleSim()
    except (OSError, ValueError) as e:
        print(f'module_sim: {e}')
        rclpy.try_shutdown()
        raise SystemExit(1)

    try:
        rclpy.spin(module_sim)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        module_sim.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
