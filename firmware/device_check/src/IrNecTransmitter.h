#pragma once

#include <Arduino.h>

/**
 * NEC infrared transmitter using the ESP32 RMT peripheral (RMT_CHANNEL_0).
 * send() はノンブロッキング (送信はハードウェアが行う)。
 */
class IrNecTransmitter {
public:
    explicit IrNecTransmitter(uint8_t pin);

    void begin(uint32_t carrierHz = 38000);

    // 32bit フレームをそのまま送る (ビット配置は NecFormat.h)
    bool send(uint32_t rawNec);

    // address / command から反転ビット付きのフレームを組んで送る
    bool send(uint8_t address, uint8_t command);

private:
    uint8_t _pin;
    uint32_t _carrierHz;
    bool _ready = false;
};
