#pragma once

#include <Arduino.h>

// ============================================================
// PC (device_manager) とのUDPテキストプロトコル。
// 1パケット = "COMMAND,arg1,arg2,..." の1行 (カンマ区切り)。
// 仕様の一覧はワークスペース直下の README.md を参照。
//
// モジュール -> PC (ブロードキャスト):
//   REGISTER,<MAC>
//   HEARTBEAT,<MAC>
//   NEIGHBOR,<MAC>,<ch>,<address>
//
// PC -> モジュール (ユニキャスト):
//   ID,<id>                    REGISTER への応答
//   ACK,<id>                   HEARTBEAT への応答
//   KRS,setPos,<servo>,<pos>   (ServoController)
//   KRS,setFree,<servo>        (ServoController)
//   IR,send,<raw>              (NeighborDetector)
// ============================================================

namespace protocol {

namespace command {
constexpr const char* REGISTER = "REGISTER";
constexpr const char* HEARTBEAT = "HEARTBEAT";
constexpr const char* NEIGHBOR = "NEIGHBOR";
constexpr const char* ID = "ID";
constexpr const char* ACK = "ACK";
constexpr const char* KRS = "KRS";
constexpr const char* IR = "IR";
}  // namespace command

// 受信パケットをカンマで分割したもの
struct Packet {
    static constexpr int MAX_FIELDS = 8;

    String fields[MAX_FIELDS];
    int count = 0;  // 有効なフィールド数 (MAX_FIELDS で頭打ち)

    const String& command() const { return fields[0]; }

    // index 番目のフィールド。存在しなければ空文字列
    const String& at(int index) const;
};

// text を Packet に分割する。MAX_FIELDS を超えるフィールドは捨てる。
void parse(const String& text, Packet& out);

String makeRegister(const String& mac);
String makeHeartbeat(const String& mac);
String makeNeighbor(const String& mac, uint8_t channel, uint8_t address);

}  // namespace protocol
