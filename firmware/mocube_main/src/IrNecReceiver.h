#pragma once

#include <Arduino.h>

/**
 * 3ch NEC infrared receiver.
 *
 * Each channel captures GPIO edges in an ISR and stores them in a
 * small ring buffer. NEC decoding is performed from loop(), not ISR.
 */
class IrNecReceiver {
public:
    static constexpr size_t CHANNEL_COUNT = 3;

    struct Message {
        uint8_t address;
        uint8_t command;
        uint32_t raw;
    };

    using MessageCallback = void (*)(uint8_t channel, const Message& message);

    IrNecReceiver(const uint8_t (&pins)[CHANNEL_COUNT]);

    void begin(MessageCallback callback = nullptr);
    void update();

private:
    struct Edge {
        uint32_t durationUs;
        bool level;
    };

    static constexpr size_t BUFFER_SIZE = 64;

    struct Channel {
        volatile uint32_t lastEdgeUs = 0;
        volatile uint8_t writeIndex = 0;
        volatile uint8_t readIndex = 0;
        Edge buffer[BUFFER_SIZE];

        uint8_t state = 0;
        uint8_t bitCount = 0;
        uint32_t data = 0;
    };

    uint8_t _pins[CHANNEL_COUNT];
    Channel _channels[CHANNEL_COUNT];
    MessageCallback _callback = nullptr;

    static IrNecReceiver* _instance;

    static void IRAM_ATTR isr0();
    static void IRAM_ATTR isr1();
    static void IRAM_ATTR isr2();

    void IRAM_ATTR handleInterrupt(uint8_t channel);

    void processEdge(uint8_t channel, bool level, uint32_t durationUs);
    void resetDecoder(Channel& channel);
    bool inRange(uint32_t value, uint32_t minValue, uint32_t maxValue) const;
    void finishMessage(uint8_t channel, Channel& decoder);
};
