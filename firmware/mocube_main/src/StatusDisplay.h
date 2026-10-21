#pragma once

#include <Arduino.h>

#include "PcLink.h"
#include "ServoController.h"

/**
 * AtomS3 の LCD (128x128) への状態表示。
 * draw() は毎回全画面を塗り直す (呼ぶたびに一瞬ちらつく)。
 */
class StatusDisplay {
public:
    // Wi-Fi 接続待ち画面
    void showConnecting();
    void showConnectingProgress();

    // ID / Wi-Fi / PC 接続 / IP / サーボ状態
    void draw(const PcLink& link, const ServoController& servos);

private:
    void drawStatusDot(int x, int y, bool state);
};
