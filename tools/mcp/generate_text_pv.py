#!/usr/bin/env python3
"""
Friction 2.5D - Minimalist White Aesthetic Kinetic Typography (极简白风 文字 PV)
Designed with Swiss Graphic & Editorial Motion Typography principles:
- Pure white (#ffffff) primary typography with muted clean off-white (#9aa0a6) subtitles
- Minimalist 1px hairline dividers, editorial coordinate tags [ 01 ], and corner crosshairs
- Deep obsidian dark background (#08090b) for maximum typographic contrast
- Smooth, elegant 2.5D perspective tilts and subtle breathing holds
- Full dynamic Canvas (W x H) and FPS adaptation
"""

import urllib.request
import json
import base64
import time

FRICTION_PORT = 9527
API_URL = f"http://127.0.0.1:{FRICTION_PORT}"

def call_eval(script: str, undo_name: str = "Minimalist White Text PV") -> dict:
    url = f"{API_URL}/api/eval"
    data = json.dumps({
        "script": script,
        "undoGroupName": undo_name
    }).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read().decode("utf-8"))

def call_tool(tool_name: str, args: dict = None) -> dict:
    url = f"{API_URL}/api/tool/{tool_name}"
    data = json.dumps(args or {}).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read().decode("utf-8"))

def capture_screenshot(filename: str) -> None:
    res = call_tool("friction_capture_viewport", {"format": "png", "quality": 95})
    if res.get("success") and "data" in res:
        img_bytes = base64.b64decode(res["data"])
        with open(filename, "wb") as f:
            f.write(img_bytes)
        print(f"  -> Viewport saved: {filename}")

