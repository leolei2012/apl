"""Windows console TUI. Mouse/keyboard levels feed the actual APL HMI."""
import ctypes
import shutil
import sys
import time
import unicodedata
from ctypes import wintypes

import panel_sim as apl


def display_text(text, columns):
    """Keep caller-provided text on one row without terminal escape controls."""
    clean = "".join(" " if unicodedata.category(char).startswith("C") or char in "\u2028\u2029" else char for char in text)
    if apl.display_width(clean) <= columns:
        return clean
    result = ""
    for char in clean:
        if apl.display_width(result + char) > columns - 3:
            break
        result += char
    return result + "..."


class Coord(ctypes.Structure):
    _fields_ = [("x", wintypes.SHORT), ("y", wintypes.SHORT)]


class Mouse(ctypes.Structure):
    _fields_ = [("position", Coord), ("buttons", wintypes.DWORD),
                ("control", wintypes.DWORD), ("flags", wintypes.DWORD)]


class EventData(ctypes.Union):
    _fields_ = [("key", apl.KeyEventRecord), ("mouse", Mouse),
                ("focus", wintypes.BOOL), ("size", Coord)]


class Record(ctypes.Structure):
    _fields_ = [("kind", wintypes.WORD), ("data", EventData)]


class Panel:
    def __init__(self, host):
        self.host = host
        self.keyboard = set()
        self.held = set()
        self.mouse_key = None
        self.mouse_down = False
        self.regions = []
        self.logs = []
        self.event_view = False
        self.event_page = 0
        self.latched = set()

    def sync(self):
        self.host.down = [self.host.supplied and (i in self.keyboard or i in self.held or i == self.mouse_key) for i in range(6)]

    def release(self):
        self.keyboard.clear()
        self.held.clear()
        self.mouse_key = None
        self.mouse_down = False
        self.latched.clear()
        self.sync()

    def toggle_supply(self):
        self.release()
        if self.host.supplied:
            self.host.supply_off()
        else:
            self.host.supply_on()
        self.observe()

    def key(self, down, char):
        if char in ("e", "[", "]") and (char == "e" or self.event_view):
            if down and char not in self.latched:
                if char == "e":
                    self.event_view = not self.event_view
                    self.event_page = 0
                elif char == "[":
                    self.event_page = min((len(self.host.events)-1)//30, self.event_page+1)
                else:
                    self.event_page = max(0, self.event_page-1)
                self.latched.add(char)
            elif not down:
                self.latched.discard(char)
            return True
        if char == "p":
            if down and char not in self.latched:
                self.toggle_supply()
                self.latched.add(char)
            elif not down:
                self.latched.discard(char)
            return True
        if char in apl.KEY_CHAR and not self.host.supplied:
            return True
        if char in apl.KEY_CHAR:
            key = apl.KEY_CHAR[char]
            if down:
                self.keyboard.add(key)
            else:
                self.keyboard.discard(key)
            self.sync()
        elif down and char == "\x1b":
            self.release()
        elif down and char == "b":
            self.release()
            self.host.reset()
        else:
            return apl.apply_char(self.host, char, down, self.latched)
        return True

    def mouse(self, x, y, down):
        if down and not self.mouse_down:
            for left, top, width, action, value in self.regions:
                if top == y and left <= x < left + width:
                    if action in ("key", "hold") and not self.host.supplied:
                        break
                    if action == "supply":
                        self.toggle_supply()
                    elif action == "key":
                        self.mouse_key = value
                    elif action == "hold":
                        if value in self.held:
                            self.held.remove(value)
                        else:
                            self.held.add(value)
                    elif action == "sensor":
                        name, delta, lower, upper = value
                        setattr(self.host, name, max(lower, min(upper, getattr(self.host, name) + delta)))
                    elif action == "release":
                        self.release()
                    elif action == "reset":
                        self.release()
                        self.host.reset()
                    break
        elif not down:
            self.mouse_key = None
        self.mouse_down = down
        self.sync()

    def observe(self):
        self.logs = [stamp + "  " + text for stamp, text in self.host.events]

    def render(self, width, height):
        self.regions = []
        if width < 126 or height < 40:
            self.release()
            return "\033[H\033[J请把终端放大到至少 126 列 × 40 行。Q 退出。"
        self.observe()
        h = self.host
        output = ["\033[H\033[48;5;234m\033[J"]

        def put(x, y, text, style="37;40"):
            output.append(f"\033[{y+1};{x+1}H\033[{style}m{text}\033[0m")

        def button(x, y, text, action, value=None, active=False):
            put(x, y, text, "1;30;46" if active else "37;48;5;238")
            self.regions.append((x, y, apl.display_width(text), action, value))

        if self.event_view:
            put(3,1,"事件记录 / E 返回 / [ 更早 / ] 更新", "1;36;48;5;234")
            end = max(0, len(self.logs) - self.event_page * 30)
            for row, line in enumerate(self.logs[max(0,end-30):end]):
                put(3,3+row,display_text(line,width-7),"37;48;5;234")
            return "".join(output)

        put(3, 1, "APL  /  PANEL SIMULATOR", "1;36;48;5;234")
        put(87, 1, "真实 C 库 · 10 ms", "90;48;5;234")
        # A terminal cell is roughly twice as tall as it is wide.
        for y in range(3, 34):
            half = int(39 * max(0, 1 - ((y - 18) / 15.5) ** 2) ** .5)
            put(40-half, y, " " * (half*2+1), "40")
        put(39, 3, "│", "90;40")
        positions = ((34,5),(7,12),(63,12),(7,23),(63,23),(34,28))
        labels = ("Purify", "Humidity", "Timer", "Light", "Speed", "Power")
        subtitles = ("Wifi (3s)", "Clean (3s)", "Lock (3s)", "", "Turbo (3s)", "Stop dry (3s)")
        for key, (x,y) in enumerate(positions):
            style = "1;30;46" if h.down[key] else ("1;37;40" if h.lamp(key) else "90;40")
            put(x,y+2,f"{key+1} {labels[key]}",style)
            put(x,y+3,subtitles[key],"90;40")
            for row in range(5):
                self.regions.append((x,y+row,12,"key",key))
        # Unmapped indicators retain a dim outline until APL provides their state.
        put(27,14,"-o", "90;40")
        put(27,16,"WiFi", "90;40")
        put(27,19,"FLT", "90;40")
        put(27,22,"LOCK", "1;36;40" if h.locked() else "90;40")
        put(52,14,"(UV)", "90;40")
        h.lib.apl_hmi_ambient_brightness_get.argtypes = [ctypes.POINTER(apl.Hmi)]
        h.lib.apl_hmi_ambient_brightness_get.restype = ctypes.c_uint8
        ambient = h.lib.apl_hmi_ambient_brightness_get(h.hmi)
        for y in (13,14):
            put(33,y,"· " * 9,"36;40" if ambient else "90;40")
        digits, unit, bright = h.digits()
        segments = {"0":"ab cdef".replace(" ",""), "1":"bc", "2":"abdeg",
                    "3":"abcdg", "4":"bcfg", "5":"acdfg", "6":"acdefg",
                    "7":"abc", "8":"abcdefg", "9":"abcdfg"}
        for index, char in enumerate(digits):
            x = 33 + index*6
            enabled = segments.get(char, "") if bright else ""
            for segment, dx, dy, glyph in (("a",1,0,"━━━"),("b",4,1,"┃"),
                ("c",4,3,"┃"),("d",1,4,"━━━"),("e",0,3,"┃"),
                ("f",0,1,"┃"),("g",1,2,"━━━")):
                put(x+dx,16+dy,glyph,"1;37;40" if segment in enabled else "38;5;236;40")
        for y,label,value in ((16,"H",1),(19,"%",2),(22,"PM2.5",3)):
            put(52,y,label,"1;37;40" if bright and unit == value else "90;40")
        for x,label,gear in ((27,"Sleep",0),(34,"~",1),(39,"~~",2),(45,"~~~",3),(52,"Auto",4)):
            put(x,24,label,"1;37;40" if h.power() and h.gear() == gear else "90;40")
        for i in range(4):
            lit = h.lib.apl_hmi_water_lamp_get(h.hmi, i+1)
            put(33+i*5,26,"━━━━","1;37;40" if lit else "38;5;236;40")
        put(27,26,"(*)","90;40")
        put(54,26,"[~]","90;40")

        put(87,4,"传感器输入", "1;37;48;5;234")
        for row, (name,label,delta,lower,upper,suffix) in enumerate((
            ("water","水位",1,0,4," / 4"),("pm25","PM2.5",10,0,1000," ug/m3"),
            ("humidity","湿度",5,0,100," %"),("temperature","温度",1,-20,60," C"))):
            y = 6 + row*2
            put(87,y,label,"37;48;5;234")
            button(95,y," - ","sensor",(name,-delta,lower,upper))
            put(100,y,str(getattr(h,name))+suffix,"37;48;5;234")
            button(117,y," + ","sensor",(name,delta,lower,upper))
        put(87,14,"固定负载", "1;37;48;5;234")
        for row, (label, value) in enumerate(h.load_rows()):
            put(87,15+row,display_text(f"{label}: {value}",width-88),"37;48;5;234")
        put(87,21,"自定义信息", "1;37;48;5;234")
        for slot, entry in enumerate(h.display_slots):
            text = "--" if entry is None else f"{entry[0]}: {entry[1]}"
            put(87,22+slot,display_text(text, width-88),"90;48;5;234" if entry is None else "37;48;5;234")
        put(87,29,"事件记录 [E 展开]", "1;37;48;5;234")
        for i,line in enumerate(self.logs[-3:]):
            put(87,30+i,display_text(line,width-88),"37;48;5;234")
        put(3,35,"保持按下：", "37;48;5;234")
        for key in range(6):
            button(15+key*7,35,f" {key+1} {'ON' if key in self.held else '--'} ","hold",key,key in self.held)
        button(61,35," 释放 [Esc] ","release")
        button(76,35," 重置 [B] ","reset")
        button(91,35," 断电 [P] " if h.supplied else " 上电 [P] ","supply",active=h.supplied)
        put(3,37,"鼠标按住 / 键盘 1-6：短按与 3 秒长按；保持可多选；E 事件；Q 退出", "90;48;5;234")
        put(3,38,"暗色图标为未点亮；WiFi / 滤网 / UV 等未映射状态仅占位。净化、加湿输出待实现。", "90;48;5;234")
        return "".join(output)


def run():
    if not sys.stdout.isatty():
        raise SystemExit("请在 Windows Terminal 或控制台里运行。")
    lib = apl.ensure_dll()
    apl.bind_api(lib)
    panel = Panel(apl.Host(lib, apl.Storage(apl.ROOT / "emulator" / "data" / "nvram.bin")))
    handle, out_mode, in_mode = apl.console_setup()
    # Extended flags disable Quick Edit; mouse/focus events stay in the input queue.
    apl.kernel32.SetConsoleMode(handle, 0x0080 | 0x0010 | 0x0008 | 0x0001)
    read_input = apl.kernel32.ReadConsoleInputW
    old_args = read_input.argtypes
    read_input.argtypes = [wintypes.HANDLE, ctypes.POINTER(Record), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdout.write("\033[?1049h\033[?25l")
    previous = time.monotonic()
    pending_time = 0
    paint_at = 0
    running = True
    try:
        while running:
            now = time.monotonic()
            pending_time += now - previous
            previous = now
            ticks = min(int(pending_time / .01), 20)
            if ticks:
                panel.host.step(ticks)
                pending_time = max(0, pending_time - ticks * .01)
                pending_time = min(pending_time, .2)
                panel.observe()
            count = wintypes.DWORD()
            apl.kernel32.GetNumberOfConsoleInputEvents(handle, ctypes.byref(count))
            for _ in range(count.value):
                record, read = Record(), wintypes.DWORD()
                if not read_input(handle, ctypes.byref(record), 1, ctypes.byref(read)):
                    break
                if record.kind == 1:
                    event = record.data.key
                    running = panel.key(bool(event.bKeyDown), event.uChar.UnicodeChar.lower()) and running
                elif record.kind == 2:
                    event = record.data.mouse
                    if event.flags in (0, 1, 2):
                        panel.mouse(event.position.x, event.position.y, bool(event.buttons & 1))
                elif record.kind == 16 and not record.data.focus:
                    panel.release()
            if now >= paint_at:
                size = shutil.get_terminal_size()
                sys.stdout.write(panel.render(size.columns, size.lines))
                sys.stdout.flush()
                paint_at = now + .05
            time.sleep(.005)
    finally:
        panel.release()
        read_input.argtypes = old_args
        apl.kernel32.SetConsoleMode(apl.kernel32.GetStdHandle(apl.STD_OUTPUT_HANDLE), out_mode)
        apl.kernel32.SetConsoleMode(handle, in_mode)
        sys.stdout.write("\033[0m\033[?25h\033[?1049l")
        sys.stdout.flush()


if __name__ == "__main__":
    run()

