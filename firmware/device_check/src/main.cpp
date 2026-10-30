// ============================================================
// mocube モジュール (M5Stack AtomS3) のデバイス確認用ファーム。
//
// Wi-Fi / PC には繋がず、モジュール単体で次のデバイスを確認する。
// ピン・ボーレートなどは mocube_main と同じ形式の Config.h から読む。
//
//   SERVO  KRS サーボ (ICS)。ID 0..KRS_SERVO_COUNT-1 を順に ±SERVO_TEST_DEG 往復させ、応答を表示
//   IR     赤外線送信 (IR_TX_PIN) と 3ch 受信 (IR_RX_PINS)。送りながら各 ch の受信を表示
//   LED    インジケータ LED (INDICATOR_PIN) を点滅
//
// 画面のボタン (BtnA) を押すと SERVO → IR → LED → SERVO ... と切り替わる。
// シリアル (115200 bps) からも操作できる。"help" でコマンド一覧。
// ============================================================

#include <Arduino.h>
#include <M5AtomS3.h>
#include <IcsHardSerialClass.h>

#include "Config.h"
#include "IrNecReceiver.h"
#include "IrNecTransmitter.h"
#include "NecFormat.h"

namespace {

// ------------------------------------------------------------
// 確認用の設定 (Config.h は mocube_main と同じにしておくため、ここに置く)
// ------------------------------------------------------------

constexpr float SERVO_TEST_DEG = 30.0f;       // 往復させる角度 [deg]
constexpr unsigned long SERVO_STEP_MS = 700;  // 1 指令ごとの間隔

constexpr uint8_t IR_TEST_ADDRESS = 0xA5;     // 送信フレームの address (command は送信回数)

constexpr unsigned long LED_BLINK_MS = 500;

constexpr unsigned long DRAW_INTERVAL_MS = 200;

// 赤外線チャンネル番号 -> 面の名前 (mocube_main と同じ)
constexpr const char* FACE_NAMES[IrNecReceiver::CHANNEL_COUNT] = {"u", "r", "f"};

// RGB565
constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GRAY = 0x8410;
constexpr uint16_t COLOR_DARK_GRAY = 0x3186;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_CYAN = 0x07FF;

constexpr int SCREEN_SIZE = 128;
constexpr int TITLE_H = 18;
constexpr int ROW_H = 16;


// ------------------------------------------------------------
// 状態
// ------------------------------------------------------------

enum class Mode : uint8_t {
    Servo,
    Ir,
    Led,
    Count,
};

struct ServoResult {
    bool tested = false;
    bool ok = false;
    int pos = 0;  // サーボが返した現在ポジション (ICS の値)
};

struct IrChannelResult {
    uint32_t count = 0;
    uint8_t address = 0;
    uint8_t command = 0;
    unsigned long lastReceived = 0;
};

IcsHardSerialClass krs(
    &Serial1,
    config::KRS_ICS_EN,
    config::KRS_BAUDRATE,
    config::KRS_TIMEOUT_MS);

IrNecReceiver irReceiver(config::IR_RX_PINS);
IrNecTransmitter irTransmitter(config::IR_TX_PIN);

M5Canvas canvas;
bool hasCanvas = false;

Mode mode = Mode::Servo;

ServoResult servoResults[config::KRS_SERVO_COUNT];
int servoId = 0;
int servoStep = 0;  // 0: +deg, 1: -deg, 2: 0, 3: free
unsigned long lastServoStep = 0;

IrChannelResult irResults[IrNecReceiver::CHANNEL_COUNT];
uint32_t irTxCount = 0;
unsigned long lastIrSend = 0;

bool ledOn = false;
unsigned long lastLedToggle = 0;

unsigned long lastDraw = 0;

String serialLine;


// ------------------------------------------------------------
// KRS サーボ
// ------------------------------------------------------------

// setPos / setFree の戻り値 (応答の現在ポジション、無応答なら ICS_FALSE) を記録する
void recordServo(int id, int pos)
{
    ServoResult& result = servoResults[id];
    result.tested = true;
    result.ok = pos != IcsBaseClass::ICS_FALSE;
    result.pos = pos;

    if (result.ok) {
        Serial.printf("KRS id=%d: pos=%d (%.1f deg)\n", id, pos, IcsBaseClass::posDeg(pos));
    }
    else {
        Serial.printf("KRS id=%d: no response\n", id);
    }
}

bool servoSetDeg(int id, float deg)
{
    if (id < 0 || id >= config::KRS_SERVO_COUNT || fabsf(deg) > config::KRS_MAX_DEG) {
        Serial.println("KRS: id or deg out of range");
        return false;
    }
    recordServo(id, krs.setPos(id, IcsBaseClass::degPos(deg)));
    return true;
}

bool servoFree(int id)
{
    if (id < 0 || id >= config::KRS_SERVO_COUNT) {
        Serial.println("KRS: id out of range");
        return false;
    }
    recordServo(id, krs.setFree(id));
    return true;
}

// 1 台ずつ +deg → -deg → 0 → 脱力 の順に動かし、次の ID へ進む。
// 無応答の ID では setPos がタイムアウト (KRS_TIMEOUT_MS) までブロックする
void updateServoSweep(unsigned long now)
{
    if (now - lastServoStep < SERVO_STEP_MS) {
        return;
    }
    lastServoStep = now;

    switch (servoStep) {
    case 0: servoSetDeg(servoId, SERVO_TEST_DEG); break;
    case 1: servoSetDeg(servoId, -SERVO_TEST_DEG); break;
    case 2: servoSetDeg(servoId, 0.0f); break;
    default: servoFree(servoId); break;
    }

    if (++servoStep > 3) {
        servoStep = 0;
        servoId = (servoId + 1) % config::KRS_SERVO_COUNT;
    }
}


// ------------------------------------------------------------
// 赤外線
// ------------------------------------------------------------

void onIrMessage(uint8_t channel, const IrNecReceiver::Message& message)
{
    IrChannelResult& result = irResults[channel];
    result.count++;
    result.address = message.address;
    result.command = message.command;
    result.lastReceived = millis();

    Serial.printf(
        "IR RX%d (%s, GPIO%u): ADDR=0x%02X CMD=0x%02X DATA=0x%08lX\n",
        channel + 1,
        FACE_NAMES[channel],
        config::IR_RX_PINS[channel],
        message.address,
        message.command,
        static_cast<unsigned long>(message.raw)
    );
}

void irSend(uint8_t address, uint8_t command)
{
    if (irTransmitter.send(address, command)) {
        irTxCount++;
    }
    else {
        Serial.println("IR TX: failed");
    }
}

void updateIrSend(unsigned long now)
{
    if (now - lastIrSend < config::IR_BEACON_INTERVAL_MS) {
        return;
    }
    lastIrSend = now;

    irSend(IR_TEST_ADDRESS, static_cast<uint8_t>(irTxCount));
}


// ------------------------------------------------------------
// インジケータ LED
// ------------------------------------------------------------

void setLed(bool on)
{
    ledOn = on;
    digitalWrite(config::INDICATOR_PIN, on ? HIGH : LOW);
}

void updateLedBlink(unsigned long now)
{
    if (now - lastLedToggle < LED_BLINK_MS) {
        return;
    }
    lastLedToggle = now;

    setLed(!ledOn);
}


// ------------------------------------------------------------
// モード切り替え
// ------------------------------------------------------------

const char* modeName(Mode m)
{
    switch (m) {
    case Mode::Servo: return "SERVO";
    case Mode::Ir: return "IR";
    case Mode::Led: return "LED";
    default: return "?";
    }
}

void setMode(Mode next)
{
    // 抜けるときに止める: 動かしかけのサーボは脱力、LED は消灯
    if (mode == Mode::Servo && servoStep != 0) {
        servoFree(servoId);
        servoStep = 0;
        servoId = (servoId + 1) % config::KRS_SERVO_COUNT;
    }
    if (mode == Mode::Led) {
        setLed(false);
    }

    mode = next;
    Serial.printf("mode: %s\n", modeName(mode));
}

void nextMode()
{
    setMode(static_cast<Mode>((static_cast<uint8_t>(mode) + 1) % static_cast<uint8_t>(Mode::Count)));
}


// ------------------------------------------------------------
// シリアルコマンド
// ------------------------------------------------------------

void printHelp()
{
    Serial.println("commands:");
    Serial.println("  mode servo|ir|led   switch mode (same as BtnA)");
    Serial.println("  pos <id> <deg>      KRS setPos");
    Serial.println("  free <id>           KRS setFree");
    Serial.println("  freeall             KRS setFree for all ids");
    Serial.println("  ir <addr> <cmd>     send one NEC frame (0x.. or decimal)");
    Serial.println("  led on|off          indicator LED");
    Serial.println("  pins                print pin config");
}

void printPins()
{
    Serial.printf("KRS ICS TX=G%u RX=G%u EN=G%u, %ld bps\n",
                  config::KRS_ICS_TX, config::KRS_ICS_RX, config::KRS_ICS_EN, config::KRS_BAUDRATE);
    for (size_t i = 0; i < IrNecReceiver::CHANNEL_COUNT; ++i) {
        Serial.printf("IR RX ch%u (%s) = G%u\n", i, FACE_NAMES[i], config::IR_RX_PINS[i]);
    }
    Serial.printf("IR TX = G%u, %lu Hz\n", config::IR_TX_PIN, static_cast<unsigned long>(config::IR_CARRIER_HZ));
    Serial.printf("INDICATOR LED = G%u\n", config::INDICATOR_PIN);
}

void handleSerialLine(const String& line)
{
    char cmd[16] = {};
    char arg1[16] = {};
    char arg2[16] = {};
    const int n = sscanf(line.c_str(), "%15s %15s %15s", cmd, arg1, arg2);
    if (n <= 0) {
        return;
    }

    const String c(cmd);
    const String a(arg1);

    if (c == "help") {
        printHelp();
    }
    else if (c == "pins") {
        printPins();
    }
    else if (c == "mode" && n >= 2) {
        if (a == "servo") setMode(Mode::Servo);
        else if (a == "ir") setMode(Mode::Ir);
        else if (a == "led") setMode(Mode::Led);
        else Serial.println("unknown mode");
    }
    else if (c == "pos" && n >= 3) {
        servoSetDeg(a.toInt(), String(arg2).toFloat());
    }
    else if (c == "free" && n >= 2) {
        servoFree(a.toInt());
    }
    else if (c == "freeall") {
        for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
            servoFree(i);
        }
    }
    else if (c == "ir" && n >= 3) {
        irSend(static_cast<uint8_t>(strtoul(arg1, nullptr, 0)),
               static_cast<uint8_t>(strtoul(arg2, nullptr, 0)));
        Serial.printf("IR TX: ADDR=0x%02X CMD=0x%02X\n",
                      static_cast<uint8_t>(strtoul(arg1, nullptr, 0)),
                      static_cast<uint8_t>(strtoul(arg2, nullptr, 0)));
    }
    else if (c == "led" && n >= 2) {
        setLed(a == "on");
        Serial.printf("LED: %s\n", ledOn ? "ON" : "OFF");
    }
    else {
        Serial.println("unknown command (type \"help\")");
    }
}

