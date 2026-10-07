#pragma once

#include <Arduino.h>

class IrNecTransmitter {
public:
    explicit IrNecTransmitter(uint8_t pin);

    void begin(uint32_t carrierHz = 38000);

    bool send(uint32_t rawNec);

    bool send(uint8_t id, uint8_t cmd);

private:
    uint8_t _pin;
    uint32_t _carrierHz;
    bool _ready = false;
};