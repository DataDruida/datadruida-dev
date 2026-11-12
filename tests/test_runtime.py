"""Tests del runtime dd en la PC, con placa simulada.
    python tests/test_runtime.py
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "runtime"))

import fakes  # noqa: E402  (instala machine y time falsos)
from fakes import s16, advance, SleepBudget  # noqa: E402
import dd  # noqa: E402
from dd import board, modbus, sensors, control, clock  # noqa: E402


class RelaysTest(unittest.TestCase):
    def setUp(self):
        fakes.fresh()
        self.r = dd.relays()
        self.tca = board.i2c().tca

    def test_inicio_sin_pulso(self):
        # el latch se escribe ANTES de pasar a salida: nunca hubo relays energizados
        self.assertTrue(all(d == 0 for (_, _, d) in self.tca.log))
        self.assertEqual(self.tca.regs[0x03], 0x00)

    def test_set_y_estado(self):
        self.r.on(1)
        self.r.on(8)
        self.assertEqual(self.tca.driven(), 0b10000001)
        self.r.off(1)
        self.assertEqual(self.r.state(), [False] * 7 + [True])
        with self.assertRaises(ValueError):
            self.r.on(9)

    def test_heal_tras_glitch(self):
        self.r.on(3)
        self.tca.reset()                      # pico de tension
        self.assertEqual(self.tca.driven(), 0)
        self.assertTrue(self.r.heal())
        self.assertEqual(self.tca.driven(), 0b100)
        self.assertFalse(self.r.heal())

    def test_tope_de_encendido(self):
        self.r.set_max_on(2, 60)
        self.r.on(2)
        advance(59)
        self.assertEqual(self.r.enforce_limits(), [])
        advance(2)
        self.assertEqual(self.r.enforce_limits(), [2])
        self.assertFalse(self.r.get(2))
        self.assertFalse(self.r.on(2))       # bloqueado
        self.r.reset(2)
        self.assertTrue(self.r.on(2))


class ModbusTest(unittest.TestCase):
    def setUp(self):
        fakes.fresh()
        self.b = modbus.bus()
        self.u = self.b.uart

    def test_crc(self):
        # trama conocida: 01 03 00 00 00 02 -> CRC C4 0B
        self.assertEqual(modbus._frame(1, 3, 0, 2)[-2:], bytes([0xC4, 0x0B]))

    def test_sensores(self):
        self.u.slaves = {
            1: {"baud": 4800, "regs": {(3, 0): 655, (3, 1): s16(-52)}},
            2: {"baud": 4800, "regs": {(3, 0): 412, (3, 1): 213, (3, 2): 1250}},
            45: {"baud": 4800, "regs": {(3, 0): 0, (3, 1): 2530, (3, 2): 6010}},
            3: {"baud": 4800, "regs": {(3, 0): 2210, (3, 1): 612, (3, 2): 2405}},
            4: {"baud": 4800, "regs": {(4, 0): 2200, (4, 1): 0, (4, 2): 1500,
                                       (4, 3): s16(-32768), (4, 4): 750}},
        }
        self.assertEqual(dd.sensor("slicetex-th-mb-02s", 1).read(), {"temp": -5.2, "hum": 65.5})
        self.assertEqual(dd.sensor("slicetex-soil-mb-thc-s", 2).read()["ec"], 1250)
        co2 = dd.sensor("seeed-s-co2-03", 45).read()
        self.assertIsNone(co2["co2"])           # calentando: CO2 = 0 -> sin dato
        self.assertAlmostEqual(co2["hum"], 60.1)
        self.assertAlmostEqual(dd.sensor("seeed-s-ph-orp-01a", 3).read()["ph"], 6.12)
        self.assertIsNone(dd.sensor("seeed-s-ec-01", 4).read())   # valor invalido

    def test_sensor_caido_no_explota(self):
        s = dd.sensor("slicetex-th-mb-02s", 7)
        self.assertIsNone(s.read())
        self.assertIsNone(s.read())
        self.assertEqual(s.fails, 2)

    def test_excepcion_modbus(self):
        self.u.slaves = {1: {"baud": 4800, "regs": {}}}
        with self.assertRaises(modbus.ModbusError):
            self.b.read(1, 0, 1)

    def test_seeed_setup(self):
        self.u.slaves = {45: {"baud": 9600, "regs": {(3, 0x10): 45}}}
        msg = sensors.seeed_setup("seeed-s-co2-03", 20)
        self.assertTrue(msg.startswith("ok"), msg)
        self.assertEqual(self.u.slaves[45]["writes"], [(0x10, 20), (0x11, 2)])
        self.assertEqual(self.u.baudrate, 4800)   # el bus vuelve a 4800

    def test_seeed_setup_no_encontrado(self):
        self.u.slaves = {}
        self.assertEqual(sensors.seeed_setup("seeed-s-ph-orp-01a", 3, scan=False),
                         "no encontrado a 9600")
        self.assertEqual(self.u.baudrate, 4800)


class ControlTest(unittest.TestCase):
    def test_histeresis_down(self):
        h = control.Hysteresis(25, 28, "down")
        self.assertEqual([h.update(v) for v in (26, 28, 27, 25.5, 25, 26)],
                         [False, True, True, True, False, False])
        h.update(30)
        self.assertFalse(h.update(None))   # sensor caido => apaga

    def test_histeresis_up(self):
        h = control.Hysteresis(35, 45, "up")
        self.assertEqual([h.update(v) for v in (40, 34, 40, 45, 40)],
                         [False, True, True, False, False])

    def test_ciclo(self):
        fakes.fresh()
        c = control.Cycle(on_s=20, off_s=120)
        seq = []
        for _ in range(16):
            seq.append(c.update(True))
            advance(10)
        # 20 s prendido, 120 s apagado, vuelve a prender
        self.assertEqual(seq[:3], [True, True, False])
        self.assertFalse(any(seq[3:14]))
        self.assertTrue(seq[14])
        self.assertFalse(c.update(False))

    def test_vpd(self):
        self.assertAlmostEqual(control.vpd(26, 60, leaf_offset=0), 1.344, places=2)
        self.assertIsNone(control.vpd(None, 60))


class ClockTest(unittest.TestCase):
    def test_ventana(self):
        m = clock.minutes
        self.assertTrue(clock.in_window("08:00", "20:00", at=m("08:00")))
        self.assertFalse(clock.in_window("08:00", "20:00", at=m("20:00")))
        self.assertTrue(clock.in_window("20:00", "06:00", at=m("23:59")))
        self.assertTrue(clock.in_window("20:00", "06:00", at=m("05:59")))
        self.assertFalse(clock.in_window("20:00", "06:00", at=m("12:00")))
        self.assertFalse(clock.in_window("08:00", "08:00", at=m("08:00")))

    def test_sin_hora_confiable_apaga(self):
        clock._ok = False
        self.assertFalse(clock.in_window("00:00", "23:59"))

    def test_rtc(self):
        fakes.fresh()
        clock.write_rtc((2026, 11, 12, 14, 35, 7))
        self.assertEqual(clock.read_rtc(), (2026, 11, 12, 14, 35, 7))
        board.i2c().rtc[0] |= 0x80               # oscilador detenido
        self.assertIsNone(clock.read_rtc())


class RunTest(unittest.TestCase):
    def setUp(self):
        fakes.fresh()
        clock._ok = False

    def test_detener_apaga_todo(self):
        r = dd.relays()
        with SleepBudget(30):
            dd.run(lambda: r.on(5), every_s=1, wifi=False)
        self.assertEqual(board.i2c().tca.driven(), 0)

    def test_errores_apagan_y_frenan(self):
        r = dd.relays()
        n = [0]

        def mala():
            n[0] += 1
            r.on(1)
            raise ValueError("bug del usuario")
        with SleepBudget(400):
            dd.run(mala, every_s=1, wifi=False)
        self.assertEqual(n[0], 3)
        self.assertEqual(board.i2c().tca.driven(), 0)

    def test_heal_en_el_bucle(self):
        r = dd.relays()
        tca = board.i2c().tca
        state = {"k": 0}

        def loop():
            r.on(4)
            state["k"] += 1
            if state["k"] == 2:
                tca.reset()
        with SleepBudget(25):
            dd.run(loop, every_s=1, wifi=False)
        self.assertGreaterEqual(r.heals, 1)


class ExamplesTest(unittest.TestCase):
    """Los 6 ejemplos compilan, corren un rato contra la placa simulada y
    terminan con todo apagado al apretar Detener."""

    def _files(self):
        ex = os.path.join(HERE, "..", "examples")
        return ex, sorted(f for f in os.listdir(ex) if f.endswith(".py"))

    def test_son_seis(self):
        self.assertEqual(len(self._files()[1]), 6)

    def test_corren(self):
        ex, files = self._files()
        for f in files:
            with self.subTest(ejemplo=f):
                fakes.fresh()
                u = modbus.bus().uart
                u.slaves = {a: {"baud": 4800,
                                "regs": {(fc, i): 500 for fc in (3, 4) for i in range(8)}}
                            for a in (1, 2, 3, 4, 45)}
                with open(os.path.join(ex, f), encoding="utf-8") as fh:
                    code = compile(fh.read(), f, "exec")
                with SleepBudget(80):
                    exec(code, {"__name__": "__main__"})
                self.assertEqual(board.i2c().tca.driven(), 0)


class OtrasPlacasTest(unittest.TestCase):
    """Relay-6CH y Relay-1CH: relays por GPIO, sin RTC / con pin DE."""

    def setUp(self):
        fakes.fresh()

    def test_6ch_relays_por_gpio_sin_pulso(self):
        board.select("waveshare-esp32-s3-relay-6ch")
        r = dd.relays()
        pins = [1, 2, 41, 42, 45, 46]
        self.assertEqual(r.count, 6)
        # nunca hubo un pin de relay en alto al iniciar
        self.assertTrue(all(lvl == 0 for (g, lvl) in fakes.Pin.log if g in pins))
        r.on(1)
        r.on(6)
        self.assertEqual([fakes.Pin.out[g] for g in pins], [1, 0, 0, 0, 0, 1])
        with self.assertRaises(ValueError):
            r.on(7)
        r.all_off()
        self.assertEqual([fakes.Pin.out[g] for g in pins], [0] * 6)

    def test_6ch_tope_y_heal(self):
        board.select("waveshare-esp32-s3-relay-6ch")
        r = dd.relays()
        r.set_max_on(3, 10)
        r.on(3)
        fakes.Pin.out[41] = 0            # algo bajó el pin por fuera
        self.assertFalse(r.heal())       # GPIO: re-afirma sin contar como heal
        self.assertEqual(fakes.Pin.out[41], 1)
        advance(11)
        self.assertEqual(r.enforce_limits(), [3])
        self.assertEqual(fakes.Pin.out[41], 0)

    def test_6ch_sin_rtc_ni_entradas(self):
        board.select("waveshare-esp32-s3-relay-6ch")
        self.assertIsNone(board.i2c())
        self.assertIsNone(clock.read_rtc())
        clock.write_rtc((2026, 11, 12, 10, 0, 0))   # no explota
        with self.assertRaises(ValueError):
            dd.inputs.closed(1)

    def test_6ch_buzzer(self):
        board.select("waveshare-esp32-s3-relay-6ch")
        dd.beep(50, times=2)
        self.assertEqual([lvl for (g, lvl) in fakes.Pin.log if g == 21], [0, 1, 0, 1, 0])

    def test_extras_en_placa_sin_buzzer(self):
        board.select("waveshare-esp32-s3-eth-8di-8ro")
        dd.beep()          # no hace nada
        dd.led(255, 0, 0)  # no hace nada

    def test_1ch_relay_y_de_rs485(self):
        board.select("waveshare-esp32-s3-relay-1ch")
        r = dd.relays()
        self.assertEqual(r.count, 1)
        r.on(1)
        self.assertEqual(fakes.Pin.out[47], 1)
        b = modbus.bus()
        b.uart.slaves[1] = {"baud": 4800, "regs": {(3, 0): 250}}
        seen = []
        orig = b.uart.write

        def spy(req):
            seen.append(fakes.Pin.out.get(21))
            return orig(req)
        b.uart.write = spy
        self.assertEqual(b.read(1, 0, 1), [250])
        self.assertEqual(seen, [1])                 # DE alto mientras transmite
        self.assertEqual(fakes.Pin.out[21], 0)      # y bajo para escuchar

    def test_1ch_rtc_en_su_bus(self):
        board.select("waveshare-esp32-s3-relay-1ch")
        clock.write_rtc((2026, 11, 12, 10, 30, 0))
        self.assertEqual(clock.read_rtc(), (2026, 11, 12, 10, 30, 0))

    def test_placa_desde_config(self):
        import json
        from dd import config
        orig = config.load
        config.load = lambda: {"board": "waveshare-esp32-s3-relay-6ch"}
        try:
            fakes.fresh()
            self.assertEqual(board.current_id(), "waveshare-esp32-s3-relay-6ch")
            config.load = lambda: {"board": "no-existe"}
            fakes.fresh()
            self.assertEqual(board.current_id(), board.DEFAULT)
        finally:
            config.load = orig
            fakes.fresh()


if __name__ == "__main__":
    unittest.main()
