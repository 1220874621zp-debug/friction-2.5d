#!/usr/bin/env python3
import os
import subprocess
import xml.etree.ElementTree as ET

CWD = "/home/lanrhyme/Projects/friction-2.5d"
TS_FILE = os.path.join(CWD, "src/app/translations/friction_zh_CN.ts")
QM_FILE = os.path.join(CWD, "src/app/translations/friction_zh_CN.qm")
HICOLOR_DIR = os.path.join(CWD, "src/app/icons/hicolor")
SCALABLE_DIR = os.path.join(HICOLOR_DIR, "scalable")

# 1. Refined effect icon (sleek, perfectly proportioned dual sparkle)
REFINED_EFFECT_SVG = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
  <path d="M14 3.5l1.6 3.4l3.4 1.6l-3.4 1.6l-1.6 3.4l-1.6 -3.4l-3.4 -1.6l3.4 -1.6z"/>
  <path d="M6 14.5l1 2l2 1l-2 1l-1 2l-1 -2l-2 -1l2 -1z"/>
</svg>'''

for cat in ["legacy", "actions", "friction"]:
    svg_path = os.path.join(SCALABLE_DIR, cat, "effect.svg")
    if os.path.exists(os.path.dirname(svg_path)):
        with open(svg_path, "w", encoding="utf-8") as f:
            f.write(REFINED_EFFECT_SVG.strip() + "\n")
        print(f"Updated {svg_path}")

# Re-render effect.png across all sizes
SIZES = [16, 17, 18, 20, 21, 22, 24, 25, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 64, 96, 128, 192, 256]
for cat in ["legacy", "actions", "friction"]:
    svg_path = os.path.join(SCALABLE_DIR, cat, "effect.svg")
    if not os.path.exists(svg_path):
        continue
    for w in SIZES:
        png_path = os.path.join(HICOLOR_DIR, f"{w}x{w}", cat, "effect.png")
        if os.path.exists(os.path.dirname(png_path)):
            subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(w), "-f", "png", "-o", png_path, svg_path], check=True)
print("Re-rendered effect.png across all sizes.")

# 2. Update translations in TS file
NEW_TRANSLATIONS = {
    "Set Layer In Point (Alt+[)": "设置图层入点 (Alt+[)",
    "Set Layer Out Point (Alt+])": "设置图层出点 (Alt+])",
    "Split Clip (Ctrl+Shift+D)": "分割片段 (Ctrl+Shift+D)",
    "Split Clip at Current Frame": "在当前帧分割片段",
    "Set In Point": "设置入点",
    "Set Out Point": "设置出点",
    "Edit duration": "编辑持续时间",
    "Split Clip": "分割片段",
    "Scene frame start": "场景起始帧",
    "Scene frame end": "场景结束帧",
    "Current frame": "当前帧",
    "Rewind": "倒带",
    "Fast Forward": "快进",
    "Go to First Frame": "跳转到起始帧",
    "Go to Last Frame": "跳转到结束帧",
    "Play Preview From Start": "从头开始预览",
    "Play Preview": "播放预览",
    "Stop Preview": "停止预览",
    "Loop Preview": "循环预览",
    "Cache %p%": "缓存进度 %p%",
    "Previous Keyframe": "上一关键帧",
    "Next Keyframe": "下一关键帧",
    "Set In": "设置入点",
    "Set Out": "设置出点"
}

tree = ET.parse(TS_FILE)
root = tree.getroot()

# Search and update or insert translation messages across contexts
for context in root.findall("context"):
    name_elem = context.find("name")
    cname = name_elem.text if name_elem is not None else ""
    for msg in context.findall("message"):
        source = msg.find("source")
        if source is not None and source.text in NEW_TRANSLATIONS:
            trans = msg.find("translation")
            if trans is None:
                trans = ET.SubElement(msg, "translation")
            trans.text = NEW_TRANSLATIONS[source.text]
            if "type" in trans.attrib:
                del trans.attrib["type"]

# Also ensure TimelineDockWidget, KeysView, Canvas have these messages
for ctx_name in ["TimelineDockWidget", "KeysView", "Canvas", "CanvasWindow"]:
    # find or create context
    ctx = None
    for c in root.findall("context"):
        ne = c.find("name")
        if ne is not None and ne.text == ctx_name:
            ctx = c
            break
    if ctx is None:
        ctx = ET.SubElement(root, "context")
        ne = ET.SubElement(ctx, "name")
        ne.text = ctx_name

    existing_sources = set()
    for msg in ctx.findall("message"):
        src = msg.find("source")
        if src is not None and src.text:
            existing_sources.add(src.text)

    for src_text, tr_text in NEW_TRANSLATIONS.items():
        if src_text not in existing_sources:
            msg = ET.SubElement(ctx, "message")
            src_elem = ET.SubElement(msg, "source")
            src_elem.text = src_text
            tr_elem = ET.SubElement(msg, "translation")
            tr_elem.text = tr_text

tree.write(TS_FILE, encoding="utf-8", xml_declaration=True)
print(f"Updated {TS_FILE}")

# Compile .qm
subprocess.run(["lrelease", TS_FILE, "-qm", QM_FILE], check=True)
print(f"Compiled {QM_FILE}")
