"""Interaction checks against the real APL DLL; no interactive console needed."""
import ctypes
import unittest

import panel_sim as apl
from tui import Panel, Record


class TuiTest(unittest.TestCase):
    def setUp(self):
        lib = apl.ensure_dll()
        apl.bind_api(lib)
        self.panel = Panel(apl.Host(lib))
        self.panel.render(126, 40)
        self.host = self.panel.host

    def click(self, x, y, ticks=1):
        self.panel.mouse(x,y,True)
        self.host.step(ticks)
        self.panel.mouse(x,y,False)
        self.host.step()

    def test_mouse_power_and_long_lock(self):
        self.click(35,30)
        self.assertEqual(self.host.power(), apl.POWER_ON)
        self.click(64,14,300)
        self.assertTrue(self.host.locked())
        self.assertEqual(self.host.short_count[apl.KEY_TIMER], 0)
        self.click(35,30)
        self.assertEqual(self.host.power(), apl.POWER_ON)

    def test_hold_multiple_and_release(self):
        self.click(23,35)
        self.click(44,35)
        self.assertTrue(self.host.down[1] and self.host.down[4])
        self.panel.release()
        self.assertFalse(any(self.host.down))

    def test_keyboard_mouse_independence(self):
        self.panel.key(True,"6")
        self.panel.mouse(35,30,True)
        self.panel.mouse(35,30,False)
        self.assertTrue(self.host.down[5])
        self.host.step()
        self.panel.key(False,"6")
        self.host.step()
        self.assertEqual(self.host.power(), apl.POWER_ON)

    def test_sensor_bounds_and_resize(self):
        for _ in range(8):
            self.click(96,6)
        self.assertEqual(self.host.water,0)
        self.assertEqual(self.host.triggers,1)
        self.click(118,6)
        self.assertEqual(self.host.recovers,1)
        self.panel.key(True,"1")
        self.panel.render(80,24)
        self.assertFalse(any(self.host.down))
        self.assertEqual(self.panel.regions,[])

    def test_timer_cycle_and_input_layout(self):
        for _ in range(13):
            self.click(64,14)
        self.assertEqual(self.host.timer(),0)
        self.assertEqual(ctypes.sizeof(Record),20)
        self.assertEqual(Record.data.offset,4)


if __name__ == "__main__":
    unittest.main()
