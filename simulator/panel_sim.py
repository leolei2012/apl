"""
APL 面板的宿主仿真。

本程序不自己实现主控或人机。它把 src 下的库编成动态库并加载，
持有一份 struct apl，注入按键电平和传感器，每 10 ms 调用 apl_tick。
终端画面从 getter 读出。库尚未规定的风干、配网、睡眠、自清洁不在这里另做。

运行：python simulator/panel_sim.py
自检：python simulator/panel_sim.py --self-test
"""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes
from pathlib import Path

from storage import Storage

ROOT = Path(__file__).resolve().parents[1]
TICK_S = 0.01
LONG_PRESS_TICKS = 300
PANEL_W = 40
LOAD_W = 32
HOUR_S = 3600
TIMER_HOUR_MAX = 12

POWER_OFF = 0
POWER_ON = 1
GEAR_SLEEP = 0
GEAR_1 = 1
GEAR_2 = 2
GEAR_3 = 3
GEAR_AUTO = 4
GEAR_TURBO = 5
GEAR_CYCLE = (GEAR_SLEEP, GEAR_1, GEAR_2, GEAR_3, GEAR_AUTO)
GEAR_NAME = {
    GEAR_SLEEP: "睡眠",
    GEAR_1: "一档",
    GEAR_2: "二档",
    GEAR_3: "三档",
    GEAR_AUTO: "自动",
    GEAR_TURBO: "极速",
}

KEY_RELEASED = 0
KEY_PRESSED = 1
KEY_PURIFY = 0
KEY_HUMIDITY = 1
KEY_TIMER = 2
KEY_LIGHT = 3
KEY_SPEED = 4
KEY_POWER = 5
KEY_COUNT = 6
KEY_LABEL = ("净化", "恒湿", "定时", "灯光", "风速", "电源")
KEY_NAMES = ("Purify", "Humidity", "Timer", "Light", "Speed", "Power")
KEY_CHAR = {"1": KEY_PURIFY, "2": KEY_HUMIDITY, "3": KEY_TIMER, "4": KEY_LIGHT, "5": KEY_SPEED, "6": KEY_POWER}

UNIT_NONE = 0
UNIT_HOUR = 1
CHILD_LOCK_OFF = 0
CHILD_LOCK_ON = 1

VCVARS = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat")


def read_macro(path: Path, name: str) -> int:
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "#define" and parts[1] == name:
            return int(parts[2].rstrip("uU"))
    raise SystemExit(f"{path} 里没有 {name}")


CORE_BYTES = read_macro(ROOT / "include" / "apl_core.h", "APL_CORE_OPAQUE_SIZE")
HMI_BYTES = read_macro(ROOT / "include" / "apl_hmi.h", "APL_HMI_OPAQUE_SIZE")
LAMP_FULL = read_macro(ROOT / "include" / "apl_hmi.h", "APL_HMI_LAMP_FULL")
LAMP_HALF = read_macro(ROOT / "include" / "apl_hmi.h", "APL_HMI_LAMP_HALF")
POWERUP_TICKS = read_macro(ROOT / "include" / "apl_hmi.h", "APL_HMI_POWERUP_LAMP_TICKS")


class Core(ctypes.Union):
    _fields_ = [
        ("align_ptr", ctypes.c_void_p),
        ("align_ll", ctypes.c_longlong),
        ("align_d", ctypes.c_double),
        ("bytes", ctypes.c_ubyte * CORE_BYTES),
    ]


class Hmi(ctypes.Union):
    _fields_ = [
        ("align_ptr", ctypes.c_void_p),
        ("align_ll", ctypes.c_longlong),
        ("align_d", ctypes.c_double),
        ("bytes", ctypes.c_ubyte * HMI_BYTES),
    ]


class Apl(ctypes.Structure):
    _fields_ = [
        ("core", Core),
        ("hmi", Hmi),
    ]


class KeyState(ctypes.Structure):
    _fields_ = [
        ("purify", ctypes.c_int),
        ("humidity", ctypes.c_int),
        ("timer", ctypes.c_int),
        ("light", ctypes.c_int),
        ("speed", ctypes.c_int),
        ("power", ctypes.c_int),
    ]


WATER_LEVEL_GET = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)
PM25_GET = ctypes.CFUNCTYPE(ctypes.c_uint16, ctypes.c_void_p)
HUMIDITY_GET = ctypes.CFUNCTYPE(ctypes.c_uint8, ctypes.c_void_p)
TEMPERATURE_GET = ctypes.CFUNCTYPE(ctypes.c_int16, ctypes.c_void_p)
CORE_EVENT = ctypes.CFUNCTYPE(None, ctypes.POINTER(Core), ctypes.c_void_p)
SWITCH_SET = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_void_p)
KEY_STATE_GET = ctypes.CFUNCTYPE(None, ctypes.POINTER(KeyState), ctypes.c_void_p)
KEY_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(Hmi), ctypes.c_void_p)


class CoreOps(ctypes.Structure):
    _fields_ = [
        ("water_level_get", WATER_LEVEL_GET),
        ("pm25_get", PM25_GET),
        ("humidity_get", HUMIDITY_GET),
        ("temperature_get", TEMPERATURE_GET),
        ("water_on_trigger", CORE_EVENT),
        ("water_on_recover", CORE_EVENT),
        ("uv_set", SWITCH_SET),
        ("anion_set", SWITCH_SET),
        ("on_power_off", CORE_EVENT),
    ]


class HmiOps(ctypes.Structure):
    _fields_ = [("key_state_get", KEY_STATE_GET)]


def host_from(ctx: int | ctypes.c_void_p) -> Host:
    return ctypes.cast(ctx, ctypes.POINTER(ctypes.py_object)).contents.value


