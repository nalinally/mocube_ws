#pragma once

#include <Arduino.h>
#include <IcsHardSerialClass.h>

#include "Config.h"
#include "Protocol.h"

/**
 * ICS (Serial1) 経由で KRS サーボを動かし、最後に送った指令を覚えておく。
 * ここで覚えている状態は「送った指令」であって、サーボから読み返した値ではない。
 */
class ServoController {
public:
    enum class Mode {
        Free,      // 脱力 (setFree)
        Position,  // 角度指令中 (setPos)
    };

    ServoController();

    void begin();

    // setPosWd の指令が途絶えたサーボを止める。Wi-Fi が切れている間も呼ぶこと
    void update(unsigned long now);

    // deg: -KRS_MAX_DEG..KRS_MAX_DEG (0 が中央)。ICS のポジションへは degPos() で変換する
    bool setPosition(int servoId, float deg);
    // setPosition と同じだが、KRS_WATCHDOG_MS の間に更新が無ければ 0 にする。deg が 0 なら監視しない
    bool setPositionWithWatchdog(int servoId, float deg, unsigned long now);
    bool setFree(int servoId);
    void setFreeAll();

    // "KRS,setPos,<servo>,<deg>" / "KRS,setPosWd,<servo>,<deg>"
    // "KRS,setFree,<servo>" / "KRS,setFreeAll"
    void handleCommand(const protocol::Packet& packet);

    Mode mode(int servoId) const { return _modes[servoId]; }
    float position(int servoId) const { return _positions[servoId]; }  // [deg]

private:
    IcsHardSerialClass _krs;

    Mode _modes[config::KRS_SERVO_COUNT];
    float _positions[config::KRS_SERVO_COUNT];  // [deg]
    bool _watched[config::KRS_SERVO_COUNT];  // setPosWd の監視中か
    unsigned long _lastUpdate[config::KRS_SERVO_COUNT];  // 監視中の指令を最後に受けた時刻 [ms]

    static bool isValidId(int servoId);
};
