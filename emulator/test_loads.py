"""Validate device outputs independently of APL's business state."""
import unittest
import panel_sim as apl
from tui import Panel


class LoadTest(unittest.TestCase):
    def setUp(self):
        lib = apl.ensure_dll()
        apl.bind_api(lib)
        self.host = apl.Host(lib)

    def test_actual_callbacks_and_no_gear_binding(self):
        h = self.host
        h.lib.apl_core_fan_gear_set(h.core, apl.GEAR_TURBO)
        self.assertIsNone(h.loads["fan"])
        h.core_ops.uv_set(1, h.ctx)
        h.core_ops.anion_set(0, h.ctx)
        self.assertEqual(h.loads["uv"], 1)
        self.assertEqual(h.loads["anion"], 0)
        self.assertEqual(dict(h.load_rows())["UV"], "开启")
        h.core_ops.uv_set(0, h.ctx)
        self.assertEqual(dict(h.load_rows())["UV"], "关闭")

    def test_supply_and_slots_are_independent(self):
        h = self.host
        h.fan_set(1250)
        h.humidifier_set(1)
        h.display_set(0, "自定义", "测试")
        screen = Panel(h).render(126, 40)
        self.assertIn("1250 rpm", screen)
        self.assertIn("自定义: 测试", screen)
        h.display_clear_all()
        self.assertEqual(h.loads["fan"], 1250)
        h.supply_off()
        h.fan_set(2000)
        self.assertIn("无供电", dict(h.load_rows())["风机"])
        h.supply_on()
        self.assertTrue(all(value is None for value in h.loads.values()))

    def test_invalid_output_keeps_state(self):
        h = self.host
        h.fan_set(500)
        with self.assertRaises(ValueError):
            h.fan_set(-1)
        with self.assertRaises(ValueError):
            h.humidifier_set(2)
        self.assertEqual(h.loads["fan"], 500)
        self.assertIsNone(h.loads["humidifier"])


if __name__ == "__main__":
    unittest.main()
