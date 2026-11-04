"""
モジュールのファームのうち、サーボの指令を受けるところ (ServoController) の写し.

firmware/mocube_main/src/ServoController.cpp と同じ規則で KRS パケットを解釈する。
ここで持つのは「サーボに送った指令」で、回転モードのサーボでは角度が回転の速さと向きになる。
ROS に依存しないので単体テストできる。
"""

SERVO_COUNT = 6          # Config.h の KRS_SERVO_COUNT (ICS ID 0..5)
SERVO_MAX_DEG = 135.0    # Config.h の KRS_MAX_DEG
WATCHDOG_S = 1.0         # Config.h の KRS_WATCHDOG_MS


def _to_int(text):
    """Arduino の String::toInt と同じく、読めなければ 0 にする."""
    try:
        return int(text.strip())
    except ValueError:
        return 0


def _to_float(text):
    """Arduino の String::toFloat と同じく、読めなければ 0 にする."""
    try:
        return float(text.strip())
    except ValueError:
        return 0.0


class ServoFirmware:
    """1 モジュール分のサーボ指令の状態."""

    def __init__(self, watchdog=WATCHDOG_S):
        self.watchdog = watchdog
        # None は脱力 (setFree)。電源投入直後も脱力
        self.angles = [None] * SERVO_COUNT
        self._watched = [False] * SERVO_COUNT
        self._last_update = [0.0] * SERVO_COUNT

    def handle_packet(self, data, now):
        """
        PC からのパケット (bytes) を受ける. KRS 以外や形のおかしいものは黙って捨てる.

        now: 受けた時刻 [s] (ウォッチドッグに使う)
        """
        fields = data.decode('utf-8', errors='ignore').strip().split(',')
        if fields[0] != 'KRS' or len(fields) < 2:
            return
        command = fields[1]
        if command == 'setPos' and len(fields) >= 4:
            self.set_position(_to_int(fields[2]), _to_float(fields[3]))
        elif command == 'setPosWd' and len(fields) >= 4:
            self.set_position_with_watchdog(_to_int(fields[2]), _to_float(fields[3]), now)
        elif command == 'setFree' and len(fields) >= 3:
            self.set_free(_to_int(fields[2]))
        elif command == 'setFreeAll':
            for servo in range(SERVO_COUNT):
                self.set_free(servo)

    def update(self, now):
        """ウォッチドッグ: setPosWd の更新が watchdog 秒途絶えたサーボを 0 にする."""
        for servo in range(SERVO_COUNT):
            if self._watched[servo] and now - self._last_update[servo] >= self.watchdog:
                self.set_position(servo, 0.0)

    def set_position(self, servo, deg):
        if not 0 <= servo < SERVO_COUNT or not abs(deg) <= SERVO_MAX_DEG:
            return False
        self._watched[servo] = False
        self.angles[servo] = deg
        return True

    def set_position_with_watchdog(self, servo, deg, now):
        if not self.set_position(servo, deg):
            return False
        self._watched[servo] = deg != 0.0
        self._last_update[servo] = now
        return True

    def set_free(self, servo):
        if not 0 <= servo < SERVO_COUNT:
            return False
        self._watched[servo] = False
        self.angles[servo] = None
        return True
