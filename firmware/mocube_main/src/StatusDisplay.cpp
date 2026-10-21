#include "StatusDisplay.h"

#include <M5AtomS3.h>

namespace {

// RGB565
constexpr uint16_t COLOR_BG = 0x1082;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GRAY = 0xBDF7;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_CYAN = 0x07FF;

}  // namespace

void StatusDisplay::showConnecting()
{
    M5.Lcd.fillScreen(COLOR_BG);

    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, 15);
    M5.Lcd.println("WiFi");

    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 45);
    M5.Lcd.println("Connecting...");
}

void StatusDisplay::showConnectingProgress()
{
    M5.Lcd.print(".");
}

void StatusDisplay::drawStatusDot(int x, int y, bool state)
{
    M5.Lcd.fillCircle(
        x,
        y,
        5,
        state ? COLOR_GREEN : COLOR_RED
    );
}

void StatusDisplay::draw(const PcLink& link, const ServoController& servos)
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

    const int id = link.id();
    if (id >= 0) {
        if (id < 10) {
            M5.Lcd.print("0");
        }
        M5.Lcd.print(id);
    } else {
        M5.Lcd.print("--");
    }

    // Wi-Fi
    const bool wifiConnected = link.isWiFiConnected();

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.setCursor(10, 33);
    M5.Lcd.print("WiFi");

    drawStatusDot(40, 36, wifiConnected);

    // PC
    M5.Lcd.setCursor(60, 33);
    M5.Lcd.setTextColor(COLOR_GRAY);
    M5.Lcd.print("PC");

    drawStatusDot(82, 36, link.isPcConnected());

    // IP
    M5.Lcd.setTextColor(COLOR_CYAN);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 46);

    if (wifiConnected) {
        M5.Lcd.print(WiFi.localIP().toString());
    } else {
        M5.Lcd.print("No WiFi");
    }

    // KRS: 2列 x 3行, "<id>:F" (free) / "<id>:P<pos>" (position)
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, 59);
    M5.Lcd.print("Servo");

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            const int servoId = i * 2 + j;

            M5.Lcd.setCursor(
                10 + 50 * j,
                69 + 10 * i
            );

            M5.Lcd.print(servoId);

            if (servos.mode(servoId) == ServoController::Mode::Free) {
                M5.Lcd.print(":F");
            }
            else {
                M5.Lcd.print(":P");
                M5.Lcd.print(servos.position(servoId));
            }
        }
    }
}
