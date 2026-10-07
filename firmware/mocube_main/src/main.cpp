#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5AtomS3.h>
#include <IcsHardSerialClass.h>

#include "IrNecReceiver.h"
#include "IrNecTransmitter.h"

// ============================================================
// Pin configuration
// ============================================================
//
// AtomS3 exposes G5/G6/G7/G8/G38/G39 as bottom GPIOs.
// KRS uses G5/G6/G7, so IR must not use those pins.
//
// IR RX:
//   RX1 = G8
//   RX2 = G38
//   RX3 = G39
//
// IR TX:
//   G1 (HY2.0-4P yellow/white-side GPIO)
//
// Change these here if your physical wiring is different.
// ============================================================

constexpr uint8_t IR_RX_PINS[3] = {G8, G38, G39};
constexpr uint8_t IR_TX_PIN = G1;


// ============================================================
// Wi-Fi
// ============================================================

const char* WIFI_SSID = "JSK300";
const char* WIFI_PASSWORD = "89sk389sk3";


// ============================================================
// UDP
// ============================================================

constexpr uint16_t UDP_PORT = 5000;
WiFiUDP udp;


// ============================================================
// Device information
// ============================================================

String myMAC = "";
int myID = -1;


// ============================================================
// Timing
// ============================================================

constexpr unsigned long REGISTER_INTERVAL = 2000;
constexpr unsigned long HEARTBEAT_INTERVAL = 1000;
constexpr unsigned long PC_TIMEOUT = 3000;

unsigned long lastRegister = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastPCAck = 0;


// ============================================================
// State
// ============================================================

bool pcConnected = false;


// ============================================================
// Colors
// ============================================================

#define COLOR_BG       0x1082
#define COLOR_PANEL    0x18E3
#define COLOR_WHITE    0xFFFF
#define COLOR_GRAY     0xBDF7
#define COLOR_GREEN    0x07E0
#define COLOR_RED      0xF800
#define COLOR_YELLOW   0xFFE0
#define COLOR_CYAN     0x07FF


// ============================================================
// KRS Servo
// ============================================================

#define KRS_ICS_TX G5
#define KRS_ICS_RX G6
#define KRS_ICS_EN G7

#define KRS_BAUDRATE 115200
#define KRS_TIMEOUT 1000

IcsHardSerialClass krs(
    &Serial1,
    KRS_ICS_EN,
    KRS_BAUDRATE,
    KRS_TIMEOUT
);

enum KRSState {
    FREE,
    SETPOS
};

KRSState krs_states[6] = {
    FREE, FREE, FREE, FREE, FREE, FREE
};

int krs_poses[6] = {
    0, 0, 0, 0, 0, 0
};


// ============================================================
// IR
// ============================================================

IrNecReceiver irReceiver(IR_RX_PINS);
IrNecTransmitter irTransmitter(IR_TX_PIN);


// ============================================================
// Utility
// ============================================================

int split(String data, char delimiter, String* dst)
{
    int index = 0;
    const int dataLength = data.length();

    for (int i = 0; i < dataLength; ++i) {
        const char tmp = data.charAt(i);

        if (tmp == delimiter) {
            ++index;
        } else {
            dst[index] += tmp;
        }
    }

    return index + 1;
}


// ============================================================
// Wi-Fi / UDP
// ============================================================

IPAddress getBroadcastAddress()
{
    IPAddress ip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();

    IPAddress broadcast;

    for (int i = 0; i < 4; ++i) {
        broadcast[i] =
            (ip[i] & mask[i]) |
            (~mask[i] & 0xFF);
    }

    return broadcast;
}

void sendBroadcast(const String& message)
{
    const IPAddress broadcastIP = getBroadcastAddress();

    udp.beginPacket(broadcastIP, UDP_PORT);
    udp.print(message);
    udp.endPacket();

    // Serial.print("SEND: ");
    // Serial.println(message);
}

void sendRegister()
{
    sendBroadcast("REGISTER," + myMAC);
}

void sendHeartbeat()
{
    sendBroadcast("HEARTBEAT," + myMAC);
}


