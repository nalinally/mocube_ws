# mocube_sim

`device_manager` と実機のモジュールの代わりになるシミュレータです。
`/servo_commands` を受けて、`device_manager` と同じ形の `/modules` を出します。
実機なしで `state_estimator`・`move_server`・`plan_executor` などのロジックをデバッグするのに使います。

```
 実機:            /servo_commands → device_manager → UDP → モジュール (サーボ・赤外線)
                                                               ↓
                  /modules        ← device_manager ← UDP ←────┘
 シミュレータ:    /servo_commands → module_sim (本当の位置を持つ) → /modules
```

`mocube_planner` の `sim_modules` との違いは、**推定とは別に本当の位置を持つ** ことです。
`sim_modules` は `state_estimator` の推定位置を正解とみなして赤外線を作るので、推定のまちがいが表に出ません。
`module_sim` はサーボ指令だけから本当の位置を動かし、その位置から赤外線を作ります。

## 使い方

```bash
ros2 run mocube_sim module_sim --ros-args -p layout_file:=plan.json
```

`layout_file` は初期配置です。次のどちらかを渡します。

- YAML の `ID: [x, y, z]` (`state_estimator` の `initial_positions_file` と同じ形)。例は `config/three_modules.example.yaml`
- `mocube_planner` の `plan` で作った計画の JSON (`initial` を使う)

実機のモジュールと混ざらないよう、`ROS_DOMAIN_ID` を変えるか、`device_manager` を止めておきます。

### 計画を通しで試す

```bash
export ROS_DOMAIN_ID=77   # 全ターミナルで同じ値
ros2 run mocube_sim module_sim --ros-args -p layout_file:=plan.json
ros2 run mocube_estimator state_estimator
ros2 run mocube_executor move_server
ros2 run mocube_planner plan_executor --ros-args -p plan_file:=plan.json
```

`state_estimator` には初期配置を与えなくても、実機と同じく赤外線で見えた順に置かれます。

推定と本当の速さがずれたときの動きを試すには、`speed_gain_mm` を `state_estimator` と違う値にします。

```bash
ros2 run mocube_sim module_sim --ros-args -p layout_file:=plan.json -p speed_gain_mm:=0.25
```

### RViz で見る (まとめて起動)

`launch/demo.launch.py` で、`module_sim`・`state_estimator`・`move_server`・RViz を起動し、
`plan_delay` 秒後に `plan_executor` が計画を実行します。`plan_file` は絶対パスで渡します。

```bash
ros2 launch mocube_sim demo.launch.py plan_file:=$PWD/plan.json ir_range:=0.02
```

| 引数 | 既定値 | 内容 |
|---|---|---|
| `plan_file` | (必須) | 計画の JSON。初期配置にも使う |
| `ir_range` | 0.1 | `module_sim` の `ir_range`。0.1 では `l_wall_to_floor` が 3 手目で失敗する (下の「試した結果」) |
| `sim_speed_gain_mm` | 0.2 | `module_sim` の `speed_gain_mm` (推定は 0.2 のまま) |
| `run_plan` | true | false なら `plan_executor` を起動しない (別のターミナルで手で起動する) |
| `plan_delay` | 5.0 | `plan_executor` を起動するまでの時間 [s] |
| `rviz` | true | RViz を起動するか |

RViz の表示 (`rviz/mocube_sim.rviz`):

| 表示 | 意味 |
|---|---|
| 灰色の立方体 | 本当の位置 (止まっている)。濃い灰色が ID0 |
| 橙の立方体 | 本当の位置 (動いている) |
| 白い数字 | ID |
| 水色の枠線 | 推定の位置 (`/module_states`)。立方体からずれていれば推定がずれている |
| TF (既定で非表示) | 推定の `mocube_<ID>` |

マーカーは `/sim/markers` (`visualization_msgs/MarkerArray`) に `true_state_period` ごとに出します。

### 本当の位置を見る・動かす

```bash
ros2 topic echo /sim/true_states        # 本当の位置。/module_states (推定) と比べる
ros2 service call /sim/set_grid_positions mocube_msgs/srv/SetGridPositions \
  "{positions: [{id: 1, x: 1, y: 0, z: 1}]}"   # 本当の位置を格子点へ置き直す (押して直した、の代わり)
```

## 実機と同じにしているところ

| 項目 | ふるまい | 元にしたもの |
|---|---|---|
| 指令の検査 | 範囲外の角度・サーボ番号外・不明なモードは warning を出して捨てる。台帳にない ID も warning | `device_manager` の `to_servo_packet` をそのまま呼ぶ |
| 指令の解釈 | `setPos` / `setPosWd` / `setFree` / `setFreeAll`。角度は `{:g}` で文字列にしたものを読むので丸めも同じ | ファームの `ServoController` の写し (`firmware.py`) |
| ウォッチドッグ | `setPosWd` の更新が 1 秒途絶えると 0 (停止)。0 を送るか `setPos` を送ると監視をやめる | 同上 |
| 電源投入直後 | 全サーボ脱力 | 同上 |
| オンライン | 1 秒ごとに HEARTBEAT が届き、`last_seen` が進む。起動から最初の HEARTBEAT までは offline | ファームの `PcLink`、`device_registry` |
| 赤外線 | 各モジュールが 0.1 秒ごとにビーコンを送り、正の側のモジュールの d / l / b 面の LED を、負の側の u / r / f 面の受信器が受ける。受けた時刻で `receivers` の `address` と `t` を上書きし、消さない | `NeighborDetector`、`device_registry` |
| `/modules` | 0.01 秒ごと。ID 順。`mac` は `02:00:00:00:00:<ID>`、`addr` は `127.0.0.1` | `device_manager` |

