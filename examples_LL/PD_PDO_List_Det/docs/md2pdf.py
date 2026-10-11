#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
md2pdf.py —— 把本目录下的 Markdown 转成排版好的 PDF（中文 + Mermaid 图都支持）。

依赖
----
Python 包（已装在 WorkBuddy 的 venv 里）:
    markdown, pygments, pypdf, reportlab
外部程序:
    Microsoft Edge —— 用 headless 模式打印 PDF。
    为什么用 Edge 而不是 reportlab 直接排？因为文档里有 Mermaid 图，
    必须有个真浏览器跑 JS 才能把它渲染出来。

用法
----
    <venv>/python.exe md2pdf.py
    <venv>/python.exe md2pdf.py 输入.md 输出.pdf

不带参数时，默认处理同目录下的 FUSB302_业务逻辑详解.md。

流程
----
    Markdown
      -> 把 ```mermaid 代码块先"寄存"起来，换成占位符
      -> markdown 库转 HTML（表格 / 围栏代码 / 属性列表）
      -> 把占位符还原成 <div class="mermaid">，交给 CDN 上的 mermaid.js
      -> 套上打印用 CSS
      -> Edge headless --print-to-pdf
      -> pypdf + reportlab 盖上页码
      -> 输出 PDF
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import markdown

# ---------------------------------------------------------------- 可调参数

HERE = Path(__file__).resolve().parent
DEFAULT_MD = HERE / "FUSB302_业务逻辑详解.md"

# Edge 的位置（Windows）。找不到就报错，不做静默降级。
EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]

# 等 mermaid.js 从 CDN 下载并渲染完的时间预算（毫秒）。
# 6 张图实测几百毫秒就够，给 25 秒是留余量。
VIRTUAL_TIME_BUDGET_MS = 25000

# 页面（A4），单位 mm
PAGE_MARGIN_TOP = 15
PAGE_MARGIN_SIDE = 14
PAGE_MARGIN_BOTTOM = 16

# ---------------------------------------------------------------- 打印用 CSS

CSS = """
@page {
  size: A4;
  margin: %(mt)dmm %(ms)dmm %(mb)dmm %(ms)dmm;
}

* { box-sizing: border-box; }

html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }

body {
  font-family: "Microsoft YaHei", "Segoe UI", "PingFang SC", "Noto Sans CJK SC", sans-serif;
  font-size: 10.5pt;
  line-height: 1.78;
  color: #1c1c1c;
  margin: 0;
  word-wrap: break-word;
}

h1 {
  font-size: 21pt;
  font-weight: 600;
  color: #0d2b45;
  margin: 0 0 6px 0;
  padding-bottom: 8px;
  border-bottom: 2.5px solid #0d2b45;
  line-height: 1.35;
}

h2 {
  font-size: 15pt;
  font-weight: 600;
  color: #0d2b45;
  margin: 26px 0 10px 0;
  padding: 5px 0 5px 10px;
  border-left: 4px solid #2f6fb0;
  background: #f2f6fb;
  page-break-after: avoid;
  line-height: 1.4;
}

h3 {
  font-size: 12.5pt;
  font-weight: 600;
  color: #1f4e79;
  margin: 20px 0 8px 0;
  page-break-after: avoid;
  line-height: 1.4;
}

h4 {
  font-size: 11pt;
  font-weight: 600;
  color: #2f6fb0;
  margin: 15px 0 6px 0;
  page-break-after: avoid;
}

p { margin: 8px 0; }

ul, ol { margin: 8px 0; padding-left: 24px; }
li { margin: 3px 0; }

a { color: #1a5fa8; text-decoration: none; }

strong { font-weight: 600; color: #0d2b45; }

hr {
  border: none;
  border-top: 1px solid #d8dee6;
  margin: 20px 0;
}

blockquote {
  margin: 10px 0;
  padding: 7px 13px;
  border-left: 3px solid #b9c6d6;
  background: #f6f8fb;
  color: #3c4858;
}
blockquote p { margin: 4px 0; }

table {
  width: 100%%;
  border-collapse: collapse;
  margin: 10px 0;
  font-size: 9.3pt;
  page-break-inside: auto;
}
thead { display: table-header-group; }
tr { page-break-inside: avoid; }
th {
  background: #e8eef6;
  border: 0.6px solid #b9c6d6;
  padding: 5px 7px;
  text-align: left;
  font-weight: 600;
  color: #0d2b45;
}
td {
  border: 0.6px solid #ccd6e2;
  padding: 5px 7px;
  vertical-align: top;
}
tbody tr:nth-child(even) { background: #fafbfd; }

code {
  font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
  font-size: 9.2pt;
  background: #eef1f5;
  border: 0.5px solid #dde3ea;
  border-radius: 3px;
  padding: 0.5px 3.5px;
  color: #a33a2c;
}

pre {
  background: #f7f9fb;
  border: 0.7px solid #dde3ea;
  border-left: 3px solid #2f6fb0;
  border-radius: 4px;
  padding: 9px 11px;
  margin: 10px 0;
  overflow-wrap: anywhere;
  white-space: pre-wrap;
  page-break-inside: avoid;
}
pre code {
  background: none;
  border: none;
  padding: 0;
  font-size: 8.8pt;
  line-height: 1.55;
  color: #24292f;
}

div.mermaid {
  margin: 14px 0;
  text-align: center;
  page-break-inside: avoid;
}
div.mermaid svg { max-width: 100%%; height: auto; }

.sr-only {
  position: absolute; width: 1px; height: 1px;
  padding: 0; margin: -1px; overflow: hidden;
  clip: rect(0,0,0,0); border: 0;
}
""" % {
    "mt": PAGE_MARGIN_TOP,
    "ms": PAGE_MARGIN_SIDE,
    "mb": PAGE_MARGIN_BOTTOM,
}

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>__TITLE__</title>
<style>__CSS__</style>
</head>
<body>
__BODY__
<script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>
<script>
  mermaid.initialize({
    startOnLoad: true,
    theme: 'neutral',
    securityLevel: 'loose',
    flowchart: { useMaxWidth: true, htmlLabels: true, curve: 'basis' },
    sequence: { useMaxWidth: true, wrap: true },
    themeVariables: {
      fontFamily: 'Microsoft YaHei, Segoe UI, sans-serif',
      fontSize: '14px'
    }
  });
</script>
</body>
</html>
"""


# ---------------------------------------------------------------- 工具函数

def find_edge() -> str:
    for p in EDGE_CANDIDATES:
        if os.path.isfile(p):
            return p
    raise SystemExit(
        "找不到 Microsoft Edge。\n"
        "这个脚本靠 Edge 的 headless 模式打印 PDF（因为要跑 JS 渲染 Mermaid）。\n"
        "请确认 Edge 已安装，或把路径加到 EDGE_CANDIDATES 里。"
    )


def md_to_html(md_text: str) -> str:
    """Markdown -> HTML，顺便把 ```mermaid 块单独保住。"""
    blocks: list[str] = []

    def stash(m: "re.Match[str]") -> str:
        blocks.append(m.group(1).strip())
        return "\n\n@@MERMAID%d@@\n\n" % (len(blocks) - 1)

    # 先把 mermaid 块抠出来，否则 markdown 库会把它当普通代码块
    stripped = re.sub(r"```mermaid[ \t]*\n(.*?)```", stash, md_text, flags=re.S)

    body = markdown.markdown(
        stripped,
        extensions=["tables", "fenced_code", "attr_list", "sane_lists", "md_in_html"],
        output_format="html5",
    )

    # 还原 mermaid：占位符可能被 <p> 包住，要拆掉，否则 <div> 嵌在 <p> 里不合法
    def unstash(m: "re.Match[str]") -> str:
        idx = int(m.group(1) if m.group(1) is not None else m.group(2))
        return '<div class="mermaid">\n%s\n</div>' % blocks[idx]

    body, n = re.subn(
        r"<p>\s*@@MERMAID(\d+)@@\s*</p>|@@MERMAID(\d+)@@",
        unstash,
        body,
    )
    if n != len(blocks):
        print("  警告：还原 mermaid 块数量不符（%d/%d）" % (n, len(blocks)))
    return body


def html_to_pdf(edge: str, html_path: Path, pdf_path: Path) -> None:
    profile = Path(tempfile.gettempdir()) / "_wb_edge_pdf_profile"
    profile.mkdir(parents=True, exist_ok=True)

    cmd = [
        edge,
        "--headless=new",
        "--disable-gpu",
        "--no-sandbox",
        "--no-first-run",
        "--disable-extensions",
        "--user-data-dir=%s" % profile,
        "--no-pdf-header-footer",
        "--virtual-time-budget=%d" % VIRTUAL_TIME_BUDGET_MS,
        "--print-to-pdf=%s" % pdf_path,
        html_path.as_uri(),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if not pdf_path.exists():
        print(proc.stdout[-3000:])
        print(proc.stderr[-3000:])
        raise SystemExit("Edge 没有生成 PDF。")


def stamp_page_numbers(pdf_path: Path) -> int:
    """在每页右下角盖 '第 n / N 页'。返回页数。"""
    from pypdf import PdfReader, PdfWriter
    from reportlab.pdfgen import canvas
    from reportlab.lib.pagesizes import A4
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.cidfonts import UnicodeCIDFont

    reader = PdfReader(str(pdf_path))
    total = len(reader.pages)
    if total == 0:
        return 0

    # 中文页码要嵌一个 CJK 字体；失败就退回纯数字
    font_name = "Helvetica"
    label_fmt = "%d / %d"
    try:
        pdfmetrics.registerFont(UnicodeCIDFont("STSong-Light"))
        font_name = "STSong-Light"
        label_fmt = "第 %d 页 / 共 %d 页"
    except Exception:
        pass

    buf = tempfile.NamedTemporaryFile(suffix=".pdf", delete=False)
    buf.close()
    c = canvas.Canvas(buf.name, pagesize=A4)
    w, h = A4
    for i in range(1, total + 1):
        c.setFont(font_name, 8)
        c.setFillColorRGB(0.45, 0.48, 0.52)
        c.drawRightString(w - 14 * 2.8346, 8 * 2.8346, label_fmt % (i, total))
        c.showPage()
    c.save()

    overlay = PdfReader(buf.name)
    writer = PdfWriter()
    for i, page in enumerate(reader.pages):
        if i < len(overlay.pages):
            page.merge_page(overlay.pages[i])
        writer.add_page(page)
    with open(pdf_path, "wb") as f:
        writer.write(f)
    os.unlink(buf.name)
    return total


# ---------------------------------------------------------------- 主流程

def main() -> None:
    args = sys.argv[1:]
    src = Path(args[0]).resolve() if len(args) >= 1 else DEFAULT_MD
    dst = Path(args[1]).resolve() if len(args) >= 2 else src.with_suffix(".pdf")

    if not src.exists():
        raise SystemExit("找不到输入文件：%s" % src)

    print("输入 : %s" % src)
    md_text = src.read_text(encoding="utf-8")

    body = md_to_html(md_text)
    title = src.stem
    m = re.search(r"^#\s+(.+)$", md_text, flags=re.M)
    if m:
        title = m.group(1).strip()

    html = (HTML_TEMPLATE
            .replace("__TITLE__", title)
            .replace("__CSS__", CSS)
            .replace("__BODY__", body))

    html_path = src.with_suffix(".html")
    html_path.write_text(html, encoding="utf-8")
    print("中间 : %s" % html_path)

    edge = find_edge()
    dst.unlink(missing_ok=True)
    html_to_pdf(edge, html_path, dst)

    pages = stamp_page_numbers(dst)
    size_kb = dst.stat().st_size / 1024.0
    print("输出 : %s" % dst)
    print("       %d 页，%.0f KB" % (pages, size_kb))


if __name__ == "__main__":
    main()
