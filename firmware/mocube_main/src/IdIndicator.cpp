#include "IdIndicator.h"

#include "Config.h"


void IdIndicator::begin()
{
    pinMode(config::INDICATOR_PIN, OUTPUT);
    _ledOn = true;  // 確実に LOW を書くため
    setLed(false);
}


void IdIndicator::update(int id, unsigned long now)
{
    if (!_running || id != _id) {
        _id = id;
        _start = now;
        _running = true;
    }

    const unsigned long blinks = _id < 0 ? 0 : _id + 1;
    if (blinks == 0) {
        setLed(false);
        return;
    }

    const unsigned long blinkMs = blinks * config::ID_BLINK_PERIOD_MS;
    const unsigned long t = (now - _start) % (blinkMs + config::ID_BLINK_GAP_MS);

    setLed(t < blinkMs && t % config::ID_BLINK_PERIOD_MS < config::ID_BLINK_PERIOD_MS / 2);
}


void IdIndicator::stop()
{
    _running = false;
    setLed(false);
}


void IdIndicator::setLed(bool on)
{
    if (on == _ledOn) {
        return;
    }
    _ledOn = on;
    digitalWrite(config::INDICATOR_PIN, on ? HIGH : LOW);
}
