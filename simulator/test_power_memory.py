"""Supply loss and byte storage tests. No persistence policy is imposed on APL."""
import ctypes
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import panel_sim as apl
from storage import Storage
from tui import Panel


class PowerMemoryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "nvram.bin"
        self.lib = apl.ensure_dll()
        apl.bind_api(self.lib)
        self.host = apl.Host(self.lib, Storage(self.path))

    def test_bytes_survive_power_and_new_host(self):
        self.assertEqual(self.host.memory_read(20, 3), b"\xff" * 3)
        self.host.memory_write(20, b"abc")
        self.host.supply_off()
        with self.assertRaises(RuntimeError):
            self.host.memory_write(20, b"xyz")
        with self.assertRaises(RuntimeError):
            self.host.memory_read(20, 3)
        with self.assertRaises(RuntimeError):
            self.host.memory_erase()
        self.host.supply_on()
        self.assertEqual(self.host.memory_read(20, 3), b"abc")
        other = apl.Host(self.lib, Storage(self.path))
        self.assertEqual(other.memory_read(20, 3), b"abc")
        other.memory_erase()
        self.assertEqual(Storage(self.path).read(20, 3), b"\xff" * 3)

    def test_loss_clears_volatile_state_without_shutdown(self):
        h = self.host
        shutdown = []
        callback = apl.CORE_EVENT(lambda *_: shutdown.append(True))
        h.core_ops.on_power_off = callback
        self.lib.apl_core_power_on(h.core)
        self.lib.apl_core_fan_gear_set(h.core, apl.GEAR_3)
        self.lib.apl_core_timer_set(h.core, 1, h.on_timer_cb, h.ctx)
        self.lib.apl_hmi_child_lock_set(h.hmi, apl.CHILD_LOCK_ON)
        h.supply_off()
        h.water = 0
        h.step(500)
        self.assertEqual(shutdown, [])
        self.assertEqual(h.triggers, 0)
        self.assertEqual(h.storage.writes, 0)
        h.supply_on()
        self.assertEqual(h.power(), apl.POWER_OFF)
        self.assertEqual(h.gear(), apl.GEAR_2)
        self.assertEqual(h.timer(), 0)
        self.assertFalse(h.locked())
        self.assertEqual(h.water, 0)

    def test_ui_supply_and_no_keys_while_off(self):
        panel = Panel(self.host)
        panel.render(126, 40)
        panel.key(True, "p")
        panel.key(True, "p")  # OS key repeat must not reconnect supply.
        self.assertFalse(self.host.supplied)
        panel.key(True, "6")
        panel.mouse(16, 35, True)
        panel.mouse(16, 35, False)
        self.assertFalse(any(self.host.down))
        panel.key(False, "p")
        panel.mouse(92, 35, True)
        panel.mouse(92, 35, False)
        self.assertTrue(self.host.supplied)
        self.assertFalse(any(self.host.down))

    def test_invalid_access_and_corrupt_file(self):
        for offset, length in ((-1, 1), (4096, 1), (0, -1)):
            with self.assertRaises(ValueError):
                self.host.memory_read(offset, length)
        with self.assertRaises(ValueError):
            self.host.memory_write(4095, b"ab")
        self.path.write_bytes(b"bad")
        with self.assertRaises(ValueError):
            Storage(self.path)
        self.assertEqual(self.path.read_bytes(), b"bad")

    def test_failed_commit_keeps_previous_memory(self):
        self.host.memory_write(0, b"old")
        with patch("storage.os.replace", side_effect=OSError("disk error")):
            with self.assertRaises(OSError):
                self.host.memory_write(0, b"new")
        self.assertEqual(self.host.memory_read(0, 3), b"old")
        self.assertEqual(Storage(self.path).read(0, 3), b"old")
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])


if __name__ == "__main__":
    unittest.main()
