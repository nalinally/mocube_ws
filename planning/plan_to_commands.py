#!/usr/bin/env python3
"""
Pathfinder の出力 (.scen) を mocube の動きとして検査し、サーボ指令の列に変換する.

各手について次を確かめ、最後に mocube_estimator の積算で指令どおりに動くかを再生する。
- 移動先が空いていて、移動の前後とも同じ側に支えがある
- ID0 が動かず、全体がつながったまま

動くモジュールが接触しているすべての面で、同じ相対移動になるようサーボを回す。
モジュールの ID は初期形状ファイルの並び順で、static のモジュールが ID0。
"""

import argparse
import json
from pathlib import Path
import sys

import numpy as np

sys.dont_write_bytecode = True  # submodule の中に __pycache__ を作らない
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src' / 'mocube_estimator'))
from mocube_estimator.estimator import Estimator  # noqa: E402
from mocube_estimator.geometry import DRIVES, FACES, NEGATIVE_FACE, POSITIVE_FACE  # noqa: E402

EDGE_LENGTH = 0.1131  # state_estimator の edge_length_mm
SPEED_GAIN = 0.0002   # state_estimator の speed_gain_mm [m/s/deg]
MAX_REPLAY_ERROR = 0.001  # [m]
SIDES = [tuple(int(v) for v in s * np.eye(3, dtype=int)[a]) for a in range(3) for s in (1, -1)]


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def load_initial(path):
    """初期形状を {ID: 座標} で返す. static のモジュールが ID0 になり、その座標が原点."""
    modules = json.loads(Path(path).read_text())['modules']
    modules.sort(key=lambda m: not m.get('static', False))
    if sum(m.get('static', False) for m in modules) != 1:
        raise ValueError('static のモジュール (ID0) はちょうど 1 台にしてください')
    origin = modules[0]['position']
    return {i: tuple(p - o for p, o in zip(m['position'], origin)) for i, m in enumerate(modules)}


def parse_scen(path):
    """(.scen の ID -> 座標, [(.scen の ID, 移動量), ...]) を返す."""
    cells, moves = {}, []
    for line in Path(path).read_text().splitlines():
        fields = [f.strip() for f in line.split(',')]
        if len(fields) != 5:
            continue
        if fields[0].startswith('*'):
            moves.append((int(fields[0][1:]), tuple(int(v) for v in fields[2:])))
        elif fields[0].isdigit() and len(fields[0]) > 0 and not moves:
            cells[int(fields[0])] = tuple(int(v) for v in fields[2:])
    return cells, moves


def is_connected(cells):
    cells = set(cells)
    stack = [next(iter(cells))]
    seen = {stack[0]}
    while stack:
        cell = stack.pop()
        for side in SIDES:
            neighbor = add(cell, side)
            if neighbor in cells and neighbor not in seen:
                seen.add(neighbor)
                stack.append(neighbor)
    return len(seen) == len(cells)


def check_move(positions, module, step):
    """mocube で実行できない手なら ValueError."""
    occupied = {c: i for i, c in positions.items()}
    source = positions[module]
    target = add(source, step)
    if module == 0:
        raise ValueError('ID0 は動かせません')
    if sorted(map(abs, step)) != [0, 0, 1]:
        raise ValueError(f'1 軸 1 マスの移動ではありません: {step}')
    if target in occupied:
        raise ValueError(f'ID{module}: 移動先がふさがっています')
    sides = [s for s in SIDES if np.dot(s, step) == 0]
    if not any(add(source, s) in occupied and add(target, s) in occupied for s in sides):
        raise ValueError(f'ID{module}: 移動の前後で同じ側の支えがありません')
    if not is_connected(c for c in occupied if c != source):
        raise ValueError(f'ID{module}: 動かすと全体が分かれます')


