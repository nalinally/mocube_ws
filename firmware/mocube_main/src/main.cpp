// ============================================================
// mocube モジュール (M5Stack AtomS3) のエントリポイント。
//
// 各機能はクラスに分かれていて、ここでは生成・初期化・loop の呼び出し順と
// PC からのコマンドの振り分けだけを書く。
//
//   PcLink           Wi-Fi / UDP / ID登録 / heartbeat / コマンド振り分け
//   ServoController  KRS サーボ (ICS)
//   NeighborDetector 赤外線ビーコンによる隣接モジュール検出
//   StatusDisplay    LCD 表示
//
// 新しい機能を足すときの基本形:
//   1. begin() / update(now) / handleCommand(packet) を持つクラスを作る
//   2. 下でグローバルに生成し、setup() で begin()、loop() で update()
//   3. PC から操作するなら pcLink.onCommand("CMD", ...) で登録する
//   4. Protocol.h とワークスペース README のプロトコル表に追記する
// ============================================================

#include <Arduino.h>
#include <M5AtomS3.h>

#include "Config.h"
#include "NeighborDetector.h"
#include "PcLink.h"
#include "Protocol.h"
#include "ServoController.h"
#include "StatusDisplay.h"

#if __has_include("WifiSecrets.h")
#include "WifiSecrets.h"
#else
#error "include/WifiSecrets.h がありません。include/WifiSecrets.example.h をコピーして作成してください"
#endif


PcLink pcLink;
ServoController servos;
NeighborDetector neighbors(pcLink);
StatusDisplay display;


void setup()
{
    auto cfg = M5.config();

    M5.begin(cfg);

    Serial.begin(115200);

    delay(500);

    display.showConnecting();
    pcLink.connectWiFi(WIFI_SSID, WIFI_PASSWORD, [] {
        display.showConnectingProgress();
    });

    display.draw(pcLink, servos);

    servos.begin();
    neighbors.begin();

    pcLink.onCommand(protocol::command::KRS, [](const protocol::Packet& packet) {
        servos.handleCommand(packet);
    });
    pcLink.onCommand(protocol::command::IR, [](const protocol::Packet& packet) {
        neighbors.handleCommand(packet);
    });

    pcLink.begin(config::UDP_PORT);
}


void loop()
{
    M5.update();

    // ISR は常時エッジを記録し、ここで NEC をデコードする。
    // Wi-Fi 切断中も止めない (止めるとリングバッファが溢れる)。
    neighbors.updateReceiver();

    if (!pcLink.isWiFiConnected()) {

        pcLink.handleWiFiLost();
        display.draw(pcLink, servos);

        delay(config::WIFI_RETRY_DELAY_MS);
        return;
    }

    const unsigned long now = millis();

    if (pcLink.update(now)) {
        display.draw(pcLink, servos);
    }

    neighbors.updateBeacon(now);

    // No delay is required for IR reception.
    // The receiver uses a ring buffer, so the loop can perform
    // other work without immediately losing an edge.
}