## 動きのモデル

位置推定 (`mocube_estimator`) と同じ理想モデルで動かします (計算には `Estimator` を使います)。

- 接している組の相対速度を両側のサーボの回転指令から決め、ID0 を固定して最小二乗で解く
- 面に垂直な向きは拘束しない。脱力と 0 は止まっている
- 速さは回転指令に比例し、係数は `speed_gain_mm` (**`state_estimator` とは別に設定**)

ばらつき、衝突、かみ合いの外れ、通信の遅れ・取りこぼしは入れていません。
このため、推定とシミュレータで `speed_gain_mm` が同じなら、赤外線の補正がなくても推定は本当の位置と一致します。
動きのモデルそのもの (どちらの塊が動くか、など) が実機と違っていても、このシミュレータでは見つかりません。

## パラメータ

| 名前 | 既定値 | 内容 |
|---|---|---|
| `layout_file` | (必須) | 初期配置 (YAML または計画の JSON) |
| `edge_length_mm` | 113.1 | モジュールの一辺 [mm] |
| `speed_gain_mm` | 0.2 | 回転指令 1 deg あたりの本当の相対移動速度 [mm/s]。`state_estimator` と別 |
| `ir_range` | 0.1 | 赤外線を受ける、向かい合う面の中心のずれの上限 (一辺に対する割合)。実機では未測定 |
| `contact_tolerance` | 0.1 | 接触判定の許容 (一辺に対する割合)。`state_estimator` と同じ意味 |
| `beacon_period` | 0.1 | 赤外線ビーコンの周期 [s] (ファームの `IR_BEACON_INTERVAL_MS`) |
| `heartbeat_period` | 1.0 | HEARTBEAT の周期 [s] (ファームの `HEARTBEAT_INTERVAL_MS`) |
| `timeout` | 3.0 | HEARTBEAT が途絶えてから offline にするまで [s] (`device_manager` の `timeout`) |
| `watchdog` | 1.0 | `setPosWd` のウォッチドッグ [s] (ファームの `KRS_WATCHDOG_MS`) |
| `publish_period` | 0.01 | 位置を進めて `/modules` を出す周期 [s] |
| `true_state_period` | 0.05 | `/sim/true_states` と `/sim/markers` を出す周期 [s] |
| `world_frame` | `world` | マーカーのフレーム (`state_estimator` の `world_frame` と同じ) |

## トピック・サービス

| 名前 | 型 | 向き |
|---|---|---|
| `/servo_commands` | `mocube_msgs/ServoCommands` | 購読 |
| `/modules` | `mocube_msgs/Modules` | 出力 |
| `/sim/true_states` | `mocube_msgs/ModuleStates` | 出力 (本当の位置。`grid_*` は最も近い格子点、`localized` は常に true) |
| `/sim/set_grid_positions` | `mocube_msgs/srv/SetGridPositions` | サービス (本当の位置を置き直す) |
| `/sim/markers` | `visualization_msgs/MarkerArray` | 出力 (RViz 用) |
| `/module_states` | `mocube_msgs/ModuleStates` | 購読 (表示の枠線にだけ使う) |

## 構成

| ファイル | 内容 |
|---|---|
| `firmware.py` | ファームの `ServoController` の写し (パケットの解釈とウォッチドッグ) |
| `world.py` | 本体 (`World`)。本当の位置、赤外線、台帳。ROS に依存しないので単体テストできる |
| `markers.py` | RViz 用のマーカー |
| `module_sim.py` | ROS ノード |
| `launch/demo.launch.py` | 全体を起動して RViz で見る |
| `rviz/mocube_sim.rviz` | RViz の設定 |

```bash
colcon test --packages-select mocube_sim && colcon test-result --verbose
```

## 試した結果 (2026-11-04)

`l_wall_to_floor` の 10 手を、上の 4 ノードを別プロセスで動かして試しました (速度係数は推定と同じ 0.2)。

| `ir_range` | 結果 | 終わったときの本当の位置 |
|---|---|---|
| 0.1 (既定) | 3 手目で `FAILED` (赤外線の確認が取れない) | — |
| 0.02 | 10 手とも成功 | 目標の格子点から 1.5 mm 以内 |

`ir_range` = 0.1 での失敗の流れ:

1. `move_server` は「新しい接触の赤外線を受け、最も近い格子点が目的の点」になった時点で止める。
   赤外線は格子点の約 11 mm 手前から受かるので、推定は格子点に揃えられるが、本当は手前で止まる
2. 2 手目で ID10 が本当は格子点の約 18 mm 上で止まった
3. 3 手目で ID11 がその上に降り、`10.u ← 11` を受けた。推定は ID10 を格子点にいるとみなしているので、
   ID11 も約 18 mm 手前で「着いた」ことになり、止まった。期待した赤外線 (`1.r ← 11`, `11.r ← 13`) は届かず失敗

`mocube_executor` の制限「赤外線が格子点の手前で受かると、その場で止まります」が、手をまたいで積み上がって起きます。
実機での赤外線の受かる範囲を測り、`ir_range` に入れて確かめる必要があります。

## 制限

- 時間は実時間 (壁時計) で進みます。速く回すことはできません
- `IR,send` (任意フレームの送信) は受けても何もしません
- モジュールの抜き差し、ID の払い出し (REGISTER)、台帳ファイル `devices.json` は扱いません。全モジュールが登録済みで始まります
