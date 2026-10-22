# mocube_main

mocube の 1 モジュール (M5Stack AtomS3) で動くファームウェアです。
PC の device_manager と UDP でつながり、KRS サーボを動かし、赤外線で隣のモジュールを検出します。
ビルド・書き込みは [../README.md](../README.md)、通信プロトコルの一覧は [WS の README](../../README.md#通信プロトコル-udp-ポート-5000)。

## ファイル構成

```
mocube_main/
├── platformio.ini
├── include/
│   ├── WifiSecrets.example.h   # テンプレート
│   └── WifiSecrets.h           # SSID / パスワード (git 管理外, 自分で作る)
├── src/
│   ├── main.cpp                # 生成・初期化・loop の順番・コマンド登録だけ
│   ├── Config.h                # ピン、周期、ポートなどの定数 (全部ここ)
│   ├── Protocol.h/.cpp         # UDP パケットの分割・組み立て
│   ├── PcLink.h/.cpp           # Wi-Fi, UDP, ID 取得, heartbeat, コマンド振り分け
│   ├── ServoController.h/.cpp  # KRS サーボ (ICS)
│   ├── NeighborDetector.h/.cpp # 赤外線ビーコンによる隣接検出
│   ├── StatusDisplay.h/.cpp    # LCD 表示
│   ├── NecFormat.h             # NEC フレームの定数・エンコード・検証
│   ├── IrNecReceiver.h/.cpp    # 3ch 赤外線受信 (ISR + デコード)
│   └── IrNecTransmitter.h/.cpp # 赤外線送信 (RMT)
├── lib/IcsClass_V300/          # 近藤科学 ICS ライブラリ (KRS サーボ通信, 改変なし)
└── experiments/                # 単体の実験スケッチ (ビルド対象外, 過去の main のバックアップ含む)
```

## ハードウェア

| 用途 | ピン | 備考 |
|---|---|---|
| ICS TX / RX / EN | G5 / G6 / G7 | `Serial1`, 115200 bps, 8E1 |
| IR 受信 ch0 / ch1 / ch2 | G8 / G38 / G39 | PC 側の面 `u` / `r` / `f` に対応 |
| IR 送信 | G1 | RMT channel 0, 38 kHz, duty 33% |

変更は `src/Config.h` だけで済むようになっています。

## クラスの関係

```
main.cpp
 ├─ PcLink pcLink              … UDP の受信と ID/ACK 処理。それ以外のコマンドは onCommand で登録されたハンドラへ
 ├─ ServoController servos     … "KRS,..." を処理
 ├─ NeighborDetector neighbors … "IR,..." を処理。pcLink を参照して NEIGHBOR を送る / 自分の ID を読む
 │    ├─ IrNecReceiver   (ISR でエッジ記録 → update() でデコード → コールバック)
 │    └─ IrNecTransmitter (RMT でフレーム送信, ノンブロッキング)
 └─ StatusDisplay display      … pcLink と servos の状態を読んで描画するだけ
```

## 起動シーケンス (`setup()`)

1. `M5.begin()`, `Serial.begin(115200)`, 500 ms 待ち
2. `display.showConnecting()` → `pcLink.connectWiFi()` — **繋がるまでブロック** (500 ms ごとに `.` を表示)
3. `display.draw()` (ID は `--`)
4. `servos.begin()` — `Serial1` を G6/G5 で開いてから ICS ライブラリを初期化
5. `neighbors.begin()` — IR 受信の割り込み登録、RMT 初期化
6. `KRS` / `IR` コマンドのハンドラ登録
7. `pcLink.begin(5000)` — UDP を開き、最初の `REGISTER` を送る

## メインループ (`loop()`)

```
M5.update()
neighbors.updateReceiver()        ← 毎回。ビーコンを受けたらここから NEIGHBOR を送る
if Wi-Fi 切断:
    PC 接続を false にして再描画、1 秒 delay して return   ← この間ビーコン送信は止まる
pcLink.update(now)                ← UDP 1 パケット処理 / REGISTER / HEARTBEAT / ACK タイムアウト
    → 表示が変わったら display.draw()
neighbors.updateBeacon(now)       ← 100 ms ごとに自分の ID を IR 送信
```

| 周期 | 値 (`Config.h`) | 内容 |
|---|---|---|
| REGISTER | 2000 ms | ID 未取得 (`id() < 0`) の間だけ |
| HEARTBEAT | 1000 ms | 常に |
| PC タイムアウト | 3000 ms | 最後の `ID` / 一致する `ACK` からの経過 |
| IR ビーコン | 100 ms | address = 自分の ID (未取得なら 0xFF), command = 0x00 |
| Wi-Fi 切断時の待ち | 1000 ms | |

`display.draw()` は ID 受信時、ACK を受けるたび (= 約 1 秒ごと)、PC タイムアウト時、Wi-Fi 切断中の毎ループで呼ばれ、
毎回全画面を塗り直します。サーボ欄はサーボ指令を受けた瞬間には更新されず、次の ACK で更新されます。

## 各クラスの詳細

### PcLink

- `connectWiFi()` は `WiFi.begin()` 後、`WL_CONNECTED` になるまで無限ループ (タイムアウトなし)
- `begin()` で `WiFiUDP::begin(5000)` する。送信も同じソケットからなので送信元ポート = 5000、
  PC はそこへ返信してくる
- ブロードキャスト先は `IP | ~netmask` (例: 192.168.97.255)
- `update()` は **1 ループで 1 パケットしか読まない**。受信が溜まると処理が遅れる
- 受信バッファは 255 byte。長いパケットは切り捨て
- `ID,<id>`: 無条件にその ID を採用 (既に ID があっても上書き)
- `ACK,<id>`: `id() >= 0` かつ自分の ID と一致したときだけ PC 接続中に更新
- それ以外のコマンドは `onCommand()` の登録順に文字列比較して最初に一致したハンドラへ (最大 8 個)。
  どれにも一致しなければ無視 (他モジュールのブロードキャストもここに来る)

### Protocol

- `parse()` はカンマで分割して `Packet::fields[0..7]` に入れる。9 個目以降は捨てる
- `Packet::at(i)` は範囲外なら空文字列。`String::toInt()` は空文字列や数字でない文字列を 0 にするので、
  引数の欠け・typo は「0 番を指定した」ように見える点に注意

### ServoController

- `IcsHardSerialClass` を `Serial1` / EN=G7 / 115200 / timeout 1000 ms で持つ
- `setPos` / `setFree` はライブラリの戻り値 (応答の角度) を見ていない。応答が無いとき
  ライブラリ内で最大 timeout (1000 ms) ブロックし得て、その間 IR デコードも UDP も止まる
- 画面の `P<pos>` / `F` は「最後に送った指令」。サーボから読み返した値ではない
- ICS ライブラリの `begin()` は内部で `Serial1.begin(baud, SERIAL_8E1)` をピン指定なしで呼ぶ。
  現在の arduino-esp32 では既に設定したピンが保持されるが、コアのバージョンを古くすると
  デフォルトピンに戻ってサーボが無反応になる可能性がある

### NeighborDetector

- ビーコン: `nec::encode(自分のID, 0x00)` を 100 ms ごとに送信
- 受信: どの ch でも command が 0x00 なら `NEIGHBOR,<MAC>,<ch>,<address>` をブロードキャスト。
  受信のたびに送るので、見えている相手 1 台・1 面あたり最大約 10 パケット/秒になる
- 受信したフレームはすべてシリアルに `IR RX<ch+1>: ADDR=.. CMD=.. DATA=..` と出る (ch は 1 始まり表示)
- `IR,send,<raw>` で任意の 32bit を送れる (`strtoul(..., 0)` なので `0x..` も 10 進も可)

### IrNecReceiver (受信)

- 3 本のピンそれぞれに CHANGE 割り込み。ISR は「前のエッジからの時間」と「エッジ後のレベル」を
  ch ごとのリングバッファ (256 エントリ, 実質 255 = 約 3.7 フレーム) に積むだけ
- `update()` でバッファを取り出してステートマシンでデコード:

  | 状態 | 待っているもの | 許容範囲 |
  |---|---|---|
  | Idle | LOW になるエッジ (mark 開始) | |
  | LeaderMark | HIGH になるエッジ = 9 ms mark の終わり | 6.7 〜 11.2 ms |
  | LeaderSpace | LOW になるエッジ = 4.5 ms space の終わり | 3.4 〜 5.6 ms |
  | BitMark | HIGH になるエッジ = 562 us mark の終わり | 0 〜 1.12 ms |
  | BitSpace | LOW になるエッジ = space の終わり | 0 〜 1.12 ms → `0`, 1.12 〜 2.25 ms → `1` |

  範囲外ならその場で Idle に戻る。15 ms 以上エッジ間隔が空いたらリセット。
  32 bit 揃ったら address/command の反転チェック (`nec::isValid`) をして、NG なら
  `IR RX<n>: INVALID DATA=...` をシリアルに出して捨てる
- 1 フレーム = 68 エッジ。バッファは 3 フレーム分以上あるので、`update()` が約 200 ms 止まっても
  取りこぼさない。それ以上 loop を止める処理 (サーボの応答待ち、全画面描画の多発、`delay`) を足すと
  溢れたエッジが捨てられ、そのフレームはデコード失敗になる
- ISR から呼ぶために static なインスタンスポインタを使っているので、`IrNecReceiver` は 1 個しか作れない

### IrNecTransmitter (送信)

- ESP32 の RMT (channel 0, 1 tick = 1 us) で搬送波 38 kHz をハードウェア生成
- `send()` は `rmt_write_items(..., wait_tx_done=false)` で即座に戻る。ただし前のフレームの送信中に呼ぶと
  その完了を待つ
- ビット順は **MSB first** (`NecFormat.h` のコメント参照)。受信側も MSB first で詰めているので
  モジュール同士では一致する

### NecFormat

32bit フレーム = `[~command][command][~address][address]` (上位から)。
`encode()` / `isValid()` / `addressOf()` / `commandOf()` を送受信で共有しています。

## デバッグ

シリアルモニタ (`pio device monitor`, 115200 bps) に出るもの:

| 出力 | 意味 |
|---|---|
| `.....` → `WiFi connected!` `IP:` `MAC:` | Wi-Fi 接続 |
| `IR TX ready: GPIO=1, carrier=38000 Hz` | RMT 初期化成功。`rmt_config failed` / `rmt_driver_install failed` ならビーコンは出ていない |
| `IR receiver/transmitter ready` | IR 初期化完了 |
| `Assigned ID: N` | PC から ID を受け取った |
| `PC heartbeat OK` | ACK 受信 (約 1 秒ごと) |
| `PC connection lost` | 3 秒 ACK が来ない |
| `IR RXn: ADDR=0x.. CMD=0x.. DATA=0x........` | 受信器 n (1 始まり) でフレームを受信 |
| `IR RXn: INVALID DATA=0x........` | 32bit 読めたが反転チェック NG (ノイズ、別の送信機、ビット化け) |
| `IR TX: 0x........` / `IR TX: failed` | `IR,send` コマンドの結果 |
| `PcLink: too many handlers` | `onCommand` の登録数オーバー (`PcLink::MAX_HANDLERS`) |

切り分けのヒント:

- **ID が取れない**: シリアルに `Assigned ID` が出ないなら PC から `ID` が届いていない。PC 側で
  `sudo tcpdump -i any -n udp port 5000` を見て REGISTER が届いているか確認
- **IR が全く受からない**: `IR RX` も `INVALID` も出ないならエッジ自体が来ていない (配線・受信器の電源・向き)。
  `INVALID` ばかりならタイミングずれ (loop が止まっている、別機器のノイズ)
- **IR を受けても PC に NEIGHBOR が出ない**: command が 0x00 以外 (ビーコン以外) か、Wi-Fi 切断中
- **ループが遅くなった気がする**: 何かがブロックしている。サーボの無応答 (最大 1 秒)、Wi-Fi 切断中の `delay(1000)`、
  `rmt_write_items` の送信完了待ち (送信周期を 100 ms より短くした場合)
