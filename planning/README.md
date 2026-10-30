# planning: 変形計画の試作

[modular-robotics](https://github.com/Modular-Robotics-Group/modular-robotics) の Pathfinder で mocube の変形手順を探し、サーボ指令に変換します。

## mocube の動きの定義 (`moves/Mocube.json`)

```
?!
##
```

`?` が動くモジュール、`!` が移動先、`#` が必要な支えです。`permGen` で全方向に展開されます。

- 支えは移動の前後とも同じ側に必要です。角の回り込みはできません
- 移動の向きの前後に接触があってもかまいません。面に垂直な向きはサーボで動かせませんが、拘束もされません

## 使い方

Pathfinder のビルドには `nlohmann-json3-dev` と Boost が必要です。実行はビルドディレクトリの中で行います (`Module Properties/` を相対パスで読むため)。

```bash
cd <Pathfinder のビルドディレクトリ>
./Pathfinder -i -s 'A*' -m <planning>/moves \
  -I <planning>/scenarios/l_wall_to_floor_initial.json \
  -F <planning>/scenarios/l_wall_to_floor_final.json \
  -e <planning>/results/l_wall_to_floor.scen

cd <planning>
python3 plan_to_commands.py scenarios/l_wall_to_floor_initial.json results/l_wall_to_floor.scen \
  -o results/l_wall_to_floor_commands.json
python3 render_plan.py results/l_wall_to_floor_commands.json -o results/l_wall_to_floor.gif
```

`plan_to_commands.py` は各手が mocube の動きとして実行できるかを検査し、`mocube_estimator` の積算で再生して指令どおりに動くかも確かめます (誤差が 1 mm を超えると異常終了)。

## 制限

- 1 手で動くのは 1 台だけです。塊ごとの移動は表せません
- 移動先にも同じ軸の座標の支えが必要なので、全体の外形サイズは変わりません
- 重力は考えていません
