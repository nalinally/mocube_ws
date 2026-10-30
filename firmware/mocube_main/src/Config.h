#pragma once

#include <Arduino.h>

// ============================================================
// ハードウェア構成・タイミング・通信設定をすべてここに集約する。
// 配線やPC側の設定を変えたときはこのファイルだけを見ればよい。
// (Wi-Fi の SSID / パスワードは include/WifiSecrets.h)
// ============================================================

namespace config {

// ------------------------------------------------------------
// Pin configuration
// ------------------------------------------------------------
//
// AtomS3 exposes G5/G6/G7/G8/G38/G39 as bottom GPIOs.
// KRS (atoms3-ics-shield) uses G5/G6/G7, so IR must not use those pins.
//
// IR RX (index = チャンネル番号 = PC側の面 "u" / "r" / "f"):
//   ch0 = G8   -> "u"
//   ch1 = G38  -> "r"
//   ch2 = G39  -> "f"
//
// IR TX:
//   G1 (HY2.0-4P yellow/white-side GPIO)
//
// Indicator LED:
//   G2 (HY2.0-4P)
//
// KRS ICS (TX / RX / EN):
//   G5 / G6 / G7

constexpr uint8_t IR_RX_PINS[3] = {G8, G38, G39};
constexpr uint8_t IR_TX_PIN = G1;
constexpr uint8_t INDICATOR_PIN = G2;

constexpr uint8_t KRS_ICS_TX = G5;
constexpr uint8_t KRS_ICS_RX = G6;
constexpr uint8_t KRS_ICS_EN = G7;


// ------------------------------------------------------------
// KRS servo (ICS)
// ------------------------------------------------------------

constexpr long KRS_BAUDRATE = 115200;
constexpr int KRS_TIMEOUT_MS = 1000;
constexpr int KRS_SERVO_COUNT = 6;  // ICS ID 0..5
// setPos で受け付ける角度 [deg]。±135 が ICS のポジション 3500..11500 に当たる
// (回転モードのサーボでは回転の速さと向き。0 で停止)
constexpr float KRS_MAX_DEG = 135.0f;


// ------------------------------------------------------------
// Indicator LED (ID 表示)
// ------------------------------------------------------------

// ID + 1 回点滅してから ID_BLINK_GAP_MS 消灯、を繰り返す
constexpr unsigned long ID_BLINK_PERIOD_MS = 250;  // 点滅 1 回の周期 (前半 ON / 後半 OFF)
constexpr unsigned long ID_BLINK_GAP_MS = 1000;    // 点滅の区切りの消灯時間


// ------------------------------------------------------------
// IR
// ------------------------------------------------------------

constexpr uint32_t IR_CARRIER_HZ = 38000;

// 隣接モジュール検出用ビーコン: address = 自分のID, command = この値
constexpr uint8_t IR_BEACON_COMMAND = 0x00;
constexpr unsigned long IR_BEACON_INTERVAL_MS = 100;

// この時間内にビーコンを受けた面を「今も隣にいる」として画面で強調する
constexpr unsigned long IR_NEIGHBOR_FRESH_MS = 500;


// ------------------------------------------------------------
// UDP (PC 側 device_manager の port パラメータと合わせる)
// ------------------------------------------------------------

constexpr uint16_t UDP_PORT = 5000;


// ------------------------------------------------------------
// PC link timing
// ------------------------------------------------------------

constexpr unsigned long REGISTER_INTERVAL_MS = 2000;   // ID未取得の間だけ送る
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 1000;
constexpr unsigned long PC_TIMEOUT_MS = 3000;          // ACKがこれだけ来なければPC切断扱い
constexpr unsigned long WIFI_RETRY_DELAY_MS = 1000;    // Wi-Fi切断中のloop待ち

}  // namespace config
