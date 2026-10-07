// 2026/10/07

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5AtomS3.h>
#include <IcsHardSerialClass.h>


// ============================================================
// Wi-Fi
// ============================================================

const char* WIFI_SSID = "JSK300";
const char* WIFI_PASSWORD = "89sk389sk3";


// ============================================================
// UDP
// ============================================================

const uint16_t UDP_PORT = 5000;

WiFiUDP udp;


// ============================================================
// Device information
// ============================================================

String myMAC = "";

int myID = -1;


// ============================================================
// Timing
// ============================================================

const unsigned long REGISTER_INTERVAL = 2000;
const unsigned long HEARTBEAT_INTERVAL = 1000;
const unsigned long PC_TIMEOUT = 3000;

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
// Broadcast address
//
// 255.255.255.255ではなく、現在のWi-Fiの
// subnet broadcastを計算する
// ============================================================

IPAddress getBroadcastAddress()
{https://chatgpt.com/codex
    IPAddress ip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();

    IPAddress broadcast;

    for (int i = 0; i < 4; i++)
    {
        broadcast[i] =
            (ip[i] & mask[i]) |
            (~mask[i] & 0xFF);
    }

    return broadcast;
}


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

enum KRSState krs_states[6] = {
    FREE, FREE, FREE, FREE, FREE, FREE
};

int krs_poses[6] = {
    0, 0, 0, 0, 0, 0
};


// ============================================================
// split
// ============================================================

int split(String data, char delimiter, String *dst){
    int index = 0; 
    int datalength = data.length();
    
    for (int i = 0; i < datalength; i++) {
        char tmp = data.charAt(i);
        if ( tmp == delimiter ) {
            index++;
        }
        else dst[index] += tmp;
    }
    
    return (index + 1);
}


// ============================================================
// Send UDP
// ============================================================

void sendBroadcast(String message)
{
    IPAddress broadcastIP =
        getBroadcastAddress();

    udp.beginPacket(
        broadcastIP,
        UDP_PORT
    );

    udp.print(message);

    udp.endPacket();

    Serial.print("SEND: ");
    Serial.println(message);
}


// ============================================================
// Register
// ============================================================

void sendRegister()
{
    String message =
        "REGISTER," + myMAC;

    sendBroadcast(message);
}


// ============================================================
// Heartbeat
// ============================================================

void sendHeartbeat()
{
    String message =
        "HEARTBEAT," + myMAC;

    sendBroadcast(message);
}


// ============================================================
// Draw status indicator
// ============================================================

void drawStatusDot(
    int x,
    int y,
    bool state
)
{
    if (state)
    {
        M5.Lcd.fillCircle(
            x,
            y,
            5,
            COLOR_GREEN
        );
    }
    else
    {
        M5.Lcd.fillCircle(
            x,
            y,
            5,
            COLOR_RED
        );
    }
}


// ============================================================
// Draw UI
// ============================================================

void drawUI()
{
    M5.Lcd.fillScreen(COLOR_BG);

    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

    // M5.Lcd.fillRoundRect(
    //     4,
    //     4,
    //     120,
    //     24,
    //     6,
    //     COLOR_PANEL
    // );

    // M5.Lcd.setTextSize(2);
    // M5.Lcd.setTextColor(COLOR_WHITE);

    // M5.Lcd.setCursor(12, 10);

    // if (pcConnected)
    // {
    //     M5.Lcd.setTextColor(COLOR_GREEN);
    //     M5.Lcd.print("PC ONLINE");
    // }
    // else
    // {
    //     M5.Lcd.setTextColor(COLOR_RED);
    //     M5.Lcd.print("PC OFFLINE");
    // }


    // --------------------------------------------------------
    // ID
    // --------------------------------------------------------

    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(10, 20);
    M5.Lcd.print("MODULE ID");


    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(2);

    M5.Lcd.setCursor(75, 10);

    if (myID >= 0)
    {
        if (myID < 10)
        {
            M5.Lcd.print("0");
        }

        M5.Lcd.print(myID);
    }
    else
    {
        M5.Lcd.print("--");
    }


    // --------------------------------------------------------
    // WiFi
    // --------------------------------------------------------

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GRAY);

    M5.Lcd.setCursor(10, 33);
    M5.Lcd.print("WiFi");

    drawStatusDot(
        40,
        36,
        WiFi.status() == WL_CONNECTED
    );


    // --------------------------------------------------------
    // PC
    // --------------------------------------------------------

    M5.Lcd.setCursor(60, 33);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.print("PC");

    drawStatusDot(
        82,
        36,
        pcConnected
    );


    // --------------------------------------------------------
    // IP
    // --------------------------------------------------------

    M5.Lcd.setTextColor(COLOR_CYAN);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(10, 46);

    if (WiFi.status() == WL_CONNECTED)
    {
        M5.Lcd.print(
            WiFi.localIP().toString()
        );
    }
    else
    {
        M5.Lcd.print("No WiFi");
    }

    // --------------------------------------------------------
    // KRS
    // --------------------------------------------------------

    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(10, 59);
    M5.Lcd.print("Servo");

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            int KRS_ID = i * 2 + j;
            M5.Lcd.setCursor(10 + 50 * j, 69 + 10 * i);
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
// WiFi connection
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

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    while (
        WiFi.status() != WL_CONNECTED
    )
    {
        delay(500);

        Serial.print(".");

        M5.Lcd.print(".");
    }


    Serial.println();

    Serial.println(
        "WiFi connected!"
    );

    Serial.print("IP: ");
    Serial.println(
        WiFi.localIP()
    );

    Serial.print("MAC: ");
    Serial.println(
        WiFi.macAddress()
    );
}