def ensure_dll() -> ctypes.CDLL:
    out_dir = Path(tempfile.gettempdir()) / "apl-simulator"
    out_dir.mkdir(parents=True, exist_ok=True)
    dll_path = out_dir / "apl.dll"
    sources = [
        ROOT / "src" / "apl.c",
        ROOT / "src" / "apl_core.c",
        ROOT / "src" / "apl_hmi.c",
        ROOT / "include" / "apl.h",
        ROOT / "include" / "apl_type.h",
        ROOT / "include" / "apl_core.h",
        ROOT / "include" / "apl_hmi.h",
        ROOT / "config" / "apl_core_config.h",
        ROOT / "config" / "apl_hmi_config.h",
        ROOT / "simulator" / "apl.def",
    ]
    stale = (not dll_path.exists()) or any(dll_path.stat().st_mtime < item.stat().st_mtime for item in sources)
    if stale:
        if not VCVARS.is_file():
            raise SystemExit(f"找不到 {VCVARS}")
        obj = out_dir.as_posix() + "/"
        command = "\n".join(
            [
                "@echo off",
                f'call "{VCVARS}"',
                "if errorlevel 1 exit /b 1",
                "cl /nologo /std:c11 /utf-8 /W4 /WX /LD "
                f'/I "{ROOT / "include"}" '
                f'/I "{ROOT / "config"}" '
                f'/Fo"{obj}" /Fe"{dll_path}" '
                f'"{sources[0]}" "{sources[1]}" "{sources[2]}" '
                f'/link /DEF:"{ROOT / "simulator" / "apl.def"}"',
                "if errorlevel 1 exit /b 1",
            ]
        )
        script = out_dir / "build_apl.bat"
        script.write_text(command, encoding="ascii")
        result = subprocess.run(["cmd", "/c", str(script)], capture_output=True, text=True, errors="replace")
        if result.returncode != 0 or not dll_path.exists():
            raise SystemExit(result.stdout + "\n" + result.stderr)
    return ctypes.CDLL(str(dll_path))


def bind_api(lib: ctypes.CDLL) -> None:
    core_p = ctypes.POINTER(Core)
    hmi_p = ctypes.POINTER(Hmi)
    apl_p = ctypes.POINTER(Apl)
    lib.apl_init.argtypes = [
        apl_p,
        ctypes.POINTER(CoreOps),
        ctypes.c_void_p,
        ctypes.POINTER(HmiOps),
        ctypes.c_void_p,
    ]
    lib.apl_init.restype = None
    lib.apl_tick.argtypes = [apl_p]
    lib.apl_tick.restype = None
    lib.apl_core_init.argtypes = [core_p, ctypes.POINTER(CoreOps), ctypes.c_void_p]
    lib.apl_core_init.restype = None
    lib.apl_core_tick.argtypes = [core_p]
    lib.apl_core_tick.restype = None
    lib.apl_core_power_on.argtypes = [core_p]
    lib.apl_core_power_on.restype = None
    lib.apl_core_power_off.argtypes = [core_p]
    lib.apl_core_power_off.restype = None
    lib.apl_core_power_get.argtypes = [core_p]
    lib.apl_core_power_get.restype = ctypes.c_int
    lib.apl_core_timer_set.argtypes = [core_p, ctypes.c_uint32, CORE_EVENT, ctypes.c_void_p]
    lib.apl_core_timer_set.restype = None
    lib.apl_core_timer_get.argtypes = [core_p]
    lib.apl_core_timer_get.restype = ctypes.c_uint32
    lib.apl_core_fan_gear_set.argtypes = [core_p, ctypes.c_int]
    lib.apl_core_fan_gear_set.restype = None
    lib.apl_core_fan_gear_get.argtypes = [core_p]
    lib.apl_core_fan_gear_get.restype = ctypes.c_int
    lib.apl_core_filter_purify_reset.argtypes = [core_p]
    lib.apl_core_filter_purify_reset.restype = None
    lib.apl_core_filter_purify_get.argtypes = [core_p]
    lib.apl_core_filter_purify_get.restype = ctypes.c_uint32
    lib.apl_core_purify_enabled_get.argtypes = [core_p]
    lib.apl_core_purify_enabled_get.restype = ctypes.c_uint8
    lib.apl_core_filter_humidify_reset.argtypes = [core_p]
    lib.apl_core_filter_humidify_reset.restype = None
    lib.apl_core_filter_humidify_get.argtypes = [core_p]
    lib.apl_core_filter_humidify_get.restype = ctypes.c_uint32
    lib.apl_core_humidify_enabled_get.argtypes = [core_p]
    lib.apl_core_humidify_enabled_get.restype = ctypes.c_uint8
    lib.apl_hmi_init.argtypes = [hmi_p, ctypes.POINTER(HmiOps), ctypes.c_void_p]
    lib.apl_hmi_init.restype = None
    lib.apl_hmi_tick.argtypes = [hmi_p]
    lib.apl_hmi_tick.restype = None
    lib.apl_hmi_powerup_lamp_get.argtypes = [hmi_p]
    lib.apl_hmi_powerup_lamp_get.restype = ctypes.c_uint8
    lib.apl_hmi_key_lamp_set.argtypes = [hmi_p, ctypes.c_int, ctypes.c_uint8]
    lib.apl_hmi_key_lamp_set.restype = None
    lib.apl_hmi_key_lamp_get.argtypes = [hmi_p, ctypes.c_int]
    lib.apl_hmi_key_lamp_get.restype = ctypes.c_uint8
    lib.apl_hmi_key_callback_set.argtypes = [hmi_p, ctypes.c_int, KEY_CB, KEY_CB, ctypes.c_void_p]
    lib.apl_hmi_key_callback_set.restype = None
    lib.apl_hmi_digits_brightness_set.argtypes = [hmi_p, ctypes.c_uint8]
    lib.apl_hmi_digits_brightness_set.restype = None
    lib.apl_hmi_digits_brightness_get.argtypes = [hmi_p]
    lib.apl_hmi_digits_brightness_get.restype = ctypes.c_uint8
    lib.apl_hmi_digits_content_set.argtypes = [hmi_p, ctypes.c_char_p]
    lib.apl_hmi_digits_content_set.restype = None
    lib.apl_hmi_digits_content_get.argtypes = [hmi_p, ctypes.c_char_p]
    lib.apl_hmi_digits_content_get.restype = None
    lib.apl_hmi_digits_unit_set.argtypes = [hmi_p, ctypes.c_int]
    lib.apl_hmi_digits_unit_set.restype = None
    lib.apl_hmi_digits_unit_get.argtypes = [hmi_p]
    lib.apl_hmi_digits_unit_get.restype = ctypes.c_int
    lib.apl_hmi_water_lamp_set.argtypes = [hmi_p, ctypes.c_uint8, ctypes.c_uint8]
    lib.apl_hmi_water_lamp_set.restype = None
    lib.apl_hmi_water_lamp_get.argtypes = [hmi_p, ctypes.c_uint8]
    lib.apl_hmi_water_lamp_get.restype = ctypes.c_uint8
    lib.apl_hmi_child_lock_set.argtypes = [hmi_p, ctypes.c_int]
    lib.apl_hmi_child_lock_set.restype = None
    lib.apl_hmi_child_lock_get.argtypes = [hmi_p]
    lib.apl_hmi_child_lock_get.restype = ctypes.c_int
    lib.apl_hmi_purify_enabled_set.argtypes = [hmi_p, ctypes.c_uint8]
    lib.apl_hmi_purify_enabled_set.restype = None
    lib.apl_hmi_humidify_enabled_set.argtypes = [hmi_p, ctypes.c_uint8]
    lib.apl_hmi_humidify_enabled_set.restype = None
    lib.apl_hmi_ambient_brightness_set.argtypes = [hmi_p, ctypes.c_uint8]
    lib.apl_hmi_ambient_brightness_set.restype = None


