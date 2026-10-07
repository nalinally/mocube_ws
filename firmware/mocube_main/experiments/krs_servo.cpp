// #include <M5Unified.h>

// void setup() {
//   auto cfg = M5.config();
//   M5.begin(cfg);

//   M5.Display.setTextSize(2);
//   M5.Display.setCursor(10, 10);
//   M5.Display.println("Hello, AtomS3!");
// }

// void loop() {
// }

//
//  @file KrsServo2.ino
//  @brief KrsServoSample2
//  @author Kondo Kagaku Co.,Ltd.
//  @date 2025/1/8
//
//  ID:0のサーボをポジション指定で動かす
//  範囲は、左5500 - 中央7500 - 右9500
//  0.5秒ごとに指定数値まで動く
//  ICSの通信にはHardwareSerialを使います。
//

#include <M5Unified.h>
#include <IcsHardSerialClass.h>

#define ICS_TX G5
#define ICS_RX G6
#define ICS_EN G7

#define BAUDRATE 115200
#define TIMEOUT 1000

IcsHardSerialClass krs(
    &Serial1,
    ICS_EN,
    BAUDRATE,
    TIMEOUT
);

void setup() {

  Serial1.begin(
      BAUDRATE,
      SERIAL_8E1,
      ICS_RX,
      ICS_TX
  );

  krs.begin();  //サーボモータの通信初期設定

  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setTextSize(2);

}

void loop() {

  krs.setPos(1,7500);      //位置指令　ID:0サーボを7500へ 中央
  M5.Display.setCursor(10, 10);
  M5.Display.println("move to 7500");
  delay(500);              //0.5秒待つ

  krs.setPos(1,9500);      //位置指令　ID:0サーボを9500へ 右
  M5.Display.setCursor(10, 10);
  M5.Display.println("move to 9500");
  delay(500);              //0.5秒待つ

  krs.setPos(1,7500);      //位置指令　ID:0サーボを7500へ 中央
  M5.Display.setCursor(10, 10);
  M5.Display.println("move to 7500");
  delay(500);              //0.5秒待つ

  krs.setPos(1,5500);      //位置指令　ID:0サーボを5500へ 左
  M5.Display.setCursor(10, 10);
  M5.Display.println("move to 5500");
  delay(500);  
}