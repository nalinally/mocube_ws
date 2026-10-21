#pragma once

#include <Arduino.h>

#include "IrNecReceiver.h"
#include "IrNecTransmitter.h"
#include "PcLink.h"
#include "Protocol.h"

/**
 * 赤外線による隣接モジュール検出。
 *
 * - 送信: 自分の ID を address にした NEC ビーコンを周期送信する
 *         (ID 未取得の間は -1 = 0xFF が送られる)
 * - 受信: 3ch の受信器のどれかでビーコンを受けたら
 *         "NEIGHBOR,<MAC>,<ch>,<相手のID>" を PC へブロードキャストする
 *
 * PC -> モジュールの "IR,send,<raw>" で任意フレームの送信もできる。
 */
class NeighborDetector {
public:
    explicit NeighborDetector(PcLink& link);

    void begin();

    // 受信エッジのデコード。ビーコン受信時はここから NEIGHBOR を送る。
    // Wi-Fi の状態に関係なく毎 loop 呼ぶ (呼ばないとリングバッファが溢れる)
    void updateReceiver();

    // ビーコン送信 (送信周期は config::IR_BEACON_INTERVAL_MS)
    void updateBeacon(unsigned long now);

    // "IR,send,<raw>"  raw は 0x 付き16進でも10進でもよい
    void handleCommand(const protocol::Packet& packet);

private:
    PcLink& _link;
    IrNecReceiver _receiver;
    IrNecTransmitter _transmitter;
    unsigned long _lastBeacon = 0;

    void onMessage(uint8_t channel, const IrNecReceiver::Message& message);
};
