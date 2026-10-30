#pragma once

#include <Arduino.h>

/**
 * インジケータ LED (config::INDICATOR_PIN) で自分の ID を知らせる。
 *
 * ID + 1 回点滅 (1 回 = ID_BLINK_PERIOD_MS, 前半 ON / 後半 OFF) してから
 * ID_BLINK_GAP_MS 消灯し、これを繰り返す。
 * ID 未取得 (-1) なら消灯のまま。ID が変わったらその場で数え直す。
 */
class IdIndicator {
public:
    void begin();

    void update(int id, unsigned long now);

    // 消灯して止める。次の update() で最初から数え直す
    void stop();

private:
    int _id = -1;
    bool _running = false;
    unsigned long _start = 0;  // 点滅パターンの開始時刻 millis()
    bool _ledOn = false;

    void setLed(bool on);
};
