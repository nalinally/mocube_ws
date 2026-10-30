# device_check

mocube モジュール (M5Stack AtomS3) のデバイス確認用ファームウェアです。
Wi-Fi / PC には繋がず、モジュール単体で KRS サーボ・赤外線受信 3ch・赤外線送信・インジケータ LED の動作を確認します。
ビルド・書き込みは [../README.md](../README.md) と同じで、ディレクトリだけ `firmware/device_check` にします
(`WifiSecrets.h` は不要)。

```bash
source firmware/activate.sh
cd firmware/device_check
pio run -t upload && pio device monitor
```

## ファイル構成

```
device_check/
├── platformio.ini          # mocube_main と同じボード・ライブラリ。ICS ライブラリは ../mocube_main/lib を参照
└── src/
    ├── main.cpp            # 確認モード・シリアルコマンド・画面
    ├── Config.h            # mocube_main/src/Config.h と同じ形式・同じ内容 (ピンはここから読む)
    ├── NecFormat.h              # 以下 3 つは mocube_main/src からのコピー
    ├── IrNecReceiver.h/.cpp
    └── IrNecTransmitter.h/.cpp
```

`Config.h` は mocube_main と同じものにしておくことで、配線を変えたときに
`cp ../mocube_main/src/Config.h src/` (または逆向き) するだけで両方を合わせられます。
確認ファーム専用の値 (往復角度・点滅周期など) は `main.cpp` 先頭の定数にあります。

## 使い方

画面のボタン (BtnA) を押すとモードが SERVO → IR → LED → SERVO … と切り替わります。

| モード | 動作 | 画面 |
|---|---|---|
| SERVO | ICS ID 0..`KRS_SERVO_COUNT`-1 を 1 台ずつ +30° → -30° → 0° → 脱力 (700 ms 間隔) | ID ごとに `OK` + 応答の角度 / `NG` (無応答) / `--` (未実施)。`>` が今動かしている ID |
| IR | 100 ms ごとに address=0xA5, command=送信回数 の NEC フレームを送信。受信は全モードで常時 | 送信回数と、ch ごと (u/r/f) の受信数・最後の `address:command`。500 ms 以内に受けた ch は緑 |
| LED | インジケータ LED を 500 ms ごとに ON/OFF | ON / OFF |

モードを抜けるとき、動かしかけのサーボは脱力し、LED は消灯します。

- SERVO の `NG` は応答が返ってこなかったという意味です。サーボは動いていても、ICS の RX 配線や
  EN ピンに問題があると NG になります。無応答の ID では `KRS_TIMEOUT_MS` (1 秒) ブロックします
- IR は自分の送信を自分の受信器で受けられれば (反射や、受信器を送信 LED に向ける) 1 台で確認できます。
  2 台を両方 IR モードにして向かい合わせても確認できます (どちらも address=0xA5)。
  送信 LED が光っているかはスマホのカメラ越しに見ると分かります
- LED は単色 LED を `digitalWrite` で ON (HIGH) / OFF (LOW) するだけです

## シリアルコマンド (115200 bps)

| コマンド | 内容 |
|---|---|
| `help` | コマンド一覧 |
| `pins` | `Config.h` のピン設定を表示 |
| `mode servo\|ir\|led` | モード切り替え (ボタンと同じ) |
| `pos <id> <deg>` | KRS `setPos` (±`KRS_MAX_DEG` まで) |
| `free <id>` / `freeall` | KRS `setFree` |
| `ir <addr> <cmd>` | NEC フレームを 1 回送信 (`0x..` / 10 進どちらも可) |
| `led on\|off` | インジケータ LED |

`pos` / `free` は SERVO モード中でも使えますが、自動の往復と混ざります。手で動かすときは IR か LED モードにしてください。

シリアルに出るもの:

| 出力 | 意味 |
|---|---|
| `KRS id=N: pos=P (D deg)` / `KRS id=N: no response` | サーボの応答 |
| `IR RXn (face, GPIOp): ADDR=.. CMD=.. DATA=..` | 受信器 n (1 始まり) でフレームを受信 |
| `IR RXn: INVALID DATA=..` | 32 bit 読めたが反転チェック NG |
| `IR TX ready: ...` / `rmt_... failed` | 送信 (RMT) の初期化結果 |