// ============================================================
// UI
// ============================================================

void drawStatusDot(int x, int y, bool state)
{
    M5.Lcd.fillCircle(
        x,
        y,
        5,
        state ? COLOR_GREEN : COLOR_RED
    );
}

void drawUI()
{
    M5.Lcd.fillScreen(COLOR_BG);

    // Module ID
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 20);
    M5.Lcd.print("MODULE ID");

    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(75, 10);

    if (myID >= 0) {
        if (myID < 10) {
            M5.Lcd.print("0");
        }
        M5.Lcd.print(myID);
    } else {
        M5.Lcd.print("--");
    }

    // Wi-Fi
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.setCursor(10, 33);
    M5.Lcd.print("WiFi");

    drawStatusDot(
        40,
        36,
        WiFi.status() == WL_CONNECTED
    );

    // PC
    M5.Lcd.setCursor(60, 33);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.print("PC");

    drawStatusDot(
        82,
        36,
        pcConnected
    );

    // IP
    M5.Lcd.setTextColor(COLOR_CYAN);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 46);

    if (WiFi.status() == WL_CONNECTED) {
        M5.Lcd.print(WiFi.localIP().toString());
    } else {
        M5.Lcd.print("No WiFi");
    }

    // KRS
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 59);
    M5.Lcd.print("Servo");

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            const int KRS_ID = i * 2 + j;

            M5.Lcd.setCursor(
                10 + 50 * j,
                69 + 10 * i
            );

            M5.Lcd.print(KRS_ID);

            if (krs_states[KRS_ID] == FREE) {
                M5.Lcd.print(":F");
            }
            else if (krs_states[KRS_ID] == SETPOS) {
                M5.Lcd.print(":P");
                M5.Lcd.print(krs_poses[KRS_ID]);
            }
        }
    }
}


// ============================================================
// Wi-Fi connection
// ============================================================

void connectWiFi()
{
    M5.Lcd.fillScreen(COLOR_BG);

    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 15);
    M5.Lcd.println("WiFi");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 45);
    M5.Lcd.println("Connecting...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);

        Serial.print(".");
        M5.Lcd.print(".");
    }

    Serial.println();

    Serial.println("WiFi connected!");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
}


// ============================================================
// IR callback
// ============================================================

void onIrMessage(
    uint8_t channel,
    const IrNecReceiver::Message& message)
{
    Serial.printf(
        "IR RX%d: ADDR=0x%02X CMD=0x%02X DATA=0x%08lX\n",
        channel + 1,
        message.address,
        message.command,
        static_cast<unsigned long>(message.raw)
    );

    // TODO:
    // 必要ならここで受信したNECコマンドに応じた処理を行う。
}


// ============================================================
// KRS control
// ============================================================

void controlKRS()
{
    // TODO
}


// ============================================================
// UDP receive
// ============================================================

