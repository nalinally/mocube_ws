#pragma once

#include <Arduino.h>

// ============================================================
// NEC 赤外線フォーマットの定数とフレームの組み立て/検証。
// 送信 (IrNecTransmitter) と受信 (IrNecReceiver) で共有する。
//
// 32bit フレーム (raw) のビット配置:
//   bit 31..24 : ~command
//   bit 23..16 :  command
//   bit 15..8  : ~address
//   bit  7..0  :  address
//
// 一般的な NEC と同じく raw を LSB (bit0) から順に送受信する。
// つまり address, ~address, command, ~command の順に、各バイトとも
// LSB first で送られる。
// ============================================================

namespace nec {

constexpr uint32_t UNIT_US = 562;

constexpr uint32_t LEADER_MARK_US = 9000;
constexpr uint32_t LEADER_SPACE_US = 4500;
constexpr uint32_t BIT_MARK_US = 562;
constexpr uint32_t ZERO_SPACE_US = 562;
constexpr uint32_t ONE_SPACE_US = 1687;

constexpr uint8_t FRAME_BITS = 32;

inline uint32_t encode(uint8_t address, uint8_t command)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(~command)) << 24) |
           (static_cast<uint32_t>(command) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(~address)) << 8) |
           static_cast<uint32_t>(address);
}

inline uint8_t addressOf(uint32_t raw) { return raw & 0xFF; }
inline uint8_t commandOf(uint32_t raw) { return (raw >> 16) & 0xFF; }

// address / command とその反転が整合しているか
inline bool isValid(uint32_t raw)
{
    const uint8_t address = raw & 0xFF;
    const uint8_t addressInv = (raw >> 8) & 0xFF;
    const uint8_t command = (raw >> 16) & 0xFF;
    const uint8_t commandInv = (raw >> 24) & 0xFF;

    return static_cast<uint8_t>(address ^ addressInv) == 0xFF &&
           static_cast<uint8_t>(command ^ commandInv) == 0xFF;
}

}  // namespace nec
