"""
シミュレータの本体: モジュールの本当の位置、ファーム、赤外線、device_manager の台帳.

ROS に依存しないので単体テストできる。ノードは module_sim.py。

- 動き: サーボの回転指令から、位置推定 (mocube_estimator) と同じ理想モデルで本当の位置を進める。
  速度係数 speed_gain は推定とは別に与える (推定の値とずらして行き過ぎや手前止まりを試せる)
- 赤外線: 各モジュールが beacon_period ごとに自分の ID を送る。向かい合う面の中心の
  面内のずれが ir_range (一辺に対する割合) 以内なら、負の側のモジュールの u / r / f 面が受ける
- 台帳: device_manager と同じく、受けた時刻を受信器ごとに上書きするだけで消さない。
  HEARTBEAT は heartbeat_period ごとに届き、timeout 秒途絶えると offline
"""

from dataclasses import dataclass, field
import json

from mocube_estimator.estimator import Estimator, parse_grid_positions
from mocube_estimator.geometry import FACES, POSITIVE_FACE
from mocube_sim.firmware import ServoFirmware, WATCHDOG_S
import numpy as np
import yaml

RECEIVER_FACES = FACES[:3]  # u, r, f (受信チャンネル 0, 1, 2)
NO_NEIGHBOR = -1            # mocube_bridge の device_registry と同じ


def make_mac(module_id):
    """シミュレータのモジュールの MAC (ローカル管理アドレス). 実機と重ならない."""
    return f'02:00:00:00:{(module_id >> 8) & 0xFF:02X}:{module_id & 0xFF:02X}'


def load_layout(path):
    """
    初期配置を読む. {ID: (x, y, z)} を返す.

    YAML の {ID: [x, y, z]} (state_estimator の initial_positions_file と同じ形)、
    または plan で作った計画の JSON (initial を使う)。
    """
    with open(path, encoding='utf-8') as f:
        if path.endswith('.json'):
            data = json.load(f)
            if isinstance(data, dict) and 'initial' in data:
                data = data['initial']
        else:
            data = yaml.safe_load(f)
    return parse_grid_positions(data)


@dataclass
class SimModule:
    """1 モジュール分の、モジュール側と device_manager 側の状態."""

    id: int  # noqa: A003  (Module.msg に合わせる)
    mac: str
    firmware: ServoFirmware
    next_heartbeat: float
    next_beacon: float
    last_seen: float
    online: bool = False
    # face -> [相手の address, 受信時刻]。device_registry と同じく一度入った値は消えない
    receivers: dict = field(
        default_factory=lambda: {face: [NO_NEIGHBOR, 0.0] for face in RECEIVER_FACES})


class World:

    def __init__(self, layout, edge_length, speed_gain, start,
                 ir_range=0.1, contact_tolerance=0.1, beacon_period=0.1,
                 heartbeat_period=1.0, timeout=3.0, watchdog=WATCHDOG_S):
        """
        パラメータ.

        layout: {ID: (x, y, z)} の初期の格子座標
        edge_length: 一辺 [m]
        speed_gain: 回転指令 1 deg あたりの本当の相対移動速度 [m/s]
        start: 開始時刻 [s] (UNIX time)
        """
        self.edge_length = edge_length
        self.ir_range = ir_range
        self.beacon_period = beacon_period
        self.heartbeat_period = heartbeat_period
        self.timeout = timeout
        self.time = start

        # 動きの計算は推定と同じ理想モデル。物理側のモジュールは常に電源が入っている
        self._physics = Estimator(edge_length, speed_gain, 0.0, contact_tolerance)
        self._physics.update_modules([(module_id, True) for module_id in layout])
        self._physics.set_grid_positions(layout, start)

        self.modules = {}
        for k, module_id in enumerate(sorted(layout)):
            # 実機と同じく、送る時刻はモジュールごとにずれている
            self.modules[module_id] = SimModule(
                id=module_id,
                mac=make_mac(module_id),
                firmware=ServoFirmware(watchdog),
                next_heartbeat=start + (0.137 * k) % heartbeat_period,
                next_beacon=start + (0.0137 * k) % beacon_period,
                last_seen=start,
            )

    # ------------------------------------------------------------
    # 入力
    # ------------------------------------------------------------

    def send(self, module_id, packet):
        """PC からモジュールへパケットを送る. そのモジュールがいなければ False."""
        module = self.modules.get(module_id)
        if module is None:
            return False
        module.firmware.handle_packet(packet, self.time)
        return True

    def set_grid_positions(self, positions):
        """{ID: (x, y, z)} のモジュールを格子点へ置き直す (サーボの指令はそのまま)."""
        unknown = [module_id for module_id in positions if module_id not in self.modules]
        if unknown:
            raise ValueError(f'unknown module ID: {unknown}')
        self._physics.set_grid_positions(positions, self.time)

    # ------------------------------------------------------------
    # 時間を進める
    # ------------------------------------------------------------

    def advance(self, now):
        """時刻 now [s] まで進める."""
        dt = now - self.time
        if dt <= 0.0:
            return
        for module in self.modules.values():
            module.firmware.update(now)
            self._physics.modules[module.id].commands = list(module.firmware.angles)
        self._physics.step(dt)
        self.time = now

        for module in self.modules.values():
            if module.next_heartbeat <= now:
                module.last_seen = now
                module.online = True
                module.next_heartbeat = self._next(
                    module.next_heartbeat, self.heartbeat_period, now)
            if now - module.last_seen > self.timeout:
                module.online = False

        senders = set()
        for module in self.modules.values():
            if module.next_beacon <= now:
                senders.add(module.id)
                module.next_beacon = self._next(module.next_beacon, self.beacon_period, now)
        if senders:
            self._receive_beacons(senders, now)

    @staticmethod
    def _next(scheduled, period, now):
        while scheduled <= now:
            scheduled += period
        return scheduled

    def _receive_beacons(self, senders, now):
        """送信した senders のビーコンを、向かい合っている負の側の受信器が受ける."""
        positions = self.positions()
        limit = self.ir_range * self.edge_length
        for contact in self._physics.find_contacts():
            if contact.upper not in senders:
                continue
            offset = positions[contact.upper] - positions[contact.lower]
            others = [k for k in range(3) if k != contact.axis]
            if (abs(offset[contact.axis] - self.edge_length) <= limit
                    and all(abs(offset[k]) <= limit for k in others)):
                face = FACES[POSITIVE_FACE[contact.axis]]
                # ビーコンの address は uint8
                self.modules[contact.lower].receivers[face] = [contact.upper & 0xFF, now]

    # ------------------------------------------------------------
    # 出力
    # ------------------------------------------------------------

    def positions(self):
        """{ID: 本当の位置 [m]}."""
        return {i: m.position.copy() for i, m in self._physics.modules.items()}

    def velocities(self):
        """{ID: 本当の速度 [m/s]}."""
        return self._physics.velocities()

    def grid_positions(self):
        """{ID: 最も近い格子点}."""
        return {
            i: tuple(int(v) for v in np.rint(p / self.edge_length))
            for i, p in self.positions().items()}
