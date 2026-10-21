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
        Position,  // 位置指令中 (setPos)
    };

    ServoController();

    void begin();

    bool setPosition(int servoId, int position);
    bool setFree(int servoId);

    // "KRS,setPos,<servo>,<pos>" / "KRS,setFree,<servo>"
    void handleCommand(const protocol::Packet& packet);

    Mode mode(int servoId) const { return _modes[servoId]; }
    int position(int servoId) const { return _positions[servoId]; }

private:
    IcsHardSerialClass _krs;

    Mode _modes[config::KRS_SERVO_COUNT];
    int _positions[config::KRS_SERVO_COUNT];

    static bool isValidId(int servoId);
};
