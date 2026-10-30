#include "IrNecReceiver.h"

#include "NecFormat.h"

namespace {

// デコード判定の許容範囲 (nec::UNIT_US 単位)
constexpr uint32_t U = nec::UNIT_US;

constexpr uint32_t LEADER_MARK_MIN_US = 12 * U;
constexpr uint32_t LEADER_MARK_MAX_US = 20 * U;
constexpr uint32_t LEADER_SPACE_MIN_US = 6 * U;
constexpr uint32_t LEADER_SPACE_MAX_US = 10 * U;
constexpr uint32_t BIT_MARK_MAX_US = 2 * U;
constexpr uint32_t ZERO_SPACE_MAX_US = 2 * U;
constexpr uint32_t ONE_SPACE_MAX_US = 4 * U;

// これより長い間隔があれば前のフレームは途切れた/終わったとみなす
constexpr uint32_t FRAME_GAP_US = 15000;

}  // namespace

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

    const uint16_t nextWrite =
        static_cast<uint16_t>((ch.writeIndex + 1) % BUFFER_SIZE);

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
                    static_cast<uint16_t>(
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
    ch.state = DecoderState::Idle;
    ch.bitCount = 0;
    ch.data = 0;
}

// level は「このエッジ直後のレベル」、durationUs は「直前の区間の長さ」。
// つまり level == HIGH のエッジでは直前の mark (LOW) の長さが、
// level == LOW のエッジでは直前の space (HIGH) の長さが分かる。
void IrNecReceiver::processEdge(
    uint8_t channel,
    bool level,
    uint32_t durationUs)
{
    Channel& ch = _channels[channel];

    if (durationUs > FRAME_GAP_US) {
        resetDecoder(ch);
    }

    switch (ch.state) {

    case DecoderState::Idle:
        if (level == LOW) {
            ch.state = DecoderState::LeaderMark;
        }
        break;

    case DecoderState::LeaderMark:
        if (level == HIGH) {
            if (inRange(durationUs, LEADER_MARK_MIN_US, LEADER_MARK_MAX_US)) {
                ch.state = DecoderState::LeaderSpace;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    case DecoderState::LeaderSpace:
        if (level == LOW) {
            if (inRange(durationUs, LEADER_SPACE_MIN_US, LEADER_SPACE_MAX_US)) {
                ch.state = DecoderState::BitMark;
                ch.bitCount = 0;
                ch.data = 0;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    case DecoderState::BitMark:
        if (level == HIGH) {
            if (inRange(durationUs, 0, BIT_MARK_MAX_US)) {
                ch.state = DecoderState::BitSpace;
            } else {
                resetDecoder(ch);
            }
        }
        break;

    case DecoderState::BitSpace:
        if (level == LOW) {
            // LSB first で詰める (NecFormat.h 参照)
            if (inRange(durationUs, 0, ZERO_SPACE_MAX_US)) {
                ch.bitCount++;
            }
            else if (inRange(durationUs, ZERO_SPACE_MAX_US, ONE_SPACE_MAX_US)) {
                ch.data |= 1UL << ch.bitCount;
                ch.bitCount++;
            }
            else {
                resetDecoder(ch);
                break;
            }

            if (ch.bitCount == nec::FRAME_BITS) {
                finishMessage(channel, ch);
                resetDecoder(ch);
            } else {
                ch.state = DecoderState::BitMark;
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
    if (!nec::isValid(decoder.data)) {
        Serial.printf(
            "IR RX%d: INVALID DATA=0x%08lX\n",
            channel + 1,
            static_cast<unsigned long>(decoder.data)
        );
        return;
    }

    Message message {
        nec::addressOf(decoder.data),
        nec::commandOf(decoder.data),
        decoder.data
    };

    if (_callback) {
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
