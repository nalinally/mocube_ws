#pragma once

#include <Arduino.h>
#include <M5GFX.h>

#include "Config.h"
#include "NeighborDetector.h"
#include "PcLink.h"
#include "ServoController.h"

/**
 * AtomS3 の LCD (128x128) への状態表示。
 *
 *  ┌────────────────────────┐
 *  │            ┌─ WiFi ─┐  │  ID は 7 セグ風の大きな数字 (未取得なら --)
 *  │ 03         └────────┘  │  WiFi / PC は 緑 = OK / 赤 = NG のバッジ
 *  │            ┌── PC ──┐  │
 *  │            └────────┘  │
 *  │     192.168.97.12      │  IP (Wi-Fi 切断中は "No WiFi")
 *  │ [u:05] [r:--] [f:02]   │  赤外線 ch0..2 (面 u/r/f) で最後に受けたビーコンの送信元 ID
 *  │────────────────────────│    緑 = 今も受信中 / 灰 = 途切れた / 枠だけ = 未受信
 *  │0  FREE      1     +45  │  サーボ 0..5 に最後に送った指令
 *  │2  FREE      3    -135  │  (FREE = 脱力 / 数字 = 角度 [deg])
 *  │4  FREE      5       0  │
 *  └────────────────────────┘
 *
 * 画面外のバッファ (M5Canvas) に描いてから一度に転送するのでちらつかない。
 * update() を毎 loop 呼ぶと、表示内容が変わったときだけ描き直す。
 */
class StatusDisplay {
public:
    // M5.begin() の後に呼ぶ
    void begin();

    // Wi-Fi 接続待ち画面
    void showConnecting();
    void showConnectingProgress();

    // 表示内容が変わっていれば描き直す (最短 DRAW_INTERVAL_MS 間隔)
    void update(
        const PcLink& link,
        const ServoController& servos,
        const NeighborDetector& neighbors,
        unsigned long now);

private:
    // 赤外線 1 チャンネル分の表示状態
    struct IrState {
        int address = -1;    // -1 = 未受信
        bool fresh = false;  // IR_NEIGHBOR_FRESH_MS 以内に受けた

        bool operator==(const IrState& other) const;
    };

    // 表示に使う値だけを集めたもの。前回と同じなら描き直さない
    struct Snapshot {
        int id = -1;
        bool wifi = false;
        bool pc = false;
        uint32_t ip = 0;
        IrState ir[NeighborDetector::CHANNEL_COUNT];
        bool free[config::KRS_SERVO_COUNT] = {};
        long deg[config::KRS_SERVO_COUNT] = {};  // 表示する整数に丸めた角度

        bool operator==(const Snapshot& other) const;
    };

    // 描画は数 ms かかるので、指令が連続で来ても詰まらないよう間隔を空ける
    static constexpr unsigned long DRAW_INTERVAL_MS = 100;

    M5Canvas _canvas;
    bool _hasCanvas = false;

    Snapshot _shown;
    bool _drawn = false;
    unsigned long _lastDraw = 0;

    static Snapshot capture(
        const PcLink& link,
        const ServoController& servos,
        const NeighborDetector& neighbors,
        unsigned long now);
    void draw(lgfx::LovyanGFX& gfx, const Snapshot& s);
    void drawId(lgfx::LovyanGFX& gfx, int id);
    void drawBadge(lgfx::LovyanGFX& gfx, int y, const char* label, bool ok);
    void drawIr(lgfx::LovyanGFX& gfx, int channel, const IrState& ir);
    void drawServo(lgfx::LovyanGFX& gfx, int servoId, bool free, long deg);
};