void updateSerial()
{
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (serialLine.length() > 0) {
                handleSerialLine(serialLine);
                serialLine = "";
            }
        }
        else if (serialLine.length() < 64) {
            serialLine += ch;
        }
    }
}


// ------------------------------------------------------------
// 画面
// ------------------------------------------------------------

void drawTitle(lgfx::LovyanGFX& gfx)
{
    gfx.fillRect(0, 0, SCREEN_SIZE, TITLE_H, COLOR_CYAN);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(COLOR_BG);
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.drawString(modeName(mode), 4, TITLE_H / 2 + 1);

    char text[8];
    snprintf(text, sizeof(text), "%u/%u",
             static_cast<uint8_t>(mode) + 1, static_cast<uint8_t>(Mode::Count));
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.drawString(text, SCREEN_SIZE - 4, TITLE_H / 2 + 1);
}

//  ID  結果  応答の角度
//  >0  OK    +29.9     (> = 今動かしている ID)
//   1  NG
//   2  --              (-- = まだ試していない)
void drawServo(lgfx::LovyanGFX& gfx)
{
    gfx.setFont(&fonts::Font2);

    for (int i = 0; i < config::KRS_SERVO_COUNT; ++i) {
        const int y = TITLE_H + 2 + i * ROW_H;
        const ServoResult& r = servoResults[i];
        char text[16];

        gfx.setTextDatum(textdatum_t::top_left);
        gfx.setTextColor(i == servoId ? COLOR_WHITE : COLOR_GRAY);
        snprintf(text, sizeof(text), "%c%d", i == servoId ? '>' : ' ', i);
        gfx.drawString(text, 2, y);

        if (!r.tested) {
            gfx.setTextColor(COLOR_DARK_GRAY);
            gfx.drawString("--", 30, y);
            continue;
        }

        gfx.setTextColor(r.ok ? COLOR_GREEN : COLOR_RED);
        gfx.drawString(r.ok ? "OK" : "NG", 30, y);

        if (r.ok) {
            snprintf(text, sizeof(text), "%+.1f", IcsBaseClass::posDeg(r.pos));
            gfx.setTextColor(COLOR_WHITE);
            gfx.setTextDatum(textdatum_t::top_right);
            gfx.drawString(text, SCREEN_SIZE - 4, y);
        }
    }
}

