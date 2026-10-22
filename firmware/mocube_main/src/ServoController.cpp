#include "ServoController.h"

ServoController::ServoController()
    : _krs(
          &Serial1,
          config::KRS_ICS_EN,
          config::KRS_BAUDRATE,
          config::KRS_TIMEOUT_MS)
{
    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        _modes[i] = Mode::Free;
        _positions[i] = 0.0f;
    }
}

void ServoController::begin()
{
    // 先にピンを指定して Serial1 を開く。krs.begin() の中でも
    // Serial1.begin(baud, SERIAL_8E1) が (ピン指定なしで) 呼ばれる。
    Serial1.begin(
        config::KRS_BAUDRATE,
        SERIAL_8E1,
        config::KRS_ICS_RX,
        config::KRS_ICS_TX
    );

    _krs.begin();
}

bool ServoController::isValidId(int servoId)
{
    return servoId >= 0 && servoId < config::KRS_SERVO_COUNT;
}

bool ServoController::setPosition(int servoId, float deg)
{
    // degPos() は ±180 まで変換してしまうので、KRS の範囲に絞る
    if (!isValidId(servoId) || fabsf(deg) > config::KRS_MAX_DEG) {
        return false;
    }

    _krs.setPos(servoId, IcsBaseClass::degPos(deg));
    _modes[servoId] = Mode::Position;
    _positions[servoId] = deg;
    return true;
}

bool ServoController::setFree(int servoId)
{
    if (!isValidId(servoId)) {
        return false;
    }

    _krs.setFree(servoId);
    _modes[servoId] = Mode::Free;
    return true;
}

void ServoController::setFreeAll()
{
    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        setFree(i);
    }
}

void ServoController::handleCommand(const protocol::Packet& packet)
{
    const String& cmd = packet.at(1);

    if (cmd == "setPos" && packet.count >= 4) {
        setPosition(packet.at(2).toInt(), packet.at(3).toFloat());
    }
    else if (cmd == "setFree" && packet.count >= 3) {
        setFree(packet.at(2).toInt());
    }
    else if (cmd == "setFreeAll") {
        setFreeAll();
    }
}
