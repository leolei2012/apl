"""Event ordering and change-only recording against the real library."""
import unittest
import panel_sim as apl
from tui import Panel

class EventTest(unittest.TestCase):
    def setUp(self):
        lib = apl.ensure_dll()
        apl.bind_api(lib)
        self.host = apl.Host(lib)
        self.host.step(apl.POWERUP_TICKS)
        self.host.events.clear()

    def test_gesture_precedes_effect(self):
        h = self.host
        for key in range(6):
            h.reset()
            h.step(apl.POWERUP_TICKS)
            h.events.clear()
            h.down[key] = True
            h.step()
            h.down[key] = False
            h.step()
            self.assertEqual(h.events[0][1], apl.KEY_NAMES[key] + " 短按")
        h.reset()
        h.step(apl.POWERUP_TICKS)
        h.events.clear()
        h.down[apl.KEY_LIGHT] = True
        h.step(300)
        h.down[apl.KEY_LIGHT] = False
        h.step()
        texts = [text for _, text in h.events]
        self.assertEqual(texts[:2], ["Light 长按", "睡眠尚未规定"])
        self.assertNotIn("Light 短按", texts)

    def test_light_and_multiple_events_survive_render(self):
        h = self.host
        h.on_short(apl.KEY_LIGHT)
        h.on_short(apl.KEY_LIGHT)
        self.assertEqual([text for _,text in h.events],
                         ["Light 短按", "灯光关", "Light 短按", "灯光开"])
        panel = Panel(h)
        panel.event_view = True
        screen = panel.render(126,40)
        self.assertIn("灯光关", screen)
        self.assertIn("灯光开", screen)
        self.assertEqual(len(panel.logs),4)

    def test_changed_only_and_idle_no_spam(self):
        h = self.host
        h.water = 3
        h.water = 3
        h.fan_set(1200)
        h.fan_set(1200)
        h.display_set(0,"状态","运行")
        h.display_set(0,"状态","运行")
        h.memory_write(0,b"abc")
        h.memory_write(0,b"abc")
        self.assertEqual(len(h.events),4)
        h.step()
        count = len(h.events)
        h.step(500)
        self.assertEqual(len(h.events),count)

if __name__ == "__main__":
    unittest.main()