//  TX G1  n=123            送信回数
//  u G8   12   A5:3F       ch ごとの受信数と最後の address:command
//                          (緑 = IR_NEIGHBOR_FRESH_MS 以内に受信)
void drawIr(lgfx::LovyanGFX& gfx, unsigned long now)
{
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::top_left);

    char text[24];
    snprintf(text, sizeof(text), "TX G%u n=%lu",
             config::IR_TX_PIN, static_cast<unsigned long>(irTxCount));
    gfx.setTextColor(COLOR_WHITE);
    gfx.drawString(text, 2, TITLE_H + 2);

    for (size_t i = 0; i < IrNecReceiver::CHANNEL_COUNT; ++i) {
        const IrChannelResult& r = irResults[i];
        const int y = TITLE_H + 4 + (i + 1) * (ROW_H + 6);
        const bool fresh = r.count > 0 && now - r.lastReceived <= config::IR_NEIGHBOR_FRESH_MS;

        if (fresh) {
            gfx.fillRoundRect(0, y - 2, SCREEN_SIZE, ROW_H + 3, 3, COLOR_GREEN);
            gfx.setTextColor(COLOR_BG);
        }
        else {
            gfx.drawRoundRect(0, y - 2, SCREEN_SIZE, ROW_H + 3, 3, COLOR_DARK_GRAY);
            gfx.setTextColor(r.count > 0 ? COLOR_GRAY : COLOR_DARK_GRAY);
        }

        snprintf(text, sizeof(text), "%s G%u %lu",
                 FACE_NAMES[i], config::IR_RX_PINS[i], static_cast<unsigned long>(r.count));
        gfx.setTextDatum(textdatum_t::top_left);
        gfx.drawString(text, 4, y);

        if (r.count > 0) {
            snprintf(text, sizeof(text), "%02X:%02X", r.address, r.command);
            gfx.setTextDatum(textdatum_t::top_right);
            gfx.drawString(text, SCREEN_SIZE - 4, y);
        }
    }
}