// ============================================================
// controlKRS
// ============================================================

void controlKRS()
{
    // TODO
}


// ============================================================
// Receive UDP
// ============================================================

void receiveUDP()
{
    int packetSize =
        udp.parsePacket();

    if (packetSize <= 0)
    {
        return;
    }


    char buffer[256];

    int length =
        udp.read(
            buffer,
            sizeof(buffer) - 1
        );

    buffer[length] = '\0';


    String message =
        String(buffer);


    Serial.print("RECV: ");
    Serial.println(message);

    String msgs[5];

    int index = split(message, ',', msgs);


    // --------------------------------------------------------
    // ID assignment
    // --------------------------------------------------------

    if (msgs[0] == "ID")
    {
        myID = msgs[1].toInt();

        // PCから応答が来たので、
        // PCとの通信開始
        lastPCAck = millis();

        pcConnected = true;

        Serial.print("Assigned ID: ");
        Serial.println(myID);

        drawUI();
    }


    // --------------------------------------------------------
    // Heartbeat ACK
    // --------------------------------------------------------

    else if (msgs[0] == "ACK")
    {
        int receivedID = msgs[1].toInt();

        // 自分のIDなら正常
        if (
            myID >= 0 &&
            receivedID == myID
        )
        {
            lastPCAck = millis();

            pcConnected = true;

            Serial.println(
                "PC heartbeat OK"
            );

            drawUI();
        }
    }

    // --------------------------------------------------------
    // KRS Servo
    // --------------------------------------------------------

    else if (msgs[0] == "KRS")
    {
        String cmd = msgs[1];

        if (cmd == "setPos") {
            int ID = msgs[2].toInt();
            int pos = msgs[3].toInt();
            krs.setPos(ID,pos);
            krs_states[ID] = SETPOS;
            krs_poses[ID] = pos;
        }

        if (cmd == "setFree") {
            int ID = msgs[2].toInt();
            krs.setFree(ID);
            krs_states[ID] = FREE;
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


    // WiFi接続
    connectWiFi();


    // MAC取得
    myMAC =
        WiFi.macAddress();


    // UDP開始
    udp.begin(UDP_PORT);


    // 初期状態
    drawUI();


    // PCに登録
    sendRegister();

    lastRegister =
        millis();


    // KRS開始
    Serial1.begin(
        KRS_BAUDRATE,
        SERIAL_8E1,
        KRS_ICS_RX,
        KRS_ICS_TX
    );

    krs.begin();  //サーボモータの通信初期設定
}


// ============================================================
// loop
// ============================================================

void loop()
{
    M5.update();


    // --------------------------------------------------------
    // WiFi切断チェック
    // --------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        pcConnected = false;

        drawUI();

        delay(1000);

        return;
    }


    // --------------------------------------------------------
    // UDP受信
    // --------------------------------------------------------

    receiveUDP();


    unsigned long now =
        millis();


    // --------------------------------------------------------
    // ID未取得ならREGISTERを繰り返す
    // --------------------------------------------------------

    if (myID < 0)
    {
        if (
            now - lastRegister
            >= REGISTER_INTERVAL
        )
        {
            lastRegister = now;

            sendRegister();
        }
    }


    // --------------------------------------------------------
    // Heartbeat
    // --------------------------------------------------------

    if (
        now - lastHeartbeat
        >= HEARTBEAT_INTERVAL
    )
    {
        lastHeartbeat = now;

        sendHeartbeat();
    }


    // --------------------------------------------------------
    // PC timeout
    // --------------------------------------------------------

    if (
        now - lastPCAck
        >= PC_TIMEOUT
    )
    {
        if (pcConnected)
        {
            pcConnected = false;

            Serial.println(
                "PC connection lost"
            );

            drawUI();
        }
    }


    delay(10);
}