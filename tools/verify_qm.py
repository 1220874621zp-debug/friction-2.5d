# -*- coding: utf-8 -*-
# 验证当前 .qm 中 PSD 同步词条在三个上下文的实际返回
import sys
from PySide6.QtCore import QTranslator, QCoreApplication

app = QCoreApplication(sys.argv)
import os
qm_path = os.path.join(os.path.dirname(__file__), "../src/app/translations/friction_zh_CN.qm")
t = QTranslator()
if not t.load(qm_path):
    print("FAILED to load qm"); sys.exit(1)

probes = [
    ("PsdImageBox",  "Update Layer from Source PSD"),
    ("PsdImageBox",  "Sync All Layers from Source PSD"),
    ("ImageBox",     "Update Layer from Source PSD"),
    ("ImageBox",     "Sync All Layers from Source PSD"),
    ("BoundingBox",  "Update Layer from Source PSD"),
    ("BoundingBox",  "Sync All Layers from Source PSD"),
    ("KraImageBox",  "Update Layer from Source KRA"),
    ("KraImageBox",  "Sync All Layers from Source KRA"),
    ("BoundingBox",  "Update Layer from Source KRA"),
    ("ImageBox",     "Update Layer from Source KRA"),
    ("QObject",      "Blur"),
    ("QObject",      "Wave Warp"),
    ("QObject",      "Chromatic Aberration"),
]
for ctx, src in probes:
    r = t.translate(ctx, src)
    print("%-14s | %-36s -> %s" % (ctx, src, r if r else "(EMPTY=English fallback)"))
