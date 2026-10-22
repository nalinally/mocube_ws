#include "StatusDisplay.h"

#include <M5AtomS3.h>

namespace {

// RGB565
constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GRAY = 0x8410;
constexpr uint16_t COLOR_DARK_GRAY = 0x3186;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_CYAN = 0x07FF;

// レイアウト (128x128)
constexpr int SCREEN_SIZE = 128;
constexpr int ID_Y = 2;
constexpr int BADGE_X = 72;
constexpr int BADGE_W = 54;
constexpr int BADGE_H = 20;
constexpr int WIFI_BADGE_Y = 3;
constexpr int PC_BADGE_Y = 27;
constexpr int IP_Y = 52;
constexpr int IR_Y = 63;
constexpr int IR_W = 40;
constexpr int IR_H = 16;
constexpr int IR_PITCH = 43;
constexpr int SEPARATOR_Y = 81;
constexpr int SERVO_Y = 83;
constexpr int SERVO_ROW_H = 15;
constexpr int SERVO_COL_W = 64;

// 赤外線チャンネル番号 -> 面の名前 (PC 側 protocol.FACES と同じ)
constexpr const char* FACE_NAMES[NeighborDetector::CHANNEL_COUNT] = {"u", "r", "f"};

// ビーコンの address がこの値なら、送信元はまだ ID を持っていない (ID -1 が uint8 になったもの)
constexpr int NO_ID_ADDRESS = 255;

}  // namespace

bool StatusDisplay::IrState::operator==(const IrState& other) const
{
    return address == other.address && fresh == other.fresh;
}

bool StatusDisplay::Snapshot::operator==(const Snapshot& other) const
{
    if (id != other.id || wifi != other.wifi || pc != other.pc || ip != other.ip) {
        return false;
    }
    for (int i = 0; i < NeighborDetector::CHANNEL_COUNT; ++i) {
        if (!(ir[i] == other.ir[i])) {
            return false;
        }
    }
    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        if (free[i] != other.free[i] || deg[i] != other.deg[i]) {
            return false;
        }
    }
    return true;
}

void StatusDisplay::begin()
{
    // AtomS3 には PSRAM が無いので内部 RAM に取る (128x128x2 = 32 KB)
    _canvas.setPsram(false);
    _canvas.setColorDepth(16);
    _hasCanvas = _canvas.createSprite(SCREEN_SIZE, SCREEN_SIZE) != nullptr;
    if (!_hasCanvas) {
        // 取れなければ LCD に直接描く (ちらつくだけで表示はできる)
        Serial.println("StatusDisplay: createSprite failed, drawing directly");
    }
}

void StatusDisplay::showConnecting()
{
    M5.Lcd.fillScreen(COLOR_BG);

    M5.Lcd.setTextDatum(textdatum_t::top_left);
    M5.Lcd.setFont(&fonts::Font4);
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.drawString("WiFi", 8, 24);

    M5.Lcd.setFont(&fonts::Font2);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.drawString("Connecting", 8, 56);
    M5.Lcd.setCursor(8, 76);
}

void StatusDisplay::showConnectingProgress()
{
    M5.Lcd.print(".");
}

void StatusDisplay::update(
    const PcLink& link,
    const ServoController& servos,
    const NeighborDetector& neighbors,
    unsigned long now)
{
    const Snapshot snapshot = capture(link, servos, neighbors, now);

    if (_drawn && snapshot == _shown) {
        return;
    }
    if (_drawn && now - _lastDraw < DRAW_INTERVAL_MS) {
        return;
    }

    if (_hasCanvas) {
        draw(_canvas, snapshot);
        _canvas.pushSprite(&M5.Lcd, 0, 0);
    }
    else {
        draw(M5.Lcd, snapshot);
    }

    _shown = snapshot;
    _drawn = true;
    _lastDraw = now;
}

StatusDisplay::Snapshot StatusDisplay::capture(
    const PcLink& link,
    const ServoController& servos,
    const NeighborDetector& neighbors,
    unsigned long now)
{
    Snapshot s;
    s.id = link.id();
    s.wifi = link.isWiFiConnected();
    s.pc = link.isPcConnected();
    s.ip = s.wifi ? static_cast<uint32_t>(WiFi.localIP()) : 0;
    for (int i = 0; i < NeighborDetector::CHANNEL_COUNT; ++i) {
        const NeighborDetector::ChannelState& channel = neighbors.channel(i);
        s.ir[i].address = channel.address;
        s.ir[i].fresh = channel.address >= 0 &&
                        now - channel.lastReceived <= config::IR_NEIGHBOR_FRESH_MS;
    }
    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        s.free[i] = servos.mode(i) == ServoController::Mode::Free;
        s.deg[i] = s.free[i] ? 0 : lroundf(servos.position(i));
    }
    return s;
}