void drawLed(lgfx::LovyanGFX& gfx)
{
    char text[8];
    snprintf(text, sizeof(text), "G%u", config::INDICATOR_PIN);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::top_center);
    gfx.setTextColor(COLOR_GRAY);
    gfx.drawString(text, SCREEN_SIZE / 2, TITLE_H + 6);

    gfx.setFont(&fonts::Font4);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(ledOn ? COLOR_GREEN : COLOR_DARK_GRAY);
    gfx.drawString(ledOn ? "ON" : "OFF", SCREEN_SIZE / 2, SCREEN_SIZE / 2 + 8);
}

void draw(lgfx::LovyanGFX& gfx, unsigned long now)
{
    gfx.fillScreen(COLOR_BG);
    drawTitle(gfx);

    switch (mode) {
    case Mode::Servo: drawServo(gfx); break;
    case Mode::Ir: drawIr(gfx, now); break;
    case Mode::Led: drawLed(gfx); break;
    default: break;
    }
}

void updateDisplay(unsigned long now)
{
    if (now - lastDraw < DRAW_INTERVAL_MS) {
        return;
    }
    lastDraw = now;

    if (hasCanvas) {
        draw(canvas, now);
        canvas.pushSprite(&M5.Lcd, 0, 0);
    }
    else {
        draw(M5.Lcd, now);
    }
}

}  // namespace


void setup()
{
    auto cfg = M5.config();

    M5.begin(cfg);

    Serial.begin(115200);

    delay(500);

    // AtomS3 には PSRAM が無いので内部 RAM に取る (128x128x2 = 32 KB)
    canvas.setPsram(false);
    canvas.setColorDepth(16);
    hasCanvas = canvas.createSprite(SCREEN_SIZE, SCREEN_SIZE) != nullptr;

    pinMode(config::INDICATOR_PIN, OUTPUT);
    setLed(false);

    // 先にピンを指定して Serial1 を開く。krs.begin() の中でも
    // Serial1.begin(baud, SERIAL_8E1) が (ピン指定なしで) 呼ばれる。
    Serial1.begin(
        config::KRS_BAUDRATE,
        SERIAL_8E1,
        config::KRS_ICS_RX,
        config::KRS_ICS_TX
    );
    krs.begin();

    irReceiver.begin(onIrMessage);
    irTransmitter.begin(config::IR_CARRIER_HZ);

    Serial.println("mocube device_check");
    printPins();
    printHelp();
    Serial.printf("mode: %s\n", modeName(mode));
}


void loop()
{
    M5.update();

    if (M5.BtnA.wasPressed()) {
        nextMode();
    }

    updateSerial();

    // 受信はどのモードでも続ける (止めるとリングバッファが溢れる)
    irReceiver.update();

    const unsigned long now = millis();

    switch (mode) {
    case Mode::Servo: updateServoSweep(now); break;
    case Mode::Ir: updateIrSend(now); break;
    case Mode::Led: updateLedBlink(now); break;
    default: break;
    }

    updateDisplay(now);
}
