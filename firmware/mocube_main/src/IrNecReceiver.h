#pragma once

#include <Arduino.h>
#include <functional>

/**
 * 3ch NEC infrared receiver.
 *
 * Each channel captures GPIO edges in an ISR and stores them in a
 * small ring buffer. NEC decoding is performed from update() (loop),
 * not ISR. update() を長時間呼ばないとリングバッファが溢れ、
 * 溢れたエッジは捨てられる (そのフレームはデコード失敗になる)。
 *
 * 受信モジュール (TSOP系) の出力は idle = HIGH, mark (搬送波あり) = LOW。
 */
class IrNecReceiver {
public:
    static constexpr size_t CHANNEL_COUNT = 3;

    struct Message {
        uint8_t address;
        uint8_t command;
        uint32_t raw;
    };

    using MessageCallback =
        std::function<void(uint8_t channel, const Message& message)>;

    explicit IrNecReceiver(const uint8_t (&pins)[CHANNEL_COUNT]);

    void begin(MessageCallback callback = nullptr);
    void update();

private:
    struct Edge {
        uint32_t durationUs;  // 直前のエッジからこのエッジまでの時間
        bool level;           // このエッジ直後のレベル
    };

    // 1 フレームは 68 エッジ (leader 開始 1 + leader 2 + 32bit x 2 + final 1)。
    // 3 フレーム分以上 (約 200 ms 以上 update() が止まっても) 溜められるようにする
    static constexpr size_t BUFFER_SIZE = 256;
    static_assert(BUFFER_SIZE - 1 >= 3 * 68, "IR buffer must hold 3 frames");

    enum class DecoderState : uint8_t {
        Idle,
        LeaderMark,   // 9ms mark の終わりを待つ
        LeaderSpace,  // 4.5ms space の終わりを待つ
        BitMark,      // 562us mark の終わりを待つ
        BitSpace,     // 562us(0) / 1687us(1) space の終わりを待つ
    };

    struct Channel {
        volatile uint32_t lastEdgeUs = 0;
        volatile uint16_t writeIndex = 0;
        volatile uint16_t readIndex = 0;
        Edge buffer[BUFFER_SIZE];

        DecoderState state = DecoderState::Idle;
        uint8_t bitCount = 0;
        uint32_t data = 0;
    };

    uint8_t _pins[CHANNEL_COUNT];
    Channel _channels[CHANNEL_COUNT];
    MessageCallback _callback;

    // ISR から参照するため唯一のインスタンスを保持する (複数生成は不可)
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
