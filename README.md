# mocube_ws

mocube というモジュラロボットを制御するための ROS 2 ワークスペースです。
各モジュールは M5Stack AtomS3 + KRS サーボ + 赤外線送受信器でできていて、
Wi-Fi (UDP) で PC 上の ROS 2 ノードとつながります。

- 開発環境: Ubuntu 24.04 / ROS 2 Jazzy (WSL2 でも動作)
- ファームウェア: PlatformIO (Arduino framework, ESP32-S3)

## ドキュメント一覧

| 対象 | ドキュメント | 内容 |
|---|---|---|
| WS 全体 | この README | 構成、セットアップ、通信プロトコル、症状別の調べ方 |
| ファームウェア全体 | [firmware/README.md](firmware/README.md) | PlatformIO の環境構築・書き込み |
| モジュールのファーム | [firmware/mocube_main/README.md](firmware/mocube_main/README.md) | クラス構成、起動/ループの流れ、赤外線デコード、既知の落とし穴 |
| ROS ブリッジ | [src/atom_krs_ros_udp_bridge/README.md](src/atom_krs_ros_udp_bridge/README.md) | device_manager ノードの内部、パラメータ、台帳ファイル |
| メッセージ定義 | [src/atom_krs_ros_udp_bridge_msg/README.md](src/atom_krs_ros_udp_bridge_msg/README.md) | `/modules` の各フィールドの意味 |

## ディレクトリ構成

```
mocube_ws/
├── README.md
├── devices.json                 # MAC -> ID の台帳 (WS 直下で ros2 run したときに使われる)
├── firmware/                    # PlatformIO (COLCON_IGNORE 済み)
│   ├── setup_pio.sh / activate.sh
│   └── mocube_main/             # AtomS3 用ファーム
└── src/                         # ROS 2 パッケージ (どちらも git submodule)
    ├── atom_krs_ros_udp_bridge/      # device_manager ノード (Python)
    └── atom_krs_ros_udp_bridge_msg/  # メッセージ定義
```

`src/` の 2 つは別リポジトリ (submodule) なので、そこを変更したら
それぞれのリポジトリで commit/push してから、WS 側で submodule の参照を commit します。

## 全体像

```
 ┌───────────── AtomS3 モジュール (複数台) ─────────────┐
 │  PcLink ── Wi-Fi/UDP ──────────┐                      │
 │  ServoController ── ICS ── KRS │                      │
 │  NeighborDetector ── IR TX/RX ─┼─ 隣のモジュールと赤外線で ID を交換
 │  StatusDisplay ── LCD          │                      │
 └────────────────────────────────┼──────────────────────┘
          UDP :5000 (broadcast)   │   ▲ UDP :5000 (unicast 応答)
                                  ▼   │
 ┌────────────── PC (ROS 2) ─────────────────────────────┐
 │  device_manager ノード                                │
 │   ├ DeviceRegistry ── devices.json (MAC->ID を永続化) │
 │   └ publish ──> /modules (Modules.msg, 100 Hz)        │
 └───────────────────────────────────────────────────────┘
```

1. モジュールは起動すると Wi-Fi に繋ぎ、`REGISTER,<MAC>` をブロードキャストする
2. PC は MAC を台帳で引き (無ければ新しい ID を払い出して保存し)、`ID,<id>` を返す
3. モジュールは 1 秒ごとに `HEARTBEAT,<MAC>` を送り、PC は `ACK,<id>` を返す
   - PC: 3 秒 HEARTBEAT が来なければそのモジュールを offline にする
   - モジュール: 3 秒 ACK が来なければ画面の PC ランプを赤にする
4. モジュールは 100 ms ごとに自分の ID を赤外線ビーコンとして送る。
   隣のモジュールがそれを受信器 ch (0/1/2 = 面 u/r/f) で受けると
   `NEIGHBOR,<MAC>,<ch>,<相手のID>` を PC に送る
5. PC は全モジュールの状態を `/modules` に流し続ける

## セットアップ

```bash
git clone git@github.com:nalinally/mocube_ws.git
cd mocube_ws
git submodule update --init --recursive
colcon build
source install/setup.bash
```

ファームウェアの環境構築と書き込みは [firmware/README.md](firmware/README.md)。
Wi-Fi の SSID / パスワードは `firmware/mocube_main/include/WifiSecrets.h` に書きます (git 管理外)。

## 起動

```bash
cd mocube_ws          # devices.json はカレントディレクトリから読み書きされる
ros2 run atom_krs_ros_udp_bridge device_manager
ros2 topic echo /modules
```

モジュールの電源を入れると、画面に ID が表示され、PC ランプが緑になります。

## 通信プロトコル (UDP, ポート 5000)

1 パケット = カンマ区切りの ASCII 1 行。定義は
ファーム側 `firmware/mocube_main/src/Protocol.h`、PC 側
`src/atom_krs_ros_udp_bridge/atom_krs_ros_udp_bridge/protocol.py`。

### モジュール -> PC (サブネットへのブロードキャスト)

| パケット | 周期 / 契機 | PC の処理 |
|---|---|---|
| `REGISTER,<MAC>` | 起動時 + ID 未取得の間 2 秒ごと | 台帳登録、`ID,<id>` を返信 |
| `HEARTBEAT,<MAC>` | 1 秒ごと | 未登録なら登録、`ACK,<id>` を返信、online に |
| `NEIGHBOR,<MAC>,<ch>,<address>` | IR ビーコンを受けるたび (1 送信元あたり最大約 10 Hz/面) | 面 `FACES[ch]` の隣接を `address` に更新 |