def build_minimalist_white_pv():
    print("==================================================================")
    print("  Friction 2.5D Minimalist White Kinetic Typography PV Generator")
    print("==================================================================")

    pv_script = """
    var s = app.activeScene;
    if (!s) throw new Error('No active scene');

    // 1. Inspect dynamic scene dimensions and frame rate
    var W = s.width || 1920;
    var H = s.height || 1080;
    var FPS = s.fps || 60;
    var CX = W / 2.0;
    var CY = H / 2.0;
    var S = Math.min(W / 1920.0, H / 1080.0);
    
    // Set 10.0 seconds total animation
    s.duration = 10.0;
    s.currentFrame = 0;

    // Helper: Convert seconds to exact scene frame
    function f(t) {
        return Math.round(t * FPS);
    }

    // Clear previous layers
    var oldLayers = s.layers();
    for (var i = oldLayers.length - 1; i >= 0; i--) {
        try { oldLayers[i].remove(); } catch(e) {}
    }

    // =================================================================
    // Global Elements: Deep Obsidian Backdrop & Clean Frame
    // =================================================================
    var bg = s.addRect("00_Background", 0, 0, W, H);
    bg.position().setValue([0, 0]);
    bg.setFillColor("#08090b");

    // Minimalist outer hairline framing margin
    var margin = Math.round(50 * S);
    var frameW = W - margin * 2;
    var frameH = H - margin * 2;
    var border = s.addRect("00_HairlineBorder", -frameW / 2.0, -frameH / 2.0, frameW, frameH);
    border.position().setValue([CX, CY]);
    border.setFillColor("transparent");
    border.setStrokeColor("#ffffff");
    border.setStrokeWidth(1);
    border.opacityProp().setValue(18);

    // Editorial Corner Index Tags
    var tagTL = s.addText("00_TagTL", "VOL. 2.5 // ARCHIVE");
    tagTL.setFontSize(Math.round(18 * S));
    tagTL.setTextAlignment("left");
    tagTL.setFillColor("#8b949e");
    tagTL.position().setValue([margin + 20 * S, margin + 35 * S]);
    tagTL.opacityProp().setValue(60);

    var tagTR = s.addText("00_TagTR", "1920x1080 @ 60FPS");
    tagTR.setFontSize(Math.round(18 * S));
    tagTR.setTextAlignment("right");
    tagTR.setFillColor("#8b949e");
    tagTR.position().setValue([W - margin - 20 * S, margin + 35 * S]);
    tagTR.opacityProp().setValue(60);

    // =================================================================
    // ACT 1: [0.0s -> 2.5s] MINIMALIST EDITORIAL INTRO
    // "PURITY OF FORM" // "极简之美 秩序重构"
    // =================================================================
    var a1_actNum = s.addText("A1_ActTag", "[ 01 / FORM ]");
    a1_actNum.setFontSize(Math.round(22 * S));
    a1_actNum.setTextAlignment("center");
    a1_actNum.setFillColor("#ffffff");
    a1_actNum.position().setValueAtFrame(f(0.0), [CX, CY - 120 * S]);
    a1_actNum.position().setValueAtFrame(f(2.2), [CX, CY - 130 * S]);
    a1_actNum.opacityProp().setValueAtFrame(f(0.0), 0);
    a1_actNum.opacityProp().setValueAtFrame(f(0.4), 80);
    a1_actNum.opacityProp().setValueAtFrame(f(2.1), 80);
    a1_actNum.opacityProp().setValueAtFrame(f(2.4), 0);

    var a1_title = s.addText("A1_Title", "PURITY OF FORM");
    a1_title.setFontSize(Math.round(92 * S));
    a1_title.setTextAlignment("center");
    a1_title.setFillColor("#ffffff");
    a1_title.set3DEnabled(true);
    a1_title.position().setValueAtFrame(f(0.0), [CX, CY - 15 * S]);
    a1_title.position().setValueAtFrame(f(0.6), [CX, CY - 25 * S]);
    a1_title.position().setValueAtFrame(f(2.1), [CX, CY - 35 * S]);
    a1_title.position().setValueAtFrame(f(2.4), [CX, CY - 90 * S]);
    a1_title.scale().setValueAtFrame(f(0.0), [106, 106]);
    a1_title.scale().setValueAtFrame(f(0.6), [100, 100]);
    a1_title.scale().setValueAtFrame(f(2.1), [102, 102]);
    a1_title.rotationX().setValueAtFrame(f(0.0), 20);
    a1_title.rotationX().setValueAtFrame(f(0.6), 0);
    a1_title.opacityProp().setValueAtFrame(f(0.0), 0);
    a1_title.opacityProp().setValueAtFrame(f(0.4), 100);
    a1_title.opacityProp().setValueAtFrame(f(2.1), 100);
    a1_title.opacityProp().setValueAtFrame(f(2.4), 0);

    var a1_lineW = Math.round(500 * S);
    var a1_line = s.addRect("A1_Line", -a1_lineW / 2.0, -1, a1_lineW, 2);
    a1_line.position().setValue([CX, CY + 45 * S]);
    a1_line.setFillColor("#ffffff");
    a1_line.scale().setValueAtFrame(f(0.2), [0, 100]);
    a1_line.scale().setValueAtFrame(f(0.8), [100, 100]);
    a1_line.scale().setValueAtFrame(f(2.1), [100, 100]);
    a1_line.scale().setValueAtFrame(f(2.4), [0, 100]);
    a1_line.opacityProp().setValueAtFrame(f(0.2), 0);
    a1_line.opacityProp().setValueAtFrame(f(0.6), 40);
    a1_line.opacityProp().setValueAtFrame(f(2.1), 40);
    a1_line.opacityProp().setValueAtFrame(f(2.4), 0);

    var a1_sub = s.addText("A1_Subtitle", "MINIMALIST MOTION GRAPHICS & VECTOR DYNAMICS");
    a1_sub.setFontSize(Math.round(24 * S));
    a1_sub.setTextAlignment("center");
    a1_sub.setFillColor("#c9d1d9");
    a1_sub.position().setValueAtFrame(f(0.3), [CX, CY + 85 * S]);
    a1_sub.position().setValueAtFrame(f(2.1), [CX, CY + 80 * S]);
    a1_sub.opacityProp().setValueAtFrame(f(0.0), 0);
    a1_sub.opacityProp().setValueAtFrame(f(0.3), 0);
    a1_sub.opacityProp().setValueAtFrame(f(0.7), 85);
    a1_sub.opacityProp().setValueAtFrame(f(2.1), 85);
    a1_sub.opacityProp().setValueAtFrame(f(2.4), 0);

    // =================================================================
    // ACT 2: [2.5s -> 5.0s] STRUCTURE & SIMPLICITY
    // "STRUCTURE" // "SIMPLICITY"
    // =================================================================
    var a2_actNum = s.addText("A2_ActTag", "[ 02 / STRUCTURE ]");
    a2_actNum.setFontSize(Math.round(22 * S));
    a2_actNum.setTextAlignment("center");
    a2_actNum.setFillColor("#ffffff");
    a2_actNum.position().setValue([CX, CY - 140 * S]);
    a2_actNum.opacityProp().setValueAtFrame(f(0.0), 0);
    a2_actNum.opacityProp().setValueAtFrame(f(2.5), 0);
    a2_actNum.opacityProp().setValueAtFrame(f(2.8), 80);
    a2_actNum.opacityProp().setValueAtFrame(f(4.6), 80);
    a2_actNum.opacityProp().setValueAtFrame(f(4.9), 0);

    var a2_t1 = s.addText("A2_Title1", "STRUCTURE");
    a2_t1.setFontSize(Math.round(86 * S));
    a2_t1.setTextAlignment("center");
    a2_t1.setFillColor("#ffffff");
    a2_t1.set3DEnabled(true);
    a2_t1.position().setValueAtFrame(f(2.5), [CX, CY - 65 * S]);
    a2_t1.position().setValueAtFrame(f(3.0), [CX, CY - 55 * S]);
    a2_t1.position().setValueAtFrame(f(4.6), [CX, CY - 55 * S]);
    a2_t1.position().setValueAtFrame(f(4.9), [CX - 200 * S, CY - 55 * S]);
    a2_t1.rotationY().setValueAtFrame(f(2.5), 25);
    a2_t1.rotationY().setValueAtFrame(f(3.0), 0);
    a2_t1.opacityProp().setValueAtFrame(f(0.0), 0);
    a2_t1.opacityProp().setValueAtFrame(f(2.5), 0);
    a2_t1.opacityProp().setValueAtFrame(f(2.8), 100);
    a2_t1.opacityProp().setValueAtFrame(f(4.6), 100);
    a2_t1.opacityProp().setValueAtFrame(f(4.9), 0);

    var a2_t2 = s.addText("A2_Title2", "SIMPLICITY");
    a2_t2.setFontSize(Math.round(86 * S));
    a2_t2.setTextAlignment("center");
    a2_t2.setFillColor("#e6edf3");
    a2_t2.set3DEnabled(true);
    a2_t2.position().setValueAtFrame(f(2.6), [CX, CY + 55 * S]);
    a2_t2.position().setValueAtFrame(f(3.1), [CX, CY + 45 * S]);
    a2_t2.position().setValueAtFrame(f(4.6), [CX, CY + 45 * S]);
    a2_t2.position().setValueAtFrame(f(4.9), [CX + 200 * S, CY + 45 * S]);
    a2_t2.rotationY().setValueAtFrame(f(2.6), -25);
    a2_t2.rotationY().setValueAtFrame(f(3.1), 0);
    a2_t2.opacityProp().setValueAtFrame(f(0.0), 0);
    a2_t2.opacityProp().setValueAtFrame(f(2.6), 0);
    a2_t2.opacityProp().setValueAtFrame(f(2.9), 100);
    a2_t2.opacityProp().setValueAtFrame(f(4.6), 100);
    a2_t2.opacityProp().setValueAtFrame(f(4.9), 0);

    var boxW = Math.round(760 * S);
    var boxH = Math.round(260 * S);
    var a2_box = s.addRect("A2_BoxFrame", -boxW / 2.0, -boxH / 2.0, boxW, boxH);
    a2_box.position().setValue([CX, CY]);
    a2_box.setFillColor("transparent");
    a2_box.setStrokeColor("#ffffff");
    a2_box.setStrokeWidth(1);
    a2_box.scale().setValueAtFrame(f(2.5), [95, 95]);
    a2_box.scale().setValueAtFrame(f(3.1), [100, 100]);
    a2_box.scale().setValueAtFrame(f(4.6), [102, 102]);
    a2_box.opacityProp().setValueAtFrame(f(0.0), 0);
    a2_box.opacityProp().setValueAtFrame(f(2.5), 0);
    a2_box.opacityProp().setValueAtFrame(f(2.9), 35);
    a2_box.opacityProp().setValueAtFrame(f(4.6), 35);
    a2_box.opacityProp().setValueAtFrame(f(4.9), 0);

    // =================================================================
    // ACT 3: [5.0s -> 7.5s] 2.5D ELEGANCE ("留 白 之 美" // "NEGATIVE SPACE")
    // =================================================================
    var a3_actNum = s.addText("A3_ActTag", "[ 03 / SPACE ]");
    a3_actNum.setFontSize(Math.round(22 * S));
    a3_actNum.setTextAlignment("center");
    a3_actNum.setFillColor("#ffffff");
    a3_actNum.position().setValue([CX, CY - 130 * S]);
    a3_actNum.opacityProp().setValueAtFrame(f(0.0), 0);
    a3_actNum.opacityProp().setValueAtFrame(f(5.0), 0);
    a3_actNum.opacityProp().setValueAtFrame(f(5.3), 80);
    a3_actNum.opacityProp().setValueAtFrame(f(7.1), 80);
    a3_actNum.opacityProp().setValueAtFrame(f(7.4), 0);

    var a3_jp = s.addText("A3_TitleZH", "留 白 之 美");
    a3_jp.setFontSize(Math.round(108 * S));
    a3_jp.setTextAlignment("center");
    a3_jp.setFillColor("#ffffff");
    a3_jp.set3DEnabled(true);
    a3_jp.position().setValueAtFrame(f(5.0), [CX, CY - 20 * S]);
    a3_jp.position().setValueAtFrame(f(5.6), [CX, CY - 20 * S]);
    a3_jp.position().setValueAtFrame(f(7.1), [CX, CY - 20 * S]);
    a3_jp.position().setValueAtFrame(f(7.4), [CX, CY - 80 * S]);
    a3_jp.scale().setValueAtFrame(f(5.0), [115, 115]);
    a3_jp.scale().setValueAtFrame(f(5.6), [100, 100]);
    a3_jp.scale().setValueAtFrame(f(7.1), [103, 103]);
    a3_jp.rotationX().setValueAtFrame(f(5.0), 35);
    a3_jp.rotationX().setValueAtFrame(f(5.6), 0);
    a3_jp.opacityProp().setValueAtFrame(f(0.0), 0);
    a3_jp.opacityProp().setValueAtFrame(f(5.0), 0);
    a3_jp.opacityProp().setValueAtFrame(f(5.4), 100);
    a3_jp.opacityProp().setValueAtFrame(f(7.1), 100);
    a3_jp.opacityProp().setValueAtFrame(f(7.4), 0);

    var a3_sub = s.addText("A3_SubEN", "THE HARMONY OF NEGATIVE SPACE");
    a3_sub.setFontSize(Math.round(26 * S));
    a3_sub.setTextAlignment("center");
    a3_sub.setFillColor("#8b949e");
    a3_sub.set3DEnabled(true);
    a3_sub.position().setValueAtFrame(f(5.2), [CX, CY + 75 * S]);
    a3_sub.position().setValueAtFrame(f(7.1), [CX, CY + 75 * S]);
    a3_sub.opacityProp().setValueAtFrame(f(0.0), 0);
    a3_sub.opacityProp().setValueAtFrame(f(5.2), 0);
    a3_sub.opacityProp().setValueAtFrame(f(5.6), 80);
    a3_sub.opacityProp().setValueAtFrame(f(7.1), 80);
    a3_sub.opacityProp().setValueAtFrame(f(7.4), 0);

    var ringR = Math.round(190 * S);
    var a3_ring = s.addEllipse("A3_Ring", 0, 0, ringR);
    a3_ring.position().setValue([CX, CY]);
    a3_ring.setFillColor("transparent");
    a3_ring.setStrokeColor("#ffffff");
    a3_ring.setStrokeWidth(1);
    a3_ring.set3DEnabled(true);
    a3_ring.scale().setValueAtFrame(f(5.0), [80, 80]);
    a3_ring.scale().setValueAtFrame(f(5.8), [100, 100]);
    a3_ring.scale().setValueAtFrame(f(7.1), [106, 106]);
    a3_ring.rotationX().setValueAtFrame(f(5.0), 45);
    a3_ring.rotationX().setValueAtFrame(f(7.4), 60);
    a3_ring.rotationY().setValueAtFrame(f(5.0), 0);
    a3_ring.rotationY().setValueAtFrame(f(7.4), 90);
    a3_ring.opacityProp().setValueAtFrame(f(0.0), 0);
    a3_ring.opacityProp().setValueAtFrame(f(5.0), 0);
    a3_ring.opacityProp().setValueAtFrame(f(5.5), 25);
    a3_ring.opacityProp().setValueAtFrame(f(7.1), 25);
    a3_ring.opacityProp().setValueAtFrame(f(7.4), 0);

    // =================================================================
    // ACT 4: [7.5s -> 10.0s] FINALE LOGO ("FRICTION 2.5D")
    // =================================================================
    var a4_logo = s.addText("A4_Logo", "FRICTION 2.5D");
    a4_logo.setFontSize(Math.round(104 * S));
    a4_logo.setTextAlignment("center");
    a4_logo.setFillColor("#ffffff");
    a4_logo.set3DEnabled(true);
    a4_logo.position().setValueAtFrame(f(7.5), [CX, CY - 30 * S]);
    a4_logo.position().setValueAtFrame(f(8.1), [CX, CY - 30 * S]);
    a4_logo.scale().setValueAtFrame(f(7.5), [112, 112]);
    a4_logo.scale().setValueAtFrame(f(8.1), [100, 100]);
    a4_logo.scale().setValueAtFrame(f(9.9), [103, 103]);
    a4_logo.opacityProp().setValueAtFrame(f(0.0), 0);
    a4_logo.opacityProp().setValueAtFrame(f(7.5), 0);
    a4_logo.opacityProp().setValueAtFrame(f(8.0), 100);
    a4_logo.opacityProp().setValueAtFrame(f(9.9), 100);

    var a4_desc = s.addText("A4_Desc", "VECTOR ANIMATION & MOTION DESIGN ENGINE");
    a4_desc.setFontSize(Math.round(24 * S));
    a4_desc.setTextAlignment("center");
    a4_desc.setFillColor("#8b949e");
    a4_desc.position().setValueAtFrame(f(7.8), [CX, CY + 55 * S]);
    a4_desc.scale().setValueAtFrame(f(7.8), [90, 90]);
    a4_desc.scale().setValueAtFrame(f(8.3), [100, 100]);
    a4_desc.opacityProp().setValueAtFrame(f(0.0), 0);
    a4_desc.opacityProp().setValueAtFrame(f(7.8), 0);
    a4_desc.opacityProp().setValueAtFrame(f(8.3), 85);
    a4_desc.opacityProp().setValueAtFrame(f(9.9), 85);

    var a4_lineW = Math.round(360 * S);
    var a4_line = s.addRect("A4_AccentLine", -a4_lineW / 2.0, -1, a4_lineW, 2);
    a4_line.position().setValue([CX, CY + 95 * S]);
    a4_line.setFillColor("#ffffff");
    a4_line.scale().setValueAtFrame(f(8.0), [0, 100]);
    a4_line.scale().setValueAtFrame(f(8.6), [100, 100]);
    a4_line.opacityProp().setValueAtFrame(f(0.0), 0);
    a4_line.opacityProp().setValueAtFrame(f(8.0), 0);
    a4_line.opacityProp().setValueAtFrame(f(8.5), 50);
    a4_line.opacityProp().setValueAtFrame(f(9.9), 50);

    s.currentFrame = 0;
    return {
        width: W,
        height: H,
        fps: FPS,
        duration: s.duration,
        totalLayers: s.numLayers
    };
    """

    print("1. Generating Minimalist White Aesthetic PV...")
    res = call_eval(pv_script, "Generate Minimalist White PV")
    print("   Scene Result:", res)

    print("2. Verifying Visual Quality & Capturing Screenshots...")
    preview_timestamps = [
        (1.2, "/tmp/pv_white_act1.png", "Act 1: PURITY OF FORM"),
        (3.6, "/tmp/pv_white_act2.png", "Act 2: STRUCTURE & SIMPLICITY"),
        (6.2, "/tmp/pv_white_act3.png", "Act 3: 留 白 之 美"),
        (8.9, "/tmp/pv_white_act4.png", "Act 4: FRICTION 2.5D FINALE")
    ]

    for t_sec, filepath, label in preview_timestamps:
        call_tool("friction_seek_timeline", {"time": t_sec})
        time.sleep(0.12)
        capture_screenshot(filepath)
        print(f"   ✓ Time {t_sec}s [{label}] captured")

    print("3. Starting Real-time Playback in Friction...")
    call_tool("friction_seek_timeline", {"time": 0.0})
    call_tool("friction_play_pause")
    print("==================================================================")
    print("  Minimalist White Typography PV generated & playing!")
    print("==================================================================")

if __name__ == "__main__":
    build_minimalist_white_pv()
