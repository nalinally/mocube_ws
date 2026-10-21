# firmware

mocube のモジュール (M5Stack AtomS3) 用ファームウェア置き場です。
`COLCON_IGNORE` を置いてあるので colcon build の対象にはなりません。

| ディレクトリ / ファイル | 内容 |
|---|---|
| [mocube_main/](mocube_main/README.md) | モジュール本体のファーム (PlatformIO プロジェクト) |
| `setup_pio.sh` | PlatformIO を `firmware/venv_pio/` にインストールする (初回のみ) |
| `activate.sh` | その venv を有効化する (`source firmware/activate.sh`) |
| `venv_pio/` | PlatformIO 本体 (git 管理外) |

## 環境構築 (初回のみ)

```bash
./firmware/setup_pio.sh
cp firmware/mocube_main/include/WifiSecrets.example.h firmware/mocube_main/include/WifiSecrets.h
# WifiSecrets.h に SSID / パスワードを書く
```

`WifiSecrets.h` が無いとビルドが `#error` で止まります。

## ビルド・書き込み・シリアルモニタ

```bash
source firmware/activate.sh
cd firmware/mocube_main
pio run                      # ビルド
pio run -t upload            # 書き込み (USB 接続)
pio device monitor           # シリアルモニタ (115200 bps)
```

- ボード設定・ライブラリのバージョンは `mocube_main/platformio.ini`
  (M5Unified 0.1.13 / M5AtomS3 1.0.0 / FastLED 3.6.0 に固定)
- WSL2 から書き込むときは、AtomS3 の USB を WSL に渡しておく必要があります (usbipd など)。
  `pio device list` にポートが出なければここを確認
- 書き込みに失敗する場合は AtomS3 のリセットボタンを長押ししてダウンロードモードにしてから再実行

## 複数台に書き込むとき

全モジュールが同じファームです。ID は PC (device_manager) が MAC アドレスから払い出すので、
モジュールごとにファームを変える必要はありません。
