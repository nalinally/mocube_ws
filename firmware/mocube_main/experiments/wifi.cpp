#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5Unified.h>

const char* ssid = "JSK300";
const char* password = "89sk389sk3";

WiFiUDP udp;
const int PORT = 5000;

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setTextSize(2);

    M5.Display.setCursor(10, 10);
    M5.Display.print("Connecting");

    WiFi.begin(ssid, password);
    // WiFi.mode(WIFI_STA);
    // WiFi.disconnect();
    // delay(100);

    // int n = WiFi.scanNetworks();

    // M5.Display.print("Found ");
    // M5.Display.print(n);
    // M5.Display.println(" networks\n");

    // for (int i = 0; i < n; i++) {
    //     M5.Display.setCursor(10, 10);
    //     M5.Display.printf(
    //         "%d: %s  RSSI=%d  channel=%d  encryption=%d\n",
    //         i,
    //         WiFi.SSID(i).c_str(),
    //         WiFi.RSSI(i),
    //         WiFi.channel(i),
    //         WiFi.encryptionType(i)
    //     );
    //     delay(1000);
    // }

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(WiFi.status());
        M5.Display.print(WiFi.status());
    }

    Serial.println();
    Serial.println(WiFi.localIP());

    udp.begin(PORT);

    M5.Display.setCursor(10, 10);
    M5.Display.println(WiFi.localIP());
}

void loop() {
    int packetSize = udp.parsePacket();

    if (packetSize) {
        char buf[256];

        int len = udp.read(buf, sizeof(buf) - 1);
        buf[len] = '\0';

        Serial.println(buf);

        M5.Display.setCursor(10, 10);
        M5.Display.println(buf);

        // ここでコマンドを解析
    }
}