- `<MAC>` は `DC:54:75:CB:BF:2C` 形式 (PC 側で大文字化)
- `<ch>` は 0, 1, 2 (= 面 `u`, `r`, `f`)。`<address>` は相手の ID (uint8、相手が ID 未取得なら 255)
- ブロードキャストなので他のモジュールにも届くが、モジュール側は未知のコマンドとして無視する

### PC -> モジュール (送信元 IP:ポートへのユニキャスト)

| パケット | 説明 | 処理するクラス |
|---|---|---|
| `ID,<id>` | REGISTER への応答。モジュールはこの ID を採用する | `PcLink` |
| `ACK,<id>` | HEARTBEAT への応答。自分の ID と一致したときだけ PC 接続中とみなす | `PcLink` |
| `KRS,setPos,<servo>,<pos>` | サーボ (ICS ID 0..5) を位置指令 (KRS の範囲 3500..11500, 中央 7500) | `ServoController` |
| `KRS,setFree,<servo>` | サーボを脱力 | `ServoController` |
| `IR,send,<raw>` | 32bit NEC フレームを送信 (`0x` 付き 16 進可) | `NeighborDetector` |

`KRS` / `IR` は現在 device_manager からは送っていません。手で試すときは例えば:

```bash
echo -n "KRS,setPos,0,7500" | nc -u -w1 <モジュールのIP> 5000
```

## 新しい機能を追加するとき

- **PC -> モジュールの新コマンド**: ファームで機能クラスに `handleCommand(const protocol::Packet&)` を作り、
  `main.cpp` で `pcLink.onCommand("XXX", ...)` に登録。`Protocol.h` とこの README の表に追記
- **モジュール -> PC の新コマンド**: ファームの `Protocol.h` に `makeXxx()` を追加して `pcLink.broadcast()`。
  PC 側は `protocol.py` の `Command` に名前を足し、`device_manager.py` の `_handlers` にハンドラを登録
- **`/modules` に情報を足す**: `Module.msg` 等にフィールドを追加 → `device_registry.Device` に状態を追加
  → `device_manager.to_modules_msg()` で詰める
- **ピン・周期・ポートの変更**: ファームは `src/Config.h`、PC は device_manager の ROS パラメータ

## 症状から原因を探す

詳しくは各パッケージの README の「デバッグ」節。まずはここで当たりをつけてください。

| 症状 | まず疑うところ |
|---|---|
| モジュール画面が `Connecting...` のまま | SSID / パスワード (`WifiSecrets.h`)。2.4 GHz の AP か。`PcLink::connectWiFi` は繋がるまで永久に待つ |
| 画面の ID が `--` のまま | PC に REGISTER が届いていない / 返信が届いていない。device_manager が動いているか、PC とモジュールが同じサブネットか、PC の FW で UDP 5000 が開いているか。WSL2 なら mirrored ネットワークモードが必要 (NAT だと LAN のブロードキャストが届かない) |
| ID は出るが PC ランプが赤のまま | ACK の ID とモジュールの ID が不一致。PC の台帳 (`devices.json`) を消した / 別ディレクトリで起動したため、モジュールが古い ID を持ったまま PC が別 ID を払い出している。モジュールをリセットすれば直る (下記「既知の問題」) |
| 新しいモジュールに既存モジュールと同じ ID が付いた | device_manager を別のカレントディレクトリで起動し、別の `devices.json` が使われている |
| `/modules` で online が true/false を行き来する | HEARTBEAT の取りこぼし (Wi-Fi 品質)。PC 側 timeout 3 秒 / 送信 1 秒なので 3 回連続で落ちると offline |
| receivers の address がずっと -1 | 隣接モジュールのビーコンが届いていない。IR の向き・距離、相手の TX (G1)、自分の RX ピン (G8/G38/G39)。シリアルモニタに `IR RX` ログが出ているか |
| receivers の address が 255 | 相手モジュールがまだ ID を取得していない |
| receivers の address が自分の ID | 自分の IR TX の反射を自分の RX が受けている |
| 隣接を外しても receivers が古い値のまま | 仕様。隣接情報は上書きされるだけで消えない。`t` (受信時刻) を見て古さを判定する |
| サーボが動かない | `KRS` パケットが届いているか、ICS 配線 (G5/G6/G7)、サーボ ID (0..5)。画面の Servo 欄は「送った指令」なので動いたかどうかは分からない |
| device_manager が起動直後に落ちる | `devices.json` が壊れている (JSON エラー)、または UDP 5000 を他のプロセスが使っている (`Address already in use`) |

## 既知の問題 / 仕様上の注意

- PC の台帳を消すと、既に ID を持っているモジュールは再 REGISTER しない (ID 取得済みだと REGISTER を送らない)。
  PC は HEARTBEAT から新しい ID で再登録して `ACK,<新ID>` を返すが、モジュールは ID 不一致で無視するので PC ランプが赤のままになる。モジュールをリセットすると直る
- `devices.json` は新しいモジュールを登録したときだけ保存される。中の `addr` / `last_seen` / `online` は保存時点の値で、起動時には読まれない
- Wi-Fi のパスワードは過去の commit 履歴には残っている