class Host:
    """硬件、按键回调和重画。库对象是一份 struct apl。"""

    def __init__(self, lib: ctypes.CDLL, storage: Storage | None = None) -> None:
        self.events = []
        self._output_snapshot = None
        self.lib = lib
        self.display_slots = [None] * 6
        self.storage = storage if storage is not None else Storage()
        self.supplied = True
        self.loads = {"fan": None, "humidifier": None, "uv": None, "anion": None}
        self.apl = Apl()
        self.core = ctypes.cast(ctypes.byref(self.apl, Apl.core.offset), ctypes.POINTER(Core))
        self.hmi = ctypes.cast(ctypes.byref(self.apl, Apl.hmi.offset), ctypes.POINTER(Hmi))
        self.down = [False] * KEY_COUNT
        self.panel_light = True
        self.water = 2
        self.pm25 = 28
        self.humidity = 55
        self.temperature = 25
        self.uv_called = 0
        self.anion_called = 0
        self.pm_reads = 0
        self.triggers = 0
        self.recovers = 0
        self.short_count = [0] * KEY_COUNT
        self.long_count = [0] * KEY_COUNT
        self.message = "待机"
        self._anchor = ctypes.py_object(self)
        self._anchor_ptr = ctypes.pointer(self._anchor)
        self.ctx = ctypes.cast(self._anchor_ptr, ctypes.c_void_p)
        self._keep: list[object] = []
        self._bind_ops()
        self.reset()

    def emit_event(self, text: str) -> None:
        self.events.append((time.strftime("%H:%M:%S"), str(text)))
        del self.events[:-500]

    @property
    def message(self):
        return self._message

    @message.setter
    def message(self, text):
        self._message = text
        self.emit_event(text)

    def __setattr__(self, name, value):
        labels = {"water": "水位输入", "pm25": "PM2.5 输入", "humidity": "湿度输入", "temperature": "温度输入"}
        changed = name in labels and name in self.__dict__ and self.__dict__[name] != value
        old = self.__dict__.get(name)
        object.__setattr__(self, name, value)
        if changed:
            self.emit_event(f"{labels[name]}: {old} -> {value}")

    def observe_outputs(self):
        """Record changes in simulated panel outputs, not arbitrary private APL fields."""
        digits, unit, bright = self.digits()
        current = {"键灯": tuple(self.lamp(i) for i in range(KEY_COUNT)),
                   "水位灯": tuple(self.lib.apl_hmi_water_lamp_get(self.hmi, i) for i in range(1, 5)),
                   "数码管": (digits, unit, bright)}
        previous = self._output_snapshot
        self._output_snapshot = current
        if previous is not None:
            for label, value in current.items():
                if previous[label] != value:
                    self.emit_event(f"{label}: {value}")

    def _bind_ops(self) -> None:
        def water_level_get(ctx: ctypes.c_void_p) -> int:
            return host_from(ctx).water

        def pm25_get(ctx: ctypes.c_void_p) -> int:
            host = host_from(ctx)
            host.pm_reads += 1
            return host.pm25

        def humidity_get(ctx: ctypes.c_void_p) -> int:
            return host_from(ctx).humidity

        def temperature_get(ctx: ctypes.c_void_p) -> int:
            return host_from(ctx).temperature

        def water_on_trigger(_self: ctypes.POINTER(Core), ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            host.triggers += 1
            host.message = "水位降到 0"

        def water_on_recover(_self: ctypes.POINTER(Core), ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            host.recovers += 1
            host.message = "水位恢复"

        def uv_set(state: int, ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            host.uv_called += 1
            host.uv_set(state)

        def anion_set(state: int, ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            host.anion_called += 1
            host.anion_set(state)

        def on_power_off(_self: ctypes.POINTER(Core), ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            host.lib.apl_hmi_child_lock_set(host.hmi, CHILD_LOCK_OFF)

        def key_state_get(state: ctypes.POINTER(KeyState), ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            names = ("purify", "humidity", "timer", "light", "speed", "power")
            for index, name in enumerate(names):
                level = KEY_PRESSED if host.down[index] else KEY_RELEASED
                setattr(state.contents, name, level)

        def on_timer(_self: ctypes.POINTER(Core), ctx: ctypes.c_void_p) -> None:
            host = host_from(ctx)
            if host.power() == POWER_ON:
                host.lib.apl_core_power_off(host.core)
                host.message = "定时到，关机"
            else:
                host.lib.apl_core_power_on(host.core)
                host.message = "定时到，开机"

        self._ops_cb = (
            WATER_LEVEL_GET(water_level_get),
            PM25_GET(pm25_get),
            HUMIDITY_GET(humidity_get),
            TEMPERATURE_GET(temperature_get),
            CORE_EVENT(water_on_trigger),
            CORE_EVENT(water_on_recover),
            SWITCH_SET(uv_set),
            SWITCH_SET(anion_set),
            CORE_EVENT(on_power_off),
            KEY_STATE_GET(key_state_get),
            CORE_EVENT(on_timer),
        )
        self.core_ops = CoreOps(*self._ops_cb[:9])
        self.hmi_ops = HmiOps(self._ops_cb[9])
        self.on_timer_cb = self._ops_cb[10]

    def fan_set(self, rpm: int) -> None:
        """Ideal simulated fan: apply a commanded RPM, not the APL gear."""
        if type(rpm) is not int or rpm < 0:
            raise ValueError("风机转速必须是非负整数")
        if self.supplied and self.loads["fan"] != rpm:
            self.loads["fan"] = rpm
            self.emit_event(f"风机输出: {rpm} rpm")

    def _switch_load(self, name: str, state: int) -> None:
        if type(state) not in (int, bool) or state not in (0, 1):
            raise ValueError("负载开关只能是 0 或 1")
        if self.supplied and self.loads[name] != int(state):
            self.loads[name] = int(state)
            label = {"humidifier": "加湿器", "uv": "UV", "anion": "负离子"}[name]
            self.emit_event(label + ("开" if state else "关"))

    def humidifier_set(self, state: int) -> None:
        self._switch_load("humidifier", state)

    def uv_set(self, state: int) -> None:
        self._switch_load("uv", state)

    def anion_set(self, state: int) -> None:
        self._switch_load("anion", state)

    def load_rows(self) -> list[tuple[str, str]]:
        rows = [("供电", "接通" if self.supplied else "断开")]
        for name, label in (("fan", "风机"), ("humidifier", "加湿器"), ("uv", "UV"), ("anion", "负离子")):
            value = self.loads[name]
            if not self.supplied:
                text = "停止（无供电）"
            elif value is None:
                text = "未驱动"
            elif name == "fan":
                text = f"运行 {value} rpm" if value else "停止 / 0 rpm"
            else:
                text = "开启" if value else "关闭"
            rows.append((label, text))
        return rows

    def display_set(self, slot: int, label: str, value: object) -> None:
        """Set one of six generic display rows (0..5). No APL field binding."""
        self._display_check(slot)
        entry = (str(label), str(value))
        if self.display_slots[slot] != entry:
            self.display_slots[slot] = entry
            self.emit_event(f"槽位 {slot}: {entry[0]} = {entry[1]}")

    def display_clear(self, slot: int) -> None:
        self._display_check(slot)
        if self.display_slots[slot] is not None:
            self.display_slots[slot] = None
            self.emit_event(f"槽位 {slot} 清空")

    def display_clear_all(self) -> None:
        for slot in range(6):
            self.display_clear(slot)

    @staticmethod
    def _display_check(slot: int) -> None:
        if type(slot) is not int or not 0 <= slot < 6:
            raise ValueError("显示槽位必须是 0 到 5 的整数")

    def memory_read(self, offset: int, length: int) -> bytes:
        """Read bytes from the simulated nonvolatile device while powered."""
        if not self.supplied:
            raise RuntimeError("未供电，存储器不可访问")
        return self.storage.read(offset, length)

    def memory_write(self, offset: int, data: bytes) -> None:
        if not self.supplied:
            raise RuntimeError("未供电，存储器不可访问")
        before = self.storage.writes
        self.storage.write(offset, data)
        if self.storage.writes != before:
            self.emit_event(f"存储写入: 地址 {offset}, {len(data)} 字节")

    def memory_erase(self) -> None:
        if not self.supplied:
            raise RuntimeError("未供电，存储器不可访问")
        before = self.storage.writes
        self.storage.erase()
        if self.storage.writes != before:
            self.emit_event("存储器已擦除")

    def supply_off(self) -> None:
        """Abrupt loss: no shutdown callback and no last-minute memory write."""
        if not self.supplied:
            return
        self.supplied = False
        for label, value in self.load_rows()[1:]:
            self.emit_event(f"{label}: 停止（无供电）")
        self.loads = {name: None for name in self.loads}
        self.down = [False] * KEY_COUNT
        ctypes.memset(ctypes.byref(self.apl), 0, ctypes.sizeof(self.apl))
        self.message = "供电已断开"
        self.observe_outputs()

    def supply_on(self) -> None:
        if self.supplied:
            return
        self.supplied = True
        self.reset()
        self.message = "供电已接通"

    def reset(self) -> None:
        if not self.supplied:
            self.message = "无供电，无法重置"
            return
        self.down = [False] * KEY_COUNT
        self.panel_light = True
        self.message = "重新初始化"
        self.lib.apl_init(
            ctypes.byref(self.apl),
            ctypes.byref(self.core_ops),
            self.ctx,
            ctypes.byref(self.hmi_ops),
            self.ctx,
        )
        self._bind_keys()
        self.publish()

    def _bind_keys(self) -> None:
        self._keep.clear()
        for key in range(KEY_COUNT):
            short_cb = KEY_CB(lambda _hmi, ctx, key=key: host_from(ctx).on_short(key))
            long_cb = KEY_CB(lambda _hmi, ctx, key=key: host_from(ctx).on_long(key))
            self._keep.extend((short_cb, long_cb))
            self.lib.apl_hmi_key_callback_set(
                self.hmi, key, short_cb, long_cb, self.ctx
            )

    def power(self) -> int:
        return int(self.lib.apl_core_power_get(self.core))

    def gear(self) -> int:
        return int(self.lib.apl_core_fan_gear_get(self.core))

    def timer(self) -> int:
        return int(self.lib.apl_core_timer_get(self.core))

    def locked(self) -> bool:
        return int(self.lib.apl_hmi_child_lock_get(self.hmi)) == CHILD_LOCK_ON

    def on_short(self, key: int) -> None:
        if not self.supplied:
            return
        self.emit_event(f"{KEY_NAMES[key]} 短按")
        self.short_count[key] += 1
        if key == KEY_POWER:
            if self.power() == POWER_ON:
                self.lib.apl_core_power_off(self.core)
                self.message = "关机"
            else:
                self.lib.apl_core_power_on(self.core)
                self.message = "开机"
        elif key == KEY_SPEED:
            gear = self.gear()
            if gear in GEAR_CYCLE:
                nxt = GEAR_CYCLE[(GEAR_CYCLE.index(gear) + 1) % len(GEAR_CYCLE)]
            else:
                nxt = GEAR_SLEEP
            self.lib.apl_core_fan_gear_set(self.core, nxt)
            self.message = GEAR_NAME[nxt]
        elif key == KEY_TIMER:
            seconds = self.timer()
            hours = 0 if seconds == 0 else (seconds + HOUR_S - 1) // HOUR_S
            hours = 0 if hours >= TIMER_HOUR_MAX else hours + 1
            if hours == 0:
                self.lib.apl_core_timer_set(self.core, 0, self.on_timer_cb, self.ctx)
                self.message = "定时取消"
            else:
                self.lib.apl_core_timer_set(
                    self.core, hours * HOUR_S, self.on_timer_cb, self.ctx
                )
                self.message = f"定时 {hours} 小时"
        elif key == KEY_LIGHT:
            self.panel_light = not self.panel_light
            self.message = "灯光开" if self.panel_light else "灯光关"
        elif key == KEY_PURIFY:
            self.message = "净化输出尚未规定"
        elif key == KEY_HUMIDITY:
            self.message = "加湿输出尚未规定"

    def on_long(self, key: int) -> None:
        if not self.supplied:
            return
        self.emit_event(f"{KEY_NAMES[key]} 长按")
        self.long_count[key] += 1
        if key == KEY_TIMER:
            nxt = CHILD_LOCK_OFF if self.locked() else CHILD_LOCK_ON
            self.lib.apl_hmi_child_lock_set(self.hmi, nxt)
            self.message = "童锁开" if nxt == CHILD_LOCK_ON else "童锁关"
        elif key == KEY_SPEED:
            if self.gear() == GEAR_TURBO:
                self.lib.apl_core_fan_gear_set(self.core, GEAR_2)
                self.message = "退出极速"
            else:
                self.lib.apl_core_fan_gear_set(self.core, GEAR_TURBO)
                self.message = "极速"
        elif key == KEY_LIGHT:
            self.message = "睡眠尚未规定"
        elif key == KEY_POWER:
            self.message = "风干尚未规定"
        elif key == KEY_PURIFY:
            self.message = "配网尚未规定"
        elif key == KEY_HUMIDITY:
            self.message = "自清洁尚未规定"

    def step(self, count: int = 1) -> None:
        if not self.supplied:
            return
        for _ in range(count):
            self.lib.apl_tick(ctypes.byref(self.apl))
            self.publish()

    def publish(self) -> None:
        """把已有的主控读数写到人机属性上。快照字段尚未规定，这里只映已有 getter。"""
        if not self.supplied:
            return
        lib = self.lib
        hmi = self.hmi
        core = self.core
        power_on = self.power() == POWER_ON
        purify = int(lib.apl_core_purify_enabled_get(core))
        humidify = int(lib.apl_core_humidify_enabled_get(core))
        lib.apl_hmi_purify_enabled_set(hmi, purify)
        lib.apl_hmi_humidify_enabled_set(hmi, humidify)
        if lib.apl_hmi_powerup_lamp_get(hmi):
            self.observe_outputs()
            return
        lib.apl_hmi_key_lamp_set(hmi, KEY_POWER, LAMP_FULL if power_on else LAMP_HALF)
        lib.apl_hmi_key_lamp_set(hmi, KEY_SPEED, LAMP_FULL if power_on else 0)
        lib.apl_hmi_key_lamp_set(hmi, KEY_TIMER, LAMP_FULL if self.timer() > 0 else 0)
        lib.apl_hmi_key_lamp_set(hmi, KEY_PURIFY, LAMP_FULL if power_on and purify else 0)
        lib.apl_hmi_key_lamp_set(hmi, KEY_HUMIDITY, LAMP_FULL if power_on and humidify else 0)
        lib.apl_hmi_key_lamp_set(hmi, KEY_LIGHT, LAMP_FULL if power_on and self.panel_light else 0)
        for index in range(1, 5):
            lit = LAMP_FULL if humidify and self.water >= index else 0
            lib.apl_hmi_water_lamp_set(hmi, index, lit)
        seconds = self.timer()
        if seconds == 0:
            text = b"   "
            unit = UNIT_NONE
        elif seconds >= HOUR_S:
            hours = min(999, seconds // HOUR_S)
            text = f"{hours:3d}".encode("ascii")
            unit = UNIT_HOUR
        else:
            text = f"{min(999, seconds):3d}".encode("ascii")
            unit = UNIT_NONE
        lib.apl_hmi_digits_brightness_set(hmi, LAMP_FULL if power_on and self.panel_light else 0)
        lib.apl_hmi_digits_content_set(hmi, text)
        lib.apl_hmi_digits_unit_set(hmi, unit)
        lib.apl_hmi_ambient_brightness_set(hmi, 0)
        self.observe_outputs()

    def lamp(self, key: int) -> int:
        return int(self.lib.apl_hmi_key_lamp_get(self.hmi, key))

    def digits(self) -> tuple[str, int, int]:
        raw = ctypes.create_string_buffer(3)
        self.lib.apl_hmi_digits_content_get(self.hmi, raw)
        unit = int(self.lib.apl_hmi_digits_unit_get(self.hmi))
        bright = int(self.lib.apl_hmi_digits_brightness_get(self.hmi))
        return raw.raw.decode("ascii"), unit, bright

    def render(self) -> list[str]:
        left = self._panel_lines()
        right = self._load_lines()
        rows = []
        for index in range(max(len(left), len(right))):
            left_row = left[index] if index < len(left) else " " * PANEL_W
            right_row = right[index] if index < len(right) else ""
            rows.append(left_row + "  " + right_row)
        rows.append("")
        rows.extend(f"{stamp}  {text}" for stamp, text in self.events[-5:])
        rows.append("手势、童锁、开关机、档位、定时和水位边沿来自 APL。q 退出")
        rows.append("1–6 按下松开是短按，按住 3 秒是长按")
        rows.append("[ ] 水位    - = PM2.5    , . 湿度    b 重新初始化    p 上电/掉电")
        return rows

    def _panel_lines(self) -> list[str]:
        inner = PANEL_W - 2
        lines = [box_top("面板", PANEL_W)]
        body = [
            [Seg(" 空气质量  快照尚未规定", "2;90")],
            self._flags(),
            self._digit_row(inner),
            self._unit_row(inner),
            self._gear_row(),
            self._water_row(),
            [Seg("")],
        ]
        for row in body:
            lines.append(box_row(row, PANEL_W))
        for row in self._ring(inner):
            lines.append(box_row(row, PANEL_W))
        lines.append(box_bottom(PANEL_W))
        return lines

    def _digit_row(self, inner: int) -> list[Seg]:
        text, _unit, bright = self.digits()
        color = "1;37" if bright else "2;90"
        return centered([Seg(text, color)], inner)

    def _unit_row(self, inner: int) -> list[Seg]:
        _text, unit, bright = self.digits()
        parts = []
        for name, value in (("H", UNIT_HOUR), ("%", 2), ("PM2.5", 3)):
            lit = bright and unit == value
            parts.append(Seg(name, "1;37" if lit else "2;90"))
            parts.append(Seg("  "))
        return centered(parts, inner)

    def _flags(self) -> list[Seg]:
        return [Seg(" "), Seg("童锁", "1;37" if self.locked() else "2;90")]

    def _gear_row(self) -> list[Seg]:
        bright = self.power() == POWER_ON
        current = self.gear()
        parts = [Seg(" ")]
        for gear, name in GEAR_NAME.items():
            lit = bright and gear == current
            parts.append(Seg(name, "1;36" if lit else "2;90"))
            parts.append(Seg(" "))
        return parts

    def _water_row(self) -> list[Seg]:
        cells = []
        shown = False
        for index in range(1, 5):
            on = self.lib.apl_hmi_water_lamp_get(self.hmi, index) != 0
            shown = shown or on
            cells.append("[#]" if on else "[ ]")
        return [Seg(" 水位 " + " ".join(cells), "1;34" if shown else "2;90")]

    def _ring(self, inner: int) -> list[list[Seg]]:
        return [
            centered([self._key_seg(KEY_PURIFY)], inner),
            spread(self._key_seg(KEY_HUMIDITY), self._key_seg(KEY_TIMER), inner),
            spread(self._key_seg(KEY_LIGHT), self._key_seg(KEY_SPEED), inner),
            centered([self._key_seg(KEY_POWER)], inner),
        ]

    def _key_seg(self, key: int) -> Seg:
        label = f"[{key + 1}{KEY_LABEL[key]}]"
        held = self.down[key]
        level = self.lamp(key)
        if held or level >= LAMP_FULL:
            return Seg(label, "1;37")
        if level > 0:
            return Seg(label, "37")
        return Seg(label, "2;90")

    def _load_lines(self) -> list[str]:
        rows = self.load_rows()
        lines = [box_top("负载", LOAD_W)]
        for name, value in rows:
            lines.append(box_row([Seg(" " + pad(name, 10) + value)], LOAD_W))
        lines.append(box_bottom(LOAD_W))
        return lines


def display_width(text: str) -> int:
    width = 0
    for ch in text:
        width += 2 if ord(ch) > 0x7F else 1
    return width


def pad(text: str, width: int) -> str:
    gap = width - display_width(text)
    if gap <= 0:
        return text
    return text + (" " * gap)


class Seg:
    def __init__(self, text: str, color: str = "") -> None:
        self.text = text
        self.color = color


def box_top(title: str, width: int) -> str:
    inner = width - 2
    label = " " + title + " "
    dash = inner - display_width(label)
    if dash < 0:
        label = label[:inner]
        dash = 0
    return "+" + label + ("-" * dash) + "+"


def box_bottom(width: int) -> str:
    return "+" + ("-" * (width - 2)) + "+"


def box_row(segments: list[Seg], width: int) -> str:
    return "|" + paint(segments, width - 2) + "|"


def centered(segments: list[Seg], inner: int) -> list[Seg]:
    visible = "".join(seg.text for seg in segments)
    gap = inner - display_width(visible)
    left = max(0, gap // 2)
    return [Seg(" " * left), *segments]


def spread(left: Seg, right: Seg, inner: int) -> list[Seg]:
    gap = inner - display_width(left.text) - display_width(right.text)
    if gap < 1:
        gap = 1
    return [left, Seg(" " * gap), right]


def paint(segments: list[Seg], width: int) -> str:
    visible = "".join(seg.text for seg in segments)
    chunks = []
    for seg in segments:
        if seg.color == "":
            chunks.append(seg.text)
        else:
            chunks.append(f"\033[{seg.color}m{seg.text}\033[0m")
    gap = width - display_width(visible)
    if gap > 0:
        chunks.append(" " * gap)
    return "".join(chunks)


def visible_text(line: str) -> str:
    out = []
    index = 0
    while index < len(line):
        if line[index] == "\033" and "[" in line[index:]:
            end = line.find("m", index)
            if end != -1:
                index = end + 1
                continue
        out.append(line[index])
        index += 1
    return "".join(out)


def assert_boxes(host: Host) -> None:
    for line in host._panel_lines():
        width = display_width(visible_text(line))
        if width != PANEL_W:
            raise AssertionError((width, visible_text(line)))
    for line in host._load_lines():
        width = display_width(visible_text(line))
        if width != LOAD_W:
            raise AssertionError((width, visible_text(line)))


def tap(host: Host, key: int) -> None:
    host.down[key] = True
    host.step(5)
    host.down[key] = False
    host.step(1)


def self_test() -> None:
    if ctypes.sizeof(KeyEventRecord) != 16 or ctypes.sizeof(InputRecord) != 20:
        raise AssertionError("控制台输入结构尺寸不对")
    if InputRecord.KeyEvent.offset != 4:
        raise AssertionError("控制台按键事件偏移不对")
    lib = ensure_dll()
    bind_api(lib)
    host = Host(lib)
    if host.power() != POWER_OFF or host.gear() != GEAR_2 or host.timer() != 0 or host.locked():
        raise AssertionError("初始状态不是关机、二档、无定时、无童锁")
    assert_boxes(host)
    text, _unit, bright = host.digits()
    if host.lib.apl_hmi_powerup_lamp_get(host.hmi) != 1 or host.lamp(KEY_POWER) != LAMP_FULL:
        raise AssertionError("上电没有全灯")
    if text != "888" or bright != LAMP_FULL:
        raise AssertionError((text, bright))
    host.step(POWERUP_TICKS - 1)
    if host.lib.apl_hmi_powerup_lamp_get(host.hmi) != 1 or host.lamp(KEY_POWER) != LAMP_FULL:
        raise AssertionError("2 秒内全灯被提前熄灭")
    host.step(1)
    text, _unit, bright = host.digits()
    if host.lib.apl_hmi_powerup_lamp_get(host.hmi) != 0 or host.lamp(KEY_POWER) != LAMP_HALF:
        raise AssertionError("2 秒后电源键不是半亮")
    if host.lamp(KEY_SPEED) != 0 or text != "   " or bright != 0:
        raise AssertionError("2 秒后其余灯没有熄灭")

    tap(host, KEY_POWER)
    if host.power() != POWER_ON or host.gear() != GEAR_2 or host.lamp(KEY_POWER) != LAMP_FULL:
        raise AssertionError("电源短按没有开机")
    tap(host, KEY_SPEED)
    if host.gear() != GEAR_3:
        raise AssertionError(host.gear())

    host.down[KEY_TIMER] = True
    host.step(LONG_PRESS_TICKS)
    if (not host.locked()) or host.long_count[KEY_TIMER] != 1:
        raise AssertionError("定时长按没有开童锁")
    host.step(20)
    if host.long_count[KEY_TIMER] != 1:
        raise AssertionError("长按重复触发")
    host.down[KEY_TIMER] = False
    host.step(1)
    if host.short_count[KEY_TIMER] != 0:
        raise AssertionError("长按松开又发了短按")

    gear_locked = host.gear()
    tap(host, KEY_SPEED)
    if host.gear() != gear_locked or host.short_count[KEY_SPEED] != 1:
        raise AssertionError("童锁没有挡住风速短按")

    host.down[KEY_TIMER] = True
    host.step(LONG_PRESS_TICKS)
    host.down[KEY_TIMER] = False
    host.step(1)
    if host.locked():
        raise AssertionError("定时长按没有关童锁")
    tap(host, KEY_SPEED)
    if host.gear() != GEAR_AUTO:
        raise AssertionError(host.gear())

    tap(host, KEY_TIMER)
    if host.timer() != HOUR_S:
        raise AssertionError(host.timer())
    text, unit, _bright = host.digits()
    if text != "  1" or unit != UNIT_HOUR:
        raise AssertionError((text, unit))
    host.step(100)
    if host.timer() != HOUR_S - 1:
        raise AssertionError(host.timer())

    lib.apl_hmi_child_lock_set(host.hmi, CHILD_LOCK_ON)
    lib.apl_core_timer_set(host.core, 1, host.on_timer_cb, host.ctx)
    host.step(100)
    if host.power() != POWER_OFF or host.timer() != 0 or host.locked():
        raise AssertionError("定时到没有关机并清童锁")
    if host.lamp(KEY_POWER) != LAMP_HALF:
        raise AssertionError("关机后电源键不是半亮")

    host.triggers = 0
    host.recovers = 0
    host.reset()
    host.water = 2
    host.step(POWERUP_TICKS)
    if host.triggers or host.recovers:
        raise AssertionError("第一拍水位不该发边沿")
    host.water = 0
    host.step(1)
    if host.triggers != 1:
        raise AssertionError("降到 0 没有触发")
    host.water = 4
    host.step(1)
    if host.recovers != 1:
        raise AssertionError("从 0 升起没有恢复")
    host.water = 1
    host.step(1)
    if host.recovers != 1 or host.triggers != 1:
        raise AssertionError("1 到 4 之间不该再发边沿")
    if host.lib.apl_hmi_water_lamp_get(host.hmi, 1) == 0:
        raise AssertionError("水位灯没有跟着宿主采样")

    host.down[KEY_LIGHT] = True
    before = host.long_count[KEY_LIGHT]
    host.step(LONG_PRESS_TICKS)
    host.step(30)
    host.down[KEY_LIGHT] = False
    host.step(1)
    if host.long_count[KEY_LIGHT] != before + 1 or host.short_count[KEY_LIGHT] != 0:
        raise AssertionError("灯光长按次数不对")
    if host.pm_reads != 0 or host.uv_called != 0 or host.anion_called != 0:
        raise AssertionError("库调用了尚未使用的传感器或 UV")
    assert_boxes(host)
    print("self-test ok")


class CharUnion(ctypes.Union):
    _fields_ = [("UnicodeChar", wintypes.WCHAR), ("AsciiChar", ctypes.c_char)]


class KeyEventRecord(ctypes.Structure):
    _fields_ = [
        ("bKeyDown", wintypes.BOOL),
        ("wRepeatCount", wintypes.WORD),
        ("wVirtualKeyCode", wintypes.WORD),
        ("wVirtualScanCode", wintypes.WORD),
        ("uChar", CharUnion),
        ("dwControlKeyState", wintypes.DWORD),
    ]


class InputRecord(ctypes.Structure):
    _fields_ = [("EventType", wintypes.WORD), ("KeyEvent", KeyEventRecord)]


kernel32 = ctypes.windll.kernel32
STD_INPUT_HANDLE = -10
STD_OUTPUT_HANDLE = -11
ENABLE_PROCESSED_INPUT = 0x0001
ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
ENABLE_WINDOW_INPUT = 0x0008
KEY_EVENT = 0x0001

kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
kernel32.GetStdHandle.restype = wintypes.HANDLE
kernel32.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.GetConsoleMode.restype = wintypes.BOOL
kernel32.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.SetConsoleMode.restype = wintypes.BOOL
kernel32.SetConsoleOutputCP.argtypes = [wintypes.UINT]
kernel32.SetConsoleOutputCP.restype = wintypes.BOOL
kernel32.GetNumberOfConsoleInputEvents.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.GetNumberOfConsoleInputEvents.restype = wintypes.BOOL
kernel32.ReadConsoleInputW.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(InputRecord),
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.ReadConsoleInputW.restype = wintypes.BOOL


def console_setup() -> tuple[int, int, int]:
    out = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
    inp = kernel32.GetStdHandle(STD_INPUT_HANDLE)
    out_mode = wintypes.DWORD()
    in_mode = wintypes.DWORD()
    if not out or not inp:
        raise SystemExit("没有可用的控制台句柄")
    if not kernel32.GetConsoleMode(out, ctypes.byref(out_mode)):
        raise SystemExit("读不到输出控制台模式")
    if not kernel32.GetConsoleMode(inp, ctypes.byref(in_mode)):
        raise SystemExit("读不到输入控制台模式")
    kernel32.SetConsoleMode(out, out_mode.value | ENABLE_VIRTUAL_TERMINAL_PROCESSING)
    kernel32.SetConsoleMode(inp, ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT)
    kernel32.SetConsoleOutputCP(65001)
    return inp, out_mode.value, in_mode.value


def read_keys(handle: int) -> list[tuple[bool, str]]:
    pending = wintypes.DWORD()
    kernel32.GetNumberOfConsoleInputEvents(handle, ctypes.byref(pending))
    events = []
    while pending.value:
        record = InputRecord()
        read = wintypes.DWORD()
        ok = kernel32.ReadConsoleInputW(handle, ctypes.byref(record), 1, ctypes.byref(read))
        pending.value -= 1
        if not ok or record.EventType != KEY_EVENT:
            continue
        ch = record.KeyEvent.uChar.UnicodeChar
        if not ch or ch == "\x00":
            continue
        events.append((bool(record.KeyEvent.bKeyDown), ch.lower()))
    return events


def apply_char(host: Host, ch: str, down: bool, latched: set[str]) -> bool:
    if ch in KEY_CHAR:
        host.down[KEY_CHAR[ch]] = down and host.supplied
        return True
    if not down:
        latched.discard(ch)
        return True
    if ch in latched:
        return True
    latched.add(ch)
    if ch == "q":
        return False
    if ch == "p":
        if host.supplied:
            host.supply_off()
        else:
            host.supply_on()
    elif ch == "b":
        host.reset()
    elif ch == "[":
        host.water = max(0, host.water - 1)
        host.message = f"水位 {host.water}/4"
    elif ch == "]":
        host.water = min(4, host.water + 1)
        host.message = f"水位 {host.water}/4"
    elif ch == "-":
        host.pm25 = max(0, host.pm25 - 10)
    elif ch == "=":
        host.pm25 = min(300, host.pm25 + 10)
    elif ch == ",":
        host.humidity = max(0, host.humidity - 5)
    elif ch == ".":
        host.humidity = min(100, host.humidity + 5)
    return True


def run() -> None:
    if not sys.stdout.isatty():
        print("请在终端里运行。", file=sys.stderr)
        sys.exit(1)
    lib = ensure_dll()
    bind_api(lib)
    host = Host(lib, Storage(ROOT / "simulator" / "data" / "nvram.bin"))
    handle, out_mode, in_mode = console_setup()
    latched: set[str] = set()
    previous = time.monotonic()
    pending = 0.0
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write("\033[?1049h\033[?25l")
        sys.stdout.flush()
        while True:
            now = time.monotonic()
            pending += now - previous
            previous = now
            ticks = int(pending / TICK_S)
            if ticks > 0:
                pending -= ticks * TICK_S
                host.step(min(ticks, 20))
            quit_requested = False
            for down, ch in read_keys(handle):
                if not apply_char(host, ch, down, latched):
                    quit_requested = True
            sys.stdout.write("\033[H")
            for line in host.render():
                sys.stdout.write("\033[2K" + line + "\n")
            sys.stdout.write("\033[J")
            sys.stdout.flush()
            if quit_requested:
                break
            time.sleep(TICK_S)
    finally:
        kernel32.SetConsoleMode(kernel32.GetStdHandle(STD_OUTPUT_HANDLE), out_mode)
        kernel32.SetConsoleMode(handle, in_mode)
        sys.stdout.write("\033[?25h\033[?1049l")
        sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="APL 面板宿主仿真")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--plain", action="store_true", help="使用旧版纯键盘面板")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.plain:
        run()
    else:
        from tui import run as run_tui
        run_tui()


if __name__ == "__main__":
    main()
