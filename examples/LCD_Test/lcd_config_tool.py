#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ST7735 快速配置工具（LCD_Test 示例工程）
==========================================

功能：
  1. 查看 / 修改 ST7735 显示参数（User/ST7735/st7735_config.h）
     - 面板预设 A / B / C（对应 POT2 工程的面板批次）
     - 旋转方向 0 / 90 / 180 / 270
     - 列/行偏移、反色、逻辑分辨率
  2. 修改上电演示文本（Core/SRC/main.c 中的两行 DrawString）

用法：
  python lcd_config_tool.py                     进入交互菜单
  python lcd_config_tool.py get                 查看当前配置
  python lcd_config_tool.py preset A|B|C        切换面板预设
  python lcd_config_tool.py rotation 0|90|180|270   切换旋转
  python lcd_config_tool.py offset X Y          设置偏移
  python lcd_config_tool.py invert 0|1          设置反色
  python lcd_config_tool.py size W H            设置逻辑分辨率
  python lcd_config_tool.py text <1|2> <内容> [x] [y] [前景色] [背景色]
  python lcd_config_tool.py help                显示帮助
"""

import re
import sys
from pathlib import Path

# 强制 UTF-8 输出，保证中文界面在 Windows 终端正常显示
for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

PROJECT_ROOT = Path(__file__).resolve().parent

CONFIG_FILE = PROJECT_ROOT / "User" / "ST7735" / "st7735_config.h"
MAIN_C_FILE = PROJECT_ROOT / "Core" / "SRC" / "main.c"

# MADCTL 位定义
MADCTL_MY = 0x80
MADCTL_MX = 0x40
MADCTL_MV = 0x20
MADCTL_ML = 0x10
MADCTL_BGR = 0x08
MADCTL_MH = 0x04

# 面板预设：键为旋转角度，值为 (madctl, width, height, xOffset, yOffset)
# 数值与 POT2 工程 LoveFinderLib/ST7735/ST7735_Config_Panel*.hpp 保持一致
PANELS = {
    "A": {
        "desc": "原批次（BGR，偏移 0/24，无反色，默认 DEG_0 横屏 160x80）",
        "defaultRotation": 0,
        "invert": 0,
        "rotations": {
            0:   (MADCTL_MX | MADCTL_MV | MADCTL_BGR, 160, 80, 0, 24),
            90:  (MADCTL_MX | MADCTL_MY | MADCTL_BGR, 80, 160, 24, 0),
            180: (MADCTL_MY | MADCTL_MV | MADCTL_BGR, 160, 80, 0, 24),
            270: (MADCTL_BGR, 80, 160, 24, 0),
        },
    },
    "B": {
        "desc": "新批次（RGB，偏移 1/26，需反色，默认 DEG_180 横屏 160x80）",
        "defaultRotation": 180,
        "invert": 1,
        "rotations": {
            0:   (MADCTL_MY | MADCTL_MX | MADCTL_MV, 160, 80, 1, 26),
            90:  (MADCTL_MX | MADCTL_MY, 80, 160, 24, 0),
            180: (MADCTL_MV, 160, 80, 1, 26),
            270: (0x00, 80, 160, 24, 0),
        },
    },
    "C": {
        "desc": "新批次镜像（BGR，偏移 1/26，需反色，默认 DEG_0 横屏 160x80）",
        "defaultRotation": 0,
        "invert": 1,
        "rotations": {
            0:   (MADCTL_MY | MADCTL_MV | MADCTL_BGR, 160, 80, 1, 26),
            90:  (MADCTL_MY | MADCTL_BGR, 80, 160, 24, 0),
            180: (MADCTL_MX | MADCTL_MV | MADCTL_BGR, 160, 80, 1, 26),
            270: (MADCTL_MX | MADCTL_BGR, 80, 160, 24, 0),
        },
    },
}

# st7735_config.h 中各宏的正则
_MACRO_RE = {
    "width":  re.compile(r"^#define\s+ST7735_WIDTH\s+([0-9]+)", re.MULTILINE),
    "height": re.compile(r"^#define\s+ST7735_HEIGHT\s+([0-9]+)", re.MULTILINE),
    "madctl": re.compile(r"^#define\s+ST7735_MADCTL\s+(0x[0-9A-Fa-f]+|[0-9]+)", re.MULTILINE),
    "xoff":   re.compile(r"^#define\s+ST7735_X_OFFSET\s+([0-9]+)", re.MULTILINE),
    "yoff":   re.compile(r"^#define\s+ST7735_Y_OFFSET\s+([0-9]+)", re.MULTILINE),
    "invert": re.compile(r"^#define\s+ST7735_INVERT\s+([0-9]+)", re.MULTILINE),
}

# main.c 中演示文本：ST7735_DrawString(x, y, "text", fg, bg)
_TEXT_RE = re.compile(
    r'ST7735_DrawString\(\s*(\d+)\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*'
    r"(ST7735_[A-Z_]+)\s*,\s*(ST7735_[A-Z_]+)\s*\)"
)

VALID_COLORS = {
    "ST7735_BLACK", "ST7735_WHITE", "ST7735_RED", "ST7735_GREEN", "ST7735_BLUE",
    "ST7735_CYAN", "ST7735_MAGENTA", "ST7735_YELLOW", "ST7735_ORANGE",
}


class ToolError(Exception):
    pass


def read_text(path: Path) -> str:
    if not path.exists():
        raise ToolError(f"文件不存在：{path}")
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def _parse_int(token: str) -> int:
    token = token.strip()
    return int(token, 16) if token.lower().startswith("0x") else int(token)


def _fmt_hex(value: int) -> str:
    return f"0x{value:02X}"


def _join_colors() -> str:
    return "、".join(sorted(VALID_COLORS))


# =====================================================================
# 读取 / 写入 st7735_config.h
# =====================================================================

def get_config(text=None):
    """读取全部显示参数。"""
    text = text or read_text(CONFIG_FILE)
    out = {}
    for key, rx in _MACRO_RE.items():
        m = rx.search(text)
        if not m:
            raise ToolError(f"st7735_config.h 中缺少 ST7735_{key.upper()} 宏")
        out[key] = _parse_int(m.group(1))
    return out


def apply_values(madctl, width, height, xoff, yoff, invert):
    """一次性写入全部参数到 st7735_config.h。"""
    text = read_text(CONFIG_FILE)
    values = {
        "width": str(width),
        "height": str(height),
        "madctl": _fmt_hex(madctl),
        "xoff": str(xoff),
        "yoff": str(yoff),
        "invert": str(invert),
    }
    for key, rx in _MACRO_RE.items():
        if not rx.search(text):
            raise ToolError(f"st7735_config.h 中缺少 ST7735_{key.upper()} 宏")
        text = rx.sub(
            lambda m, k=key: m.group(0)[: m.start(1) - m.start()] + values[k] + m.group(0)[m.end(1) - m.start():],
            text,
        )
    write_text(CONFIG_FILE, text)


def panel_for(text=None):
    """根据当前配置反查最接近的面板预设 + 旋转角度，找不到返回 None。"""
    cfg = get_config(text)
    for name, panel in PANELS.items():
        for deg, (madctl, w, h, xo, yo) in panel["rotations"].items():
            if (cfg["madctl"] == madctl and cfg["width"] == w and cfg["height"] == h
                    and cfg["xoff"] == xo and cfg["yoff"] == yo
                    and cfg["invert"] == panel["invert"]):
                return name, deg
    return None


# =====================================================================
# 演示文本（main.c）
# =====================================================================

def get_texts(text=None):
    """解析 main.c 中的 ST7735_DrawString 调用。"""
    text = text or read_text(MAIN_C_FILE)
    out = []
    for m in _TEXT_RE.finditer(text):
        out.append({
            "x": int(m.group(1)),
            "y": int(m.group(2)),
            "str": m.group(3),
            "fg": m.group(4),
            "bg": m.group(5),
        })
    return out


def set_text(line, content, x=None, y=None, fg=None, bg=None):
    """修改 main.c 中第 line 行（1 起）的演示文本；None 表示保持原值。"""
    text = read_text(MAIN_C_FILE)
    matches = list(_TEXT_RE.finditer(text))
    if not matches:
        raise ToolError("main.c 中未找到 ST7735_DrawString(...) 调用")
    if line < 1 or line > len(matches):
        raise ToolError(f"行号无效：{line}（实际找到 {len(matches)} 行）")

    m = matches[line - 1]
    nx = x if x is not None else int(m.group(1))
    ny = y if y is not None else int(m.group(2))
    nfg = fg if fg else m.group(4)
    nbg = bg if bg else m.group(5)
    for col in (nfg, nbg):
        if col not in VALID_COLORS:
            raise ToolError(f"未知颜色名：{col}（可用：{_join_colors()}）")

    new_call = f'ST7735_DrawString({nx}, {ny}, "{content}", {nfg}, {nbg})'
    text = text[: m.start()] + new_call + text[m.end():]
    write_text(MAIN_C_FILE, text)


# =====================================================================
# 命令实现
# =====================================================================

def cmd_get():
    cfg = get_config()
    texts = get_texts()
    matched = panel_for()
    color_order = "BGR" if cfg["madctl"] & MADCTL_BGR else "RGB"
    print("当前 ST7735 配置（st7735_config.h）：")
    print(f"  逻辑分辨率 : {cfg['width']} x {cfg['height']}")
    print(f"  MADCTL     : {_fmt_hex(cfg['madctl'])}（颜色序: {color_order}）")
    print(f"  偏移 X/Y   : {cfg['xoff']} / {cfg['yoff']}")
    print(f"  反色       : {'开' if cfg['invert'] else '关'}")
    if matched:
        print(f"  匹配预设   : Panel {matched[0]} @ {matched[1]} 度")
    print("演示文本（main.c）：")
    for i, t in enumerate(texts, 1):
        print(f"  {i}. ({t['x']},{t['y']}) \"{t['str']}\" {t['fg']} / {t['bg']}")
    return 0


def cmd_preset(target):
    target = target.upper()
    if target not in PANELS:
        raise ToolError(f"未知面板预设 \"{target}\"，可用：{'、'.join(PANELS)}")
    panel = PANELS[target]
    deg = panel["defaultRotation"]
    madctl, w, h, xo, yo = panel["rotations"][deg]
    apply_values(madctl, w, h, xo, yo, panel["invert"])
    print(f"已切换为 Panel {target}（{panel['desc']}）")
    print(f"配置写入：{CONFIG_FILE}")
    return 0


def cmd_rotation(deg):
    if deg not in (0, 90, 180, 270):
        raise ToolError("旋转角度仅支持 0 / 90 / 180 / 270")
    matched = panel_for()
    if not matched:
        print("提示：当前配置不匹配任何预设，将在 Panel A 基础上切换旋转。")
        name = "A"
    else:
        name = matched[0]
    panel = PANELS[name]
    madctl, w, h, xo, yo = panel["rotations"][deg]
    apply_values(madctl, w, h, xo, yo, panel["invert"])
    print(f"已切换旋转方向：{deg} 度（Panel {name}，{w}x{h}）")
    print(f"配置写入：{CONFIG_FILE}")
    return 0


def cmd_offset(x, y):
    cfg = get_config()
    apply_values(cfg["madctl"], cfg["width"], cfg["height"], x, y, cfg["invert"])
    print(f"已设置偏移：X={x}, Y={y}")
    return 0


def cmd_invert(value):
    if value not in (0, 1):
        raise ToolError("反色值仅支持 0（关）或 1（开）")
    cfg = get_config()
    apply_values(cfg["madctl"], cfg["width"], cfg["height"], cfg["xoff"], cfg["yoff"], value)
    print(f"已设置反色：{'开' if value else '关'}")
    return 0


def cmd_size(w, h):
    if w <= 0 or h <= 0 or w > 240 or h > 320:
        raise ToolError("分辨率超出合理范围（1..240 x 1..320）")
    cfg = get_config()
    apply_values(cfg["madctl"], w, h, cfg["xoff"], cfg["yoff"], cfg["invert"])
    print(f"已设置逻辑分辨率：{w} x {h}")
    return 0


def cmd_text(args):
    if len(args) < 2:
        raise ToolError("用法：text <1|2> <内容> [x] [y] [前景色] [背景色]")
    try:
        line = int(args[0])
    except ValueError:
        raise ToolError(f"行号无效：{args[0]}")
    content = args[1]
    extra = args[2:]
    try:
        x = int(extra[0]) if len(extra) > 0 else None
        y = int(extra[1]) if len(extra) > 1 else None
    except ValueError:
        raise ToolError("坐标必须是整数")
    fg = extra[2].upper() if len(extra) > 2 else None
    bg = extra[3].upper() if len(extra) > 3 else None
    set_text(line, content, x, y, fg, bg)
    print(f"已修改演示文本第 {line} 行：\"{content}\"")
    print(f"源文件：{MAIN_C_FILE}")
    return 0


def prompt(text):
    try:
        return input(text).strip()
    except EOFError:
        return ""


def interactive_menu():
    while True:
        try:
            cfg = get_config()
            matched = panel_for()
            panel_line = f"Panel {matched[0]} @ {matched[1]} 度" if matched else "自定义配置"
            cfg_line = (
                f"            {cfg['width']}x{cfg['height']} "
                f"MADCTL={_fmt_hex(cfg['madctl'])} "
                f"偏移({cfg['xoff']},{cfg['yoff']}) "
                f"反色{'开' if cfg['invert'] else '关'}"
            )
        except ToolError as e:
            cfg = None
            panel_line = f"读取失败（{e}）"
            cfg_line = ""
        print("=" * 54)
        print("      ST7735 快速配置工具（LCD_Test）")
        print("=" * 54)
        print(f"  当前配置：{panel_line}")
        if cfg:
            print(cfg_line)
        print("  1. 切换面板预设（A / B / C）")
        print("  2. 切换旋转方向（0 / 90 / 180 / 270）")
        print("  3. 修改偏移（offset X Y）")
        print("  4. 切换反色（invert 0/1）")
        print("  5. 修改逻辑分辨率（size W H）")
        print("  6. 修改演示文本（text）")
        print("  0. 退出")
        print("=" * 54)
        choice = prompt("请选择（0-6）：").lower()

        if choice == "1":
            print("可用面板预设：")
            for name, panel in PANELS.items():
                print(f"  {name}：{panel['desc']}")
            target = prompt(f"请输入面板预设（{'/'.join(PANELS)}）：")
            try:
                cmd_preset(target)
            except ToolError as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice == "2":
            deg = prompt("请输入旋转角度（0/90/180/270）：")
            try:
                cmd_rotation(int(deg))
            except (ValueError, ToolError) as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice == "3":
            x = prompt("X 偏移：")
            y = prompt("Y 偏移：")
            try:
                cmd_offset(int(x), int(y))
            except (ValueError, ToolError) as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice == "4":
            v = prompt("反色 0/1（0=关 1=开）：")
            try:
                cmd_invert(int(v))
            except (ValueError, ToolError) as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice == "5":
            w = prompt("宽度：")
            h = prompt("高度：")
            try:
                cmd_size(int(w), int(h))
            except (ValueError, ToolError) as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice == "6":
            try:
                texts = get_texts()
                for i, t in enumerate(texts, 1):
                    print(f"  {i}. ({t['x']},{t['y']}) \"{t['str']}\" {t['fg']}/{t['bg']}")
            except ToolError as e:
                print(f"错误：{e}", file=sys.stderr)
                continue
            n = prompt("要修改哪一行（1/2，回车跳过）：")
            if not n:
                continue
            try:
                line = int(n)
                new_text = prompt(f"第 {line} 行新内容（回车保持不变）：")
                texts0 = get_texts()
                if line < 1 or line > len(texts0):
                    print("行号无效。")
                    continue
                if not new_text:
                    new_text = texts0[line - 1]["str"]
                set_text(line, new_text)
                print("已更新。")
            except (ValueError, ToolError) as e:
                print(f"错误：{e}", file=sys.stderr)
        elif choice in ("0", "q", "quit", "exit"):
            print("再见！")
            break
        else:
            print("无效输入，请重试。")


# =====================================================================
# 入口
# =====================================================================

def main():
    args = [a for a in sys.argv[1:] if a]

    if not args:
        interactive_menu()
        return 0

    cmd = args[0].lower()

    if cmd in ("help", "-h", "--help"):
        print(__doc__)
        return 0
    if cmd in ("get", "status", "show"):
        try:
            return cmd_get()
        except ToolError as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd in ("preset", "panel", "setpanel"):
        if len(args) < 2:
            print("用法：python lcd_config_tool.py preset A|B|C", file=sys.stderr)
            return 1
        try:
            return cmd_preset(args[1])
        except ToolError as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd in ("rotation", "rot"):
        if len(args) < 2:
            print("用法：python lcd_config_tool.py rotation 0|90|180|270", file=sys.stderr)
            return 1
        try:
            return cmd_rotation(int(args[1]))
        except (ValueError, ToolError) as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd == "offset":
        if len(args) < 3:
            print("用法：python lcd_config_tool.py offset X Y", file=sys.stderr)
            return 1
        try:
            return cmd_offset(int(args[1]), int(args[2]))
        except (ValueError, ToolError) as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd == "invert":
        if len(args) < 2:
            print("用法：python lcd_config_tool.py invert 0|1", file=sys.stderr)
            return 1
        try:
            return cmd_invert(int(args[1]))
        except (ValueError, ToolError) as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd == "size":
        if len(args) < 3:
            print("用法：python lcd_config_tool.py size W H", file=sys.stderr)
            return 1
        try:
            return cmd_size(int(args[1]), int(args[2]))
        except (ValueError, ToolError) as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1
    if cmd == "text":
        try:
            return cmd_text(args[1:])
        except ToolError as e:
            print(f"错误：{e}", file=sys.stderr)
            return 1

    print(f"未知命令 \"{cmd}\"，输入 \"python {Path(__file__).name} help\" 查看帮助。", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())