void receiveUDP()
{
    const int packetSize = udp.parsePacket();

    if (packetSize <= 0) {
        return;
    }

    char buffer[256];

    const int length =
        udp.read(buffer, sizeof(buffer) - 1);

    if (length <= 0) {
        return;
    }

    buffer[length] = '\0';

    const String message(buffer);

    // Serial.print("RECV: ");
    // Serial.println(message);

    String msgs[5];
    const int index = split(message, ',', msgs);

    if (index <= 0) {
        return;
    }

    // --------------------------------------------------------
    // ID assignment
    // --------------------------------------------------------

    if (msgs[0] == "ID" && index >= 2) {

        myID = msgs[1].toInt();

        lastPCAck = millis();
        pcConnected = true;

        Serial.print("Assigned ID: ");
        Serial.println(myID);

        drawUI();
    }

    // --------------------------------------------------------
    // Heartbeat ACK
    // --------------------------------------------------------

    else if (msgs[0] == "ACK" && index >= 2) {

        const int receivedID = msgs[1].toInt();

        if (myID >= 0 && receivedID == myID) {
            lastPCAck = millis();
            pcConnected = true;

            Serial.println("PC heartbeat OK");

            drawUI();
        }
    }

    // --------------------------------------------------------
    // KRS Servo
    // --------------------------------------------------------

    else if (msgs[0] == "KRS" && index >= 3) {

        const String cmd = msgs[1];

        if (cmd == "setPos" && index >= 4) {

            const int ID = msgs[2].toInt();
            const int pos = msgs[3].toInt();

            if (ID >= 0 && ID < 6) {
                krs.setPos(ID, pos);
                krs_states[ID] = SETPOS;
                krs_poses[ID] = pos;
            }
        }

        if (cmd == "setFree" && index >= 3) {

            const int ID = msgs[2].toInt();

            if (ID >= 0 && ID < 6) {
                krs.setFree(ID);
                krs_states[ID] = FREE;
            }
        }
    }

    // --------------------------------------------------------
    // IR TX
    //
    // Example:
    //   IR,send,0xE718FF00
    // --------------------------------------------------------

    else if (msgs[0] == "IR" &&
             index >= 3 &&
             msgs[1] == "send") {

        const uint32_t raw =
            strtoul(msgs[2].c_str(), nullptr, 0);

        if (irTransmitter.send(raw)) {
            Serial.printf(
                "IR TX: 0x%08lX\n",
                static_cast<unsigned long>(raw)
            );
        } else {
            Serial.println("IR TX: failed");
        }
    }
}


// ============================================================
// setup
// ============================================================

void setup()
{
    auto cfg = M5.config();

    M5.begin(cfg);

    Serial.begin(115200);

    delay(500);

    // --------------------------------------------------------
    // Wi-Fi
    // --------------------------------------------------------

    connectWiFi();

    myMAC = WiFi.macAddress();

    udp.begin(UDP_PORT);

    // --------------------------------------------------------
    // UI
    // --------------------------------------------------------

    drawUI();

    // --------------------------------------------------------
    // KRS
    // --------------------------------------------------------

    Serial1.begin(
        KRS_BAUDRATE,
        SERIAL_8E1,
        KRS_ICS_RX,
        KRS_ICS_TX
    );

    krs.begin();

    // --------------------------------------------------------
    // IR
    // --------------------------------------------------------

    irReceiver.begin(onIrMessage);
    irTransmitter.begin(38000);

    Serial.println("IR receiver/transmitter ready");

    // --------------------------------------------------------
    // PC registration
    // --------------------------------------------------------

    sendRegister();
    lastRegister = millis();
}


// ============================================================
// loop
// ============================================================

void loop()
{

    M5.update();

    // --------------------------------------------------------
    // IR RX
    //
    // ISRは常時エッジを記録し、ここでNECをデコードする。
    // --------------------------------------------------------

    irReceiver.update();

    // --------------------------------------------------------
    // Wi-Fi connection
    // --------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {

        pcConnected = false;
        drawUI();

        delay(1000);
        return;
    }

    // --------------------------------------------------------
    // UDP
    // --------------------------------------------------------

    receiveUDP();

    const unsigned long now = millis();

    // --------------------------------------------------------
    // Register
    // --------------------------------------------------------

    if (myID < 0 &&
        now - lastRegister >= REGISTER_INTERVAL) {

        lastRegister = now;
        sendRegister();
    }

    // --------------------------------------------------------
    // Heartbeat
    // --------------------------------------------------------

    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {

        lastHeartbeat = now;
        sendHeartbeat();
    }

    // --------------------------------------------------------
    // PC timeout
    // --------------------------------------------------------

    if (now - lastPCAck >= PC_TIMEOUT) {

        if (pcConnected) {

            pcConnected = false;

            Serial.println("PC connection lost");

            drawUI();
        }
    }

    // --------------------------------------------------------
    // IR TX
    // --------------------------------------------------------

    static int last_send = 0;
    if (millis() - last_send >= 1000) {
        // Serial.println("data send");
        last_send = millis();
        irTransmitter.send(0x01, 0x2E);
    }

    // No delay is required for IR reception.
    // The receiver uses a ring buffer, so the loop can perform
    // other work without immediately losing an edge.
}