def servo_commands(positions, module, step, speed_deg):
    """{(ID, サーボ): 回転指令 [deg]} を返す. 移動の前後で接触する面をすべて回す."""
    occupied = {c: i for i, c in positions.items()}
    source = positions[module]
    commands = {}
    for side in SIDES:
        if np.dot(side, step) != 0:
            continue
        axis = int(np.flatnonzero(side)[0])
        for cell in (source, add(source, step)):
            other = occupied.get(add(cell, side))
            if other is None:
                continue
            lower, upper = (module, other) if side[axis] > 0 else (other, module)
            # lower に対する upper の速度の向き
            relative = -np.array(step) if side[axis] > 0 else np.array(step)
            lower_face, upper_face = POSITIVE_FACE[axis], NEGATIVE_FACE[axis]
            if abs(np.dot(DRIVES[lower_face], relative)) > 0.5:
                key = (lower, lower_face)
                angle = speed_deg * np.dot(DRIVES[lower_face], relative)
            else:
                # upper のサーボは lower を動かすので符号が逆になる
                key = (upper, upper_face)
                angle = -speed_deg * np.dot(DRIVES[upper_face], relative)
            if commands.setdefault(key, float(angle)) != float(angle):
                raise ValueError(f'サーボ {key} への指令が食い違っています')
    return commands


def replay(initial, steps):
    """指令を積算し、各手の後の位置と格子のずれの最大値 [m] を返す."""
    estimator = Estimator(EDGE_LENGTH, SPEED_GAIN)
    estimator.update_modules([(i, True) for i in initial])
    estimator.set_grid_positions(initial, 0.0)
    worst = 0.0
    for step in steps:
        for command in step['commands']:
            estimator.set_servo(command['module'], command['servo'], command['angle_deg'])
        ticks = 1000
        for _ in range(ticks):
            estimator.step(step['duration_s'] / ticks)
        for command in step['commands']:
            estimator.set_servo(command['module'], command['servo'], None)
        for i, cell in step['positions'].items():
            error = np.linalg.norm(estimator.modules[i].position - np.array(cell) * EDGE_LENGTH)
            worst = max(worst, float(error))
    return worst


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('initial', help='Pathfinder に渡した初期形状の JSON')
    parser.add_argument('scen', help='Pathfinder が出力した .scen')
    parser.add_argument('-o', '--output', required=True, help='サーボ指令列の JSON の出力先')
    parser.add_argument('--speed-deg', type=float, default=100.0, help='回転指令の大きさ [deg]')
    args = parser.parse_args()

    positions = load_initial(args.initial)
    scen_cells, moves = parse_scen(args.scen)
    offset = min(scen_cells.values())
    scen_to_id = {}
    origin_of_ids = min(positions.values())
    by_cell = {c: i for i, c in positions.items()}
    for scen_id, cell in scen_cells.items():
        scen_to_id[scen_id] = by_cell[add(tuple(a - b for a, b in zip(cell, offset)), origin_of_ids)]

    duration = EDGE_LENGTH / (SPEED_GAIN * args.speed_deg)
    steps = []
    for scen_id, step in moves:
        module = scen_to_id[scen_id]
        check_move(positions, module, step)
        commands = servo_commands(positions, module, step, args.speed_deg)
        source = positions[module]
        positions = {**positions, module: add(source, step)}
        steps.append({
            'module': module,
            'from': source,
            'to': positions[module],
            'duration_s': duration,
            'commands': [
                {'module': m, 'servo': s, 'face': FACES[s], 'angle_deg': a}
                for (m, s), a in sorted(commands.items())
            ],
            'positions': dict(positions),
        })

    worst = replay(load_initial(args.initial), steps)
    Path(args.output).write_text(json.dumps(
        {'initial': load_initial(args.initial), 'steps': steps}, indent=2, ensure_ascii=False))
    for k, step in enumerate(steps, 1):
        servos = ', '.join(
            f"ID{c['module']}.{c['face']}({c['angle_deg']:+.0f})" for c in step['commands'])
        print(f"{k:3d}: ID{step['module']} {step['from']} -> {step['to']}  {servos}")
    print(f'{len(steps)} 手、再生誤差は最大 {worst * 1000:.3f} mm')
    if worst > MAX_REPLAY_ERROR:
        sys.exit('再生誤差が大きすぎます: 指令どおりに動いていません')


if __name__ == '__main__':
    main()
