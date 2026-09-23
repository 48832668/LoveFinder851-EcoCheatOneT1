#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fontlib.py — 从库的 font_manifest.json 生成库的 font.h / font_data.cpp

架构 (库共享 · 工程只存编译清单)
================================
  库   LoveFinderLibForPY32_LL/FontLib/
         font_manifest.json   唯一真相源: 字体名册 + 全部字形像素
         font.h               生成物: 固定接口 (字体名册 + 查表 API)
         font_data.cpp        生成物: 全部字形的逐字符数组 + X-macro 查表

  工程 <proj>/LoveFinderLib/FontLib/
         font_config.hpp      手写/由 PickSoul 生成: 只有「要编译哪些字」

工作原理
========
font_data.cpp 持有该字体【全部】字形的 static 数组, 但查表函数的 switch
只展开工程清单里的字符:

    #define FONT_CASE(cp) case cp: return g_7x10_cp##cp;
        FONT_7X10_CHARS(FONT_CASE)      // <- 来自工程的 font_config.hpp
    #undef FONT_CASE

未被引用的 static 数组由编译器直接丢弃 (armclang -O2 即可, 无需 LTO)。
已实测 (armclang V6.24 / Cortex-M0+ / -O2):
    白名单 25 字符 -> map 中只保留 25 个字形, 字形 RO = 500 B
    白名单 95 字符 -> map 中保留 95 个字形, 字形 RO = 1900 B

铁律
====
库的 FontLib 目录里【绝不能】出现 font_config.hpp —— 那是工程的文件。
库内文件用 #include "font_config.hpp" 时, 因为库目录没有同名文件,
会经 -I 路径解析到工程的清单。这就是「完整剥离」的接口。

用法
====
    python tools/gen_fontlib.py
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTLIB = os.path.join(ROOT, 'LoveFinderLibForPY32_LL', 'FontLib')
MANIFEST = os.path.join(FONTLIB, 'font_manifest.json')
OUT_H = os.path.join(FONTLIB, 'font.h')
OUT_CPP = os.path.join(FONTLIB, 'font_data.cpp')

# 生成物头部统一警示
BANNER = """/**
 * @file {fname}
 * @brief {desc} (生成物 — 请勿手工编辑)
 * @author LoveFinder
 *
 * 本文件由 PickSoul (FontHub Editor) 从 font_manifest.json (唯一真相源) 生成。
 * (离线等价工具: tools/gen_fontlib.py —— 两者输出逐字节一致)
 * 字形像素数据属于【共享库】, 修改请通过 PickSoul 编辑库, 所有例程一起更新。
 *
 * 本目录【不含】font_config.hpp —— 那是每个工程自己的编译清单,
 * 经 -I 路径提供 (见各工程 LoveFinderLib/FontLib/font_config.hpp)。
 */"""


# ---------------------------------------------------------------------------
# 命名
# ---------------------------------------------------------------------------
def macro_base(name):
    """Font_7x10 -> FONT_7X10 ; Font_ZH_16x16 -> FONT_ZH_16X16"""
    return re.sub(r'[^0-9A-Za-z_]', '_', name).upper()


def ident_base(name):
    """Font_7x10 -> 7x10 ; Font_ZH_16x16 -> zh_16x16  (用于 C 标识符后缀)"""
    s = name[5:] if name.startswith('Font_') else name
    return re.sub(r'[^0-9A-Za-z_]', '_', s).lower()


def glyph_var(font_name, codepoint):
    """Font_7x10, 65 -> g_7x10_cp65"""
    return 'g_%s_cp%d' % (ident_base(font_name), codepoint)


def lookup_fn(font_name):
    return 'font_lookup_%s' % ident_base(font_name)


def chars_macro(font_name):
    return '%s_CHARS' % macro_base(font_name)


def use_macro(font_name):
    return 'USE_%s' % macro_base(font_name)