void StatusDisplay::draw(lgfx::LovyanGFX& gfx, const Snapshot& s)
{
    gfx.fillScreen(COLOR_BG);

    drawId(gfx, s.id);
    drawBadge(gfx, WIFI_BADGE_Y, "WiFi", s.wifi);
    drawBadge(gfx, PC_BADGE_Y, "PC", s.pc);

    // IP (小さめ。普段は見ないので)
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_center);
    gfx.setTextColor(s.wifi ? COLOR_CYAN : COLOR_RED);
    gfx.drawString(s.wifi ? IPAddress(s.ip).toString().c_str() : "No WiFi", SCREEN_SIZE / 2, IP_Y);

    for (int i = 0; i < NeighborDetector::CHANNEL_COUNT; ++i) {
        drawIr(gfx, i, s.ir[i]);
    }

    gfx.drawFastHLine(0, SEPARATOR_Y, SCREEN_SIZE, COLOR_DARK_GRAY);

    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        drawServo(gfx, i, s.free[i], s.deg[i]);
    }
}

void StatusDisplay::drawId(lgfx::LovyanGFX& gfx, int id)
{
    // Font7 は 7 セグ風の数字と '-' だけを持つ (1 文字 32x48)
    char text[8];
    if (id >= 0) {
        snprintf(text, sizeof(text), "%02d", id);
    }
    else {
        strcpy(text, "--");
    }

    gfx.setFont(&fonts::Font7);
    if (gfx.textWidth(text) > BADGE_X - 4) {
        gfx.setFont(&fonts::Font4);  // 3 桁以上は入らないので小さくする
    }
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextColor(id >= 0 ? COLOR_WHITE : COLOR_GRAY);
    gfx.drawString(text, 2, ID_Y);
}

void StatusDisplay::drawBadge(lgfx::LovyanGFX& gfx, int y, const char* label, bool ok)
{
    gfx.fillRoundRect(BADGE_X, y, BADGE_W, BADGE_H, 4, ok ? COLOR_GREEN : COLOR_RED);

    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(ok ? COLOR_BG : COLOR_WHITE);
    gfx.drawString(label, BADGE_X + BADGE_W / 2, y + BADGE_H / 2 + 1);
}

void StatusDisplay::drawIr(lgfx::LovyanGFX& gfx, int channel, const IrState& ir)
{
    const int x = 1 + channel * IR_PITCH;

    // 緑 = 今も受信中 / 灰 = 途切れた (最後の値を残す) / 枠だけ = 未受信
    uint16_t textColor;
    if (ir.fresh) {
        gfx.fillRoundRect(x, IR_Y, IR_W, IR_H, 3, COLOR_GREEN);
        textColor = COLOR_BG;
    }
    else if (ir.address >= 0) {
        gfx.fillRoundRect(x, IR_Y, IR_W, IR_H, 3, COLOR_DARK_GRAY);
        textColor = COLOR_GRAY;
    }
    else {
        gfx.drawRoundRect(x, IR_Y, IR_W, IR_H, 3, COLOR_DARK_GRAY);
        textColor = COLOR_DARK_GRAY;
    }

    char text[8];
    if (ir.address < 0) {
        snprintf(text, sizeof(text), "%s:--", FACE_NAMES[channel]);
    }
    else if (ir.address == NO_ID_ADDRESS) {
        snprintf(text, sizeof(text), "%s:??", FACE_NAMES[channel]);
    }
    else {
        snprintf(text, sizeof(text), "%s:%02d", FACE_NAMES[channel], ir.address);
    }

    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(textColor);
    gfx.drawString(text, x + IR_W / 2, IR_Y + IR_H / 2 + 1);
}

void StatusDisplay::drawServo(lgfx::LovyanGFX& gfx, int servoId, bool free, long deg)
{
    // 2 列 x 3 行。左列 0, 2, 4 / 右列 1, 3, 5
    const int x = (servoId % 2) * SERVO_COL_W;
    const int y = SERVO_Y + (servoId / 2) * SERVO_ROW_H;

    gfx.setFont(&fonts::Font2);

    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextColor(COLOR_GRAY);
    gfx.drawNumber(servoId, x + 3, y);

    gfx.setTextDatum(textdatum_t::top_right);
    if (free) {
        gfx.setTextColor(COLOR_DARK_GRAY);
        gfx.drawString("FREE", x + SERVO_COL_W - 6, y);
    }
    else {
        char text[8];
        snprintf(text, sizeof(text), deg > 0 ? "+%ld" : "%ld", deg);
        gfx.setTextColor(COLOR_WHITE);
        gfx.drawString(text, x + SERVO_COL_W - 6, y);
    }
}
