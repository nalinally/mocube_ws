#include "IrNecReceiver.h"

IrNecReceiver* IrNecReceiver::_instance = nullptr;

IrNecReceiver::IrNecReceiver(const uint8_t (&pins)[CHANNEL_COUNT])
{
    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
        _pins[i] = pins[i];
    }
}

void IrNecReceiver::begin(MessageCallback callback)
{
    _callback = callback;
    _instance = this;

    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
        pinMode(_pins[i], INPUT);

        _channels[i].lastEdgeUs = micros();
        _channels[i].writeIndex = 0;
        _channels[i].readIndex = 0;
        resetDecoder(_channels[i]);
    }

    attachInterrupt(_pins[0], isr0, CHANGE);
    attachInterrupt(_pins[1], isr1, CHANGE);
    attachInterrupt(_pins[2], isr2, CHANGE);
}

void IRAM_ATTR IrNecReceiver::isr0()
{
    if (_instance != nullptr) {
        _instance->handleInterrupt(0);
    }
}

void IRAM_ATTR IrNecReceiver::isr1()
{
    if (_instance != nullptr) {
        _instance->handleInterrupt(1);
    }
}

void IRAM_ATTR IrNecReceiver::isr2()
{
    if (_instance != nullptr) {
        _instance->handleInterrupt(2);
    }
}

void IRAM_ATTR IrNecReceiver::handleInterrupt(uint8_t channel)
{
    Channel& ch = _channels[channel];

    const uint32_t now = micros();
    const uint32_t duration = now - ch.lastEdgeUs;
    ch.lastEdgeUs = now;

    const uint8_t nextWrite =
        static_cast<uint8_t>((ch.writeIndex + 1) % BUFFER_SIZE);

    // Buffer full: drop this edge rather than overwriting unread data.
    if (nextWrite == ch.readIndex) {
        return;
    }

    ch.buffer[ch.writeIndex].durationUs = duration;
    ch.buffer[ch.writeIndex].level =
        digitalRead(_pins[channel]);

    ch.writeIndex = nextWrite;
}

void IrNecReceiver::update()
{
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
        Channel& ch = _channels[channel];

        while (true) {
            Edge edge;
            bool available = false;

            noInterrupts();

            if (ch.readIndex != ch.writeIndex) {
                edge = ch.buffer[ch.readIndex];
                ch.readIndex =
                    static_cast<uint8_t>(
                        (ch.readIndex + 1) % BUFFER_SIZE
                    );
                available = true;
            }

            interrupts();

            if (!available) {
                break;
            }

            processEdge(
                channel,
                edge.level,
                edge.durationUs
            );
        }
    }
}

bool IrNecReceiver::inRange(
    uint32_t value,
    uint32_t minValue,
    uint32_t maxValue) const
{
    return value >= minValue && value <= maxValue;
}

void IrNecReceiver::resetDecoder(Channel& ch)
{
    ch.state = 0;
    ch.bitCount = 0;
    ch.data = 0;
}

void IrNecReceiver::processEdge(
    uint8_t channel,
    bool level,
    uint32_t durationUs)
{
    Channel& ch = _channels[channel];

    // An unusually long interval means that the previous frame
    // was interrupted or has already ended.
    if (durationUs > 15000) {
        resetDecoder(ch);
    }

    switch (ch.state) {

    // Idle -> leader mark
    case 0:
        if (level == LOW) {
            ch.state = 1;
        }
        break;

    // Leader mark: approximately 9 ms
    case 1:
        if (level == HIGH) {
            // Serial.print("leader_us: ");
            // Serial.println(durationUs);
            if (inRange(durationUs, 12 * 562, 20 * 562)) {
                ch.state = 2;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    // Leader space: approximately 4.5 ms
    case 2:
        if (level == LOW) {
            if (inRange(durationUs, 6 * 562, 10 * 562)) {
                ch.state = 3;
                ch.bitCount = 0;
                ch.data = 0;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    // Bit mark: approximately 562 us
    case 3:
        if (level == HIGH) {
            if (inRange(durationUs, 0, 2 * 562)) {
                ch.state = 4;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    // Bit space:
    // 0 -> approximately 562 us
    // 1 -> approximately 1687 us
    case 4:
        if (level == LOW) {
            if (inRange(durationUs, 0, 2 * 562)) {
                ch.data <<= 1;
                ch.bitCount++;
            }
            else if (inRange(durationUs, 2 * 562, 4 * 562)) {
                ch.data <<= 1;
                ch.data |= 1;
                ch.bitCount++;
            }
            else {
                resetDecoder(ch);
                break;
            }

            if (ch.bitCount == 32) {
                finishMessage(channel, ch);
                resetDecoder(ch);
            } else {
                ch.state = 3;
            }
        }
        break;

    default:
        resetDecoder(ch);
        break;
    }
}

void IrNecReceiver::finishMessage(
    uint8_t channel,
    Channel& decoder)
{
    const uint8_t address =
        decoder.data & 0xFF;

    const uint8_t addressInv =
        (decoder.data >> 8) & 0xFF;

    const uint8_t command =
        (decoder.data >> 16) & 0xFF;

    const uint8_t commandInv =
        (decoder.data >> 24) & 0xFF;

    if (
        static_cast<uint8_t>(address ^ addressInv) != 0xFF ||
        static_cast<uint8_t>(command ^ commandInv) != 0xFF
    ) {
        Serial.printf(
            "IR RX%d: INVALID DATA=0x%08lX\n",
            channel + 1,
            static_cast<unsigned long>(decoder.data)
        );
        return;
    }

    Message message {
        address,
        command,
        decoder.data
    };

    if (_callback != nullptr) {
        _callback(channel, message);
    } else {
        Serial.printf(
            "IR RX%d: ADDR=0x%02X CMD=0x%02X DATA=0x%08lX\n",
            channel + 1,
            message.address,
            message.command,
            static_cast<unsigned long>(message.raw)
        );
    }
}
