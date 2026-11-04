from mocube_sim.firmware import ServoFirmware


def test_starts_free():
    assert ServoFirmware().angles == [None] * 6


def test_set_pos_and_free():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPos,1,30', 0.0)
    assert firmware.angles[1] == 30.0
    firmware.handle_packet(b'KRS,setFree,1', 0.0)
    assert firmware.angles[1] is None
    firmware.handle_packet(b'KRS,setPos,2,-5.5', 0.0)
    firmware.handle_packet(b'KRS,setFreeAll', 0.0)
    assert firmware.angles == [None] * 6


def test_ignores_out_of_range_and_unknown():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPos,6,10', 0.0)
    firmware.handle_packet(b'KRS,setPos,0,135.5', 0.0)
    firmware.handle_packet(b'KRS,setPos,0', 0.0)
    firmware.handle_packet(b'IR,send,0x1', 0.0)
    firmware.handle_packet(b'KRS,unknown,0,10', 0.0)
    assert firmware.angles == [None] * 6


def test_unreadable_number_is_zero_like_arduino():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPos,x,10', 0.0)  # toInt("x") == 0
    assert firmware.angles[0] == 10.0


def test_watchdog_stops_after_silence():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPosWd,0,20', 10.0)
    firmware.update(10.9)
    assert firmware.angles[0] == 20.0
    firmware.handle_packet(b'KRS,setPosWd,0,20', 10.9)
    firmware.update(11.8)
    assert firmware.angles[0] == 20.0
    firmware.update(11.9)
    assert firmware.angles[0] == 0.0


def test_plain_set_pos_cancels_watchdog():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPosWd,0,20', 0.0)
    firmware.handle_packet(b'KRS,setPos,0,20', 0.5)
    firmware.update(5.0)
    assert firmware.angles[0] == 20.0


def test_zero_with_watchdog_is_not_watched():
    firmware = ServoFirmware()
    firmware.handle_packet(b'KRS,setPosWd,0,0', 0.0)
    firmware.handle_packet(b'KRS,setPos,0,15', 0.1)
    firmware.update(5.0)
    assert firmware.angles[0] == 15.0