# ---------------------------------------------------------------------------
# 生成 font.h
# ---------------------------------------------------------------------------
def gen_header(fonts):
    out = [BANNER.format(fname='font.h',
                         desc='FontLib 公共接口 — 驱动无关 (字体名册 + 查表 API)')]
    out.append('')
    out.append('#ifndef FONT_H')
    out.append('#define FONT_H')
    out.append('')
    out.append('#include <stdint.h>')
    out.append('')
    out.append('/* 工程编译清单 (每个工程自己的 LoveFinderLib/FontLib/font_config.hpp) */')
    out.append('#include "font_config.hpp"')
    out.append('')
    out.append('/*============================================================================')
    out.append(' * 库的字体名册')
    out.append(' *   每个字体都是一套【唯一】的像素数据, 放在本库中, 所有工程共享。')
    out.append(' *   工程用 USE_<FONT> 决定是否编译, 用 <FONT>_CHARS(X) 决定编译哪些字。')
    out.append(' *   未声明的字体默认关闭。')
    out.append(' *==========================================================================*/')
    for f in fonts:
        m = use_macro(f['name'])
        out.append('#ifndef %s' % m)
        out.append('#define %s 0' % m)
        out.append('#endif')
    out.append('')
    out.append('/*============================================================================')
    out.append(' * FontDef — 字库统一描述结构')
    out.append(' *   本库所有字体都是字符级编译 (data == nullptr, 走 font_get_glyph 查表)。')
    out.append(' *==========================================================================*/')
    out.append('typedef struct FontDef {')
    out.append('    const uint8_t width;')
    out.append('    uint8_t height;')
    out.append('    const uint16_t *data;')
    out.append('} FontDef;')
    out.append('')
    out.append('/* --- 字体实例 (由工程清单启用) --- */')
    for f in fonts:
        out.append('#if %s' % use_macro(f['name']))
        out.append('extern FontDef %s;   /* %dx%d %s, %d 个字形可用 */'
                   % (f['name'], f['width'], f['height'], f['type'], len(f['glyphs'])))
        out.append('#endif')
    out.append('')
    out.append('/*============================================================================')
    out.append(' * 字形查表 (字符级编译)')
    out.append(' *   未编译的字符返回 nullptr -> 渲染端自动跳过 (不显示也不占位)')
    out.append(' *==========================================================================*/')
    out.append('/** 按 ASCII 码查字模 (ASCII 字体专用) */')
    out.append('const uint16_t* font_get_glyph(const FontDef& font, uint8_t ch);')
    out.append('')
    out.append('/** 按 Unicode 码点查字模 (汉字等非 ASCII 字体专用) */')
    out.append('const uint16_t* font_get_glyph_unicode(const FontDef& font, uint16_t uni);')
    out.append('')
    out.append('#endif /* FONT_H */')
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------------------
# 生成 font_data.cpp
# ---------------------------------------------------------------------------
def gen_source(fonts):
    out = [BANNER.format(fname='font_data.cpp',
                         desc='FontLib 字形数据 — 全部字形的逐字符数组 + 查表')]
    out.append('')
    out.append('#include "font.h"')
    out.append('')
    total = 0
    for f in fonts:
        name = f['name']
        use = use_macro(name)
        h = f['height']
        out.append('')
        out.append('/*============================================================================')
        out.append(' * %s — %dx%d %s, 全部 %d 个字形 (本库唯一副本)'
                   % (name, f['width'], f['height'], f['type'], len(f['glyphs'])))
        out.append(' *==========================================================================*/')
        out.append('#if %s' % use)
        out.append('')
        for g in f['glyphs']:
            cp = g['codepoint']
            var = glyph_var(name, cp)
            out.append('static const uint16_t %s[%d] = {   /* %s */'
                       % (var, h, g['char']))
            rows = g['bitmap']
            for i in range(0, len(rows), 4):
                out.append('    ' + ', '.join(rows[i:i + 4]) + ',')
            out.append('};')
            total += 1
        out.append('')
        # 查表函数
        is_uni = (f['type'] == 'unicode')
        param_t = 'uint16_t uni' if is_uni else 'uint8_t ch'
        out.append('static const uint16_t* %s(%s) {' % (lookup_fn(name), param_t))
        out.append('    switch (%s) {' % ('uni' if is_uni else 'ch'))
        out.append('#define FONT_CASE(cp) case cp: return %s_cp##cp;'
                   % ('g_' + ident_base(name)))
        out.append('        %s(FONT_CASE)   /* <- 工程的编译清单 */' % chars_macro(name))
        out.append('#undef FONT_CASE')
        out.append('        default: return nullptr;   /* 未编译的字符 -> 渲染端跳过 */')
        out.append('    }')
        out.append('}')
        out.append('')
        out.append('FontDef %s = {%d, %d, nullptr};' % (name, f['width'], f['height']))
        out.append('')
        out.append('#endif /* %s */' % use)
    # 分发
    out.append('')
    out.append('/*============================================================================')
    out.append(' * 通用字形分发 — 供渲染端调用 (ST7735 等驱动只依赖 font.h)')
    out.append(' *==========================================================================*/')
    out.append('const uint16_t* font_get_glyph(const FontDef& font, uint8_t ch) {')
    for f in fonts:
        if f['type'] != 'ascii':
            continue
        out.append('#if %s' % use_macro(f['name']))
        out.append('    if (&font == &%s) return %s(ch);' % (f['name'], lookup_fn(f['name'])))
        out.append('#endif')
    out.append('    return nullptr;')
    out.append('}')
    out.append('')
    out.append('const uint16_t* font_get_glyph_unicode(const FontDef& font, uint16_t uni) {')
    for f in fonts:
        if f['type'] != 'unicode':
            continue
        out.append('#if %s' % use_macro(f['name']))
        out.append('    if (&font == &%s) return %s(uni);' % (f['name'], lookup_fn(f['name'])))
        out.append('#endif')
    out.append('    (void)font; (void)uni;')
    out.append('    return nullptr;')
    out.append('}')
    return '\n'.join(out) + '\n', total


# ---------------------------------------------------------------------------
def main():
    if not os.path.exists(MANIFEST):
        sys.exit('找不到 %s' % MANIFEST)
    with open(MANIFEST, encoding='utf-8') as fp:
        man = json.load(fp)

    fonts = man.get('fonts', [])
    if not fonts:
        sys.exit('manifest 里没有字体')

    print('库的字体名册 (%d 个):' % len(fonts))
    for f in fonts:
        print('  %-18s %2dx%-3d %-8s %3d 个字形   %s'
              % (f['name'], f['width'], f['height'], f['type'], len(f['glyphs']),
                 chars_macro(f['name'])))

    with open(OUT_H, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write(gen_header(fonts))
    src, total = gen_source(fonts)
    with open(OUT_CPP, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write(src)
    print()
    print('  [OK] %s' % OUT_H)
    print('  [OK] %s   (%d 个字形数组)' % (OUT_CPP, total))
    print()
    print('  提示: 每个工程只需提供 font_config.hpp (USE_* + *_CHARS(X))。')
    print('        未被清单引用的字形会被编译器丢弃, 不占 Flash。')


if __name__ == '__main__':
    main()
