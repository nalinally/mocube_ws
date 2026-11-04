import json

from mocube_sim.world import load_layout, NO_NEIGHBOR, World
import numpy as np
import pytest

EDGE = 0.1131
GAIN = 0.001  # 1 deg あたり 1 mm/s


def run(world, until, dt=0.01):
    t = world.time
    while t < until - 1e-9:
        t = min(t + dt, until)
        world.advance(t)


def make_world(layout=None, **kwargs):
    return World(layout or {0: (0, 0, 0), 1: (1, 0, 0)}, EDGE, GAIN, 0.0, **kwargs)


def test_servo_moves_neighbor_with_own_speed_gain():
    world = make_world()
    # 0 の r 面 (サーボ 1) の正回転で、+x 側の 1 が +y に動く
    world.send(0, b'KRS,setPos,1,10')
    run(world, 2.0)
    position = world.positions()[1]
    assert position[1] == pytest.approx(0.02, abs=1e-6)
    assert position[0] == pytest.approx(EDGE)
    assert np.allclose(world.positions()[0], 0.0)


def test_free_and_stop_do_not_move():
    world = make_world()
    world.send(0, b'KRS,setPos,1,0')
    world.send(1, b'KRS,setFree,4')
    run(world, 1.0)
    assert np.allclose(world.positions()[1], (EDGE, 0, 0))


def test_watchdog_stops_motion():
    world = make_world()
    world.send(0, b'KRS,setPosWd,1,10')
    run(world, 3.0)
    assert world.positions()[1][1] == pytest.approx(0.01, abs=2e-4)


def test_ir_received_only_by_negative_side_when_aligned():
    world = make_world()
    run(world, 0.2)
    address, t = world.modules[0].receivers['r']
    assert address == 1 and t > 0.0
    assert all(a == NO_NEIGHBOR for a, _ in world.modules[1].receivers.values())
    assert world.modules[0].receivers['u'][0] == NO_NEIGHBOR


def test_ir_stops_when_offset_exceeds_range():
    world = make_world(ir_range=0.1)
    world.send(0, b'KRS,setPos,1,100')  # 0.1 m/s
    run(world, 0.5)
    _, last = world.modules[0].receivers['r']
    # ずれが 0.1 * 一辺 (約 11 mm, 約 0.11 s) を超えたら受けない。値は残る
    assert 0.0 < last <= 0.12
    assert world.modules[0].receivers['r'][0] == 1


def test_ir_received_again_at_next_cell():
    layout = {0: (0, 0, 0), 1: (0, 1, 0), 2: (1, 0, 0)}
    world = make_world(layout)
    # 0 の r サーボで 2 を +y へ 1 マス (113.1 mm を 100 mm/s)。
    # 途中から接する 1 の r サーボも同じ向きに回す (計画と同じ)
    world.send(0, b'KRS,setPos,1,100')
    world.send(1, b'KRS,setPos,1,100')
    run(world, EDGE / 0.1)
    world.send(0, b'KRS,setPos,1,0')
    world.send(1, b'KRS,setPos,1,0')
    start = world.time
    run(world, start + 0.3)
    assert world.grid_positions()[2] == (1, 1, 0)
    address, t = world.modules[1].receivers['r']
    assert address == 2 and t > start


def test_heartbeat_makes_online():
    world = make_world()
    assert not any(m.online for m in world.modules.values())
    run(world, 1.0)
    assert all(m.online for m in world.modules.values())


def test_unknown_module():
    world = make_world()
    assert not world.send(5, b'KRS,setPos,1,10')
    with pytest.raises(ValueError):
        world.set_grid_positions({5: (0, 0, 0)})


def test_set_grid_positions():
    world = make_world()
    world.set_grid_positions({1: (0, 0, 1)})
    assert world.grid_positions()[1] == (0, 0, 1)
    run(world, 0.2)
    assert world.modules[0].receivers['u'][0] == 1


def test_load_layout(tmp_path):
    plan = tmp_path / 'plan.json'
    plan.write_text(json.dumps({'initial': {'0': [0, 0, 0], '1': [1, 0, 0]}, 'steps': []}))
    assert load_layout(str(plan)) == {0: (0, 0, 0), 1: (1, 0, 0)}
    layout = tmp_path / 'layout.yaml'
    layout.write_text('0: [0, 0, 0]\n3: [0, 0, 1]\n')
    assert load_layout(str(layout)) == {0: (0, 0, 0), 3: (0, 0, 1)}
