#!/usr/bin/env python3
"""
Generate a stylish Kinetic Typography PV in Friction 2.5D via MCP
"""

import json
import urllib.request
import sys

MCP_URL = "http://127.0.0.1:9527/mcp"

def eval_friction_js(script: str, group_name: str = "Generate Kinetic Typography PV") -> dict:
    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "friction_eval_script",
            "arguments": {
                "script": script,
                "undoGroupName": group_name
            }
        }
    }
    data = json.dumps(req).encode("utf-8")
    req_obj = urllib.request.Request(
        MCP_URL,
        data=data,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req_obj, timeout=15) as resp:
        return json.loads(resp.read().decode("utf-8"))

def seek_and_play(frame: int = 0) -> None:
    # Seek to frame 0
    req_seek = {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "friction_seek_timeline",
            "arguments": {"frame": frame}
        }
    }
    urllib.request.urlopen(
        urllib.request.Request(MCP_URL, data=json.dumps(req_seek).encode("utf-8"), headers={"Content-Type": "application/json"}),
        timeout=5
    )

def main():
    js_script = r"""
(function() {
    var scene = app.activeScene;
    if (!scene) {
        throw new Error("No active scene found in Friction");
    }

    // 1. Scene Specifications: 1920x1080 @ 60fps, 7.0 seconds (420 frames)
    scene.width = 1920;
    scene.height = 1080;
    scene.fps = 60;
    scene.duration = 7.0;

    // 2. Clear existing scene layers
    var oldLayers = scene.layers();
    for (var i = oldLayers.length - 1; i >= 0; i--) {
        oldLayers[i].remove();
    }

    // Helper functions for animation
    function addTextLayer(name, text, fontSize, color, align) {
        var lay = scene.addText(name, text);
        lay.setFontSize(fontSize);
        lay.setFillColor(color);
        lay.setTextAlignment(align || "center");
        return lay;
    }

    function addRectLayer(name, x, y, w, h, color) {
        var lay = scene.addRect(name, x, y, w, h);
        lay.setFillColor(color);
        return lay;
    }

    function addCircleLayer(name, cx, cy, radius, color) {
        var lay = scene.addEllipse(name, cx, cy, radius);
        lay.setFillColor(color);
        return lay;
    }

    // =========================================================================
    // SECTION 0: GLOBAL HUD & BACKGROUND
    // =========================================================================
    var bg = addRectLayer("00_bg_main", 0, 0, 1920, 1080, "#0e1017");
    
    // Top & Bottom Tech HUD Lines
    var hudTop = addRectLayer("00_hud_top", 60, 45, 1800, 2, "#272d3b");
    var hudBottom = addRectLayer("00_hud_bottom", 60, 1035, 1800, 2, "#272d3b");

    var hudTL = addTextLayer("00_hud_tl", "// SYSTEM: FRICTION 2.5D", 16, "#718093", "left");
    hudTL.position().setValue([100, 75]);

    var hudTR = addTextLayer("00_hud_tr", "SKIA REALTIME ENGINE // 60 FPS", 16, "#718093", "left");
    hudTR.position().setValue([1500, 75]);

    var hudBL = addTextLayer("00_hud_bl", "SEQ: 01 // KINETIC_PV", 16, "#00f2fe", "left");
    hudBL.position().setValue([100, 1005]);

    var hudBR = addTextLayer("00_hud_br", "2026 NEXT-GEN MOTION DESIGN", 16, "#718093", "left");
    hudBR.position().setValue([1550, 1005]);

    // =========================================================================
    // SECTION 1: [0f - 85f] INTRO GLITCH & TITLE IMPACT
    // =========================================================================
    var c1Glow = addCircleLayer("01_intro_glow", 960, 540, 260, "#161a29");
    c1Glow.opacityProp().setValueAtFrame(0, 0);
    c1Glow.opacityProp().setValueAtFrame(20, 45);
    c1Glow.opacityProp().setValueAtFrame(70, 45);
    c1Glow.opacityProp().setValueAtFrame(85, 0);

    var c1Tag = addTextLayer("01_intro_tag", "MOTION DESIGN EVOLUTION", 24, "#00f2fe", "center");
    c1Tag.position().setValueAtFrame(0, [960, 440]);
    c1Tag.position().setValueAtFrame(25, [960, 420]);
    c1Tag.opacityProp().setValueAtFrame(0, 0);
    c1Tag.opacityProp().setValueAtFrame(12, 100);
    c1Tag.opacityProp().setValueAtFrame(72, 100);
    c1Tag.opacityProp().setValueAtFrame(85, 0);

    var c1Title = addTextLayer("01_intro_title", "FRICTION", 120, "#f5f6fa", "center");
    c1Title.set3DEnabled(true);
    c1Title.position().setValue([960, 520]);
    
    // Scale spring overshoot
    c1Title.scale().setValueAtFrame(4, [0.15, 0.15]);
    c1Title.scale().setValueAtFrame(24, [1.18, 1.18]);
    c1Title.scale().setValueAtFrame(38, [0.96, 0.96]);
    c1Title.scale().setValueAtFrame(50, [1.0, 1.0]);
    c1Title.scale().setValueAtFrame(85, [1.35, 1.35]);

    // 3D rotation tilt
    c1Title.rotationX().setValueAtFrame(4, 45);
    c1Title.rotationX().setValueAtFrame(35, 0);
    c1Title.opacityProp().setValueAtFrame(4, 0);
    c1Title.opacityProp().setValueAtFrame(14, 100);
    c1Title.opacityProp().setValueAtFrame(72, 100);
    c1Title.opacityProp().setValueAtFrame(85, 0);

    var c1Bar = addRectLayer("01_intro_bar", 660, 595, 600, 4, "#ff3366");
    c1Bar.scale().setValueAtFrame(15, [0.01, 1]);
    c1Bar.scale().setValueAtFrame(40, [1.0, 1]);
    c1Bar.opacityProp().setValueAtFrame(15, 0);
    c1Bar.opacityProp().setValueAtFrame(20, 100);
    c1Bar.opacityProp().setValueAtFrame(72, 100);
    c1Bar.opacityProp().setValueAtFrame(85, 0);

    var c1Sub = addTextLayer("01_intro_sub", "VECTOR GRAPHICS • 2.5D SPATIAL • GLSL SHADERS", 20, "#718093", "center");
    c1Sub.position().setValue([960, 635]);
    c1Sub.opacityProp().setValueAtFrame(25, 0);
    c1Sub.opacityProp().setValueAtFrame(45, 100);
    c1Sub.opacityProp().setValueAtFrame(72, 100);
    c1Sub.opacityProp().setValueAtFrame(85, 0);

    // =========================================================================
    // SECTION 2: [85f - 185f] KINETIC RHYTHM // FAST BEAT DROP
    // =========================================================================
    var c2Card = addRectLayer("02_beat_card", 310, 290, 1300, 500, "#141722");
    c2Card.set3DEnabled(true);
    c2Card.rotationY().setValueAtFrame(85, -35);
    c2Card.rotationY().setValueAtFrame(120, 0);
    c2Card.rotationY().setValueAtFrame(185, 25);
    c2Card.opacityProp().setValueAtFrame(85, 0);
    c2Card.opacityProp().setValueAtFrame(98, 100);
    c2Card.opacityProp().setValueAtFrame(175, 100);
    c2Card.opacityProp().setValueAtFrame(185, 0);

    var c2Word1 = addTextLayer("02_beat_word1", "ULTRA FAST", 64, "#ffd000", "center");
    c2Word1.position().setValueAtFrame(90, [540, 430]);
    c2Word1.position().setValueAtFrame(115, [680, 430]);
    c2Word1.opacityProp().setValueAtFrame(90, 0);
    c2Word1.opacityProp().setValueAtFrame(102, 100);
    c2Word1.opacityProp().setValueAtFrame(175, 100);
    c2Word1.opacityProp().setValueAtFrame(185, 0);

    var c2Word2 = addTextLayer("02_beat_word2", "2.5D KINETIC", 110, "#00f2fe", "center");
    c2Word2.set3DEnabled(true);
    c2Word2.position().setValue([960, 535]);
    c2Word2.scale().setValueAtFrame(105, [0.1, 0.1]);
    c2Word2.scale().setValueAtFrame(125, [1.2, 1.2]);
    c2Word2.scale().setValueAtFrame(140, [0.97, 0.97]);
    c2Word2.scale().setValueAtFrame(152, [1.0, 1.0]);
    c2Word2.rotation().setValueAtFrame(105, -6);
    c2Word2.rotation().setValueAtFrame(135, 0);
    c2Word2.opacityProp().setValueAtFrame(105, 0);
    c2Word2.opacityProp().setValueAtFrame(118, 100);
    c2Word2.opacityProp().setValueAtFrame(175, 100);
    c2Word2.opacityProp().setValueAtFrame(185, 0);

    var c2Word3 = addTextLayer("02_beat_word3", "ZERO LATENCY WORKFLOW", 34, "#ff3366", "center");
    c2Word3.position().setValueAtFrame(120, [1220, 640]);
    c2Word3.position().setValueAtFrame(145, [1050, 640]);
    c2Word3.opacityProp().setValueAtFrame(120, 0);
    c2Word3.opacityProp().setValueAtFrame(132, 100);
    c2Word3.opacityProp().setValueAtFrame(175, 100);
    c2Word3.opacityProp().setValueAtFrame(185, 0);

    // =========================================================================
    // SECTION 3: [185f - 290f] SPATIAL 3D SHOWCASE // DEPTH ILLUSION
    // =========================================================================
    var c3BgText = addTextLayer("03_spat_bg_text", "DIMENSION", 150, "#191d29", "center");
    c3BgText.set3DEnabled(true);
    c3BgText.position().setValue([960, 460]);
    c3BgText.zPosition().setValue(-350);
    c3BgText.opacityProp().setValueAtFrame(185, 0);
    c3BgText.opacityProp().setValueAtFrame(202, 70);
    c3BgText.opacityProp().setValueAtFrame(278, 70);
    c3BgText.opacityProp().setValueAtFrame(290, 0);

    var c3CardL = addRectLayer("03_spat_card_l", 420, 410, 360, 260, "#1e2333");
    c3CardL.set3DEnabled(true);
    c3CardL.rotationY().setValue(32);
    c3CardL.zPosition().setValue(120);
    c3CardL.opacityProp().setValueAtFrame(195, 0);
    c3CardL.opacityProp().setValueAtFrame(210, 90);
    c3CardL.opacityProp().setValueAtFrame(278, 90);
    c3CardL.opacityProp().setValueAtFrame(290, 0);

    var c3CardText = addTextLayer("03_spat_card_text", "GPU SHADER\nGLASS • GLOW\nPIXEL SORT", 24, "#00ff88", "center");
    c3CardText.set3DEnabled(true);
    c3CardText.position().setValue([600, 540]);
    c3CardText.rotationY().setValue(32);
    c3CardText.zPosition().setValue(130);
    c3CardText.opacityProp().setValueAtFrame(195, 0);
    c3CardText.opacityProp().setValueAtFrame(210, 100);
    c3CardText.opacityProp().setValueAtFrame(278, 100);
    c3CardText.opacityProp().setValueAtFrame(290, 0);

    var c3Hero = addTextLayer("03_spat_hero", "BREAK THE LIMIT", 84, "#f5f6fa", "center");
    c3Hero.set3DEnabled(true);
    c3Hero.position().setValue([1120, 520]);
    c3Hero.rotationY().setValueAtFrame(190, -40);
    c3Hero.rotationY().setValueAtFrame(235, 5);
    c3Hero.rotationY().setValueAtFrame(290, 22);
    c3Hero.rotationX().setValueAtFrame(190, 20);
    c3Hero.rotationX().setValueAtFrame(245, -5);
    c3Hero.zPosition().setValueAtFrame(190, 200);
    c3Hero.zPosition().setValueAtFrame(235, 40);
    c3Hero.opacityProp().setValueAtFrame(190, 0);
    c3Hero.opacityProp().setValueAtFrame(205, 100);
    c3Hero.opacityProp().setValueAtFrame(278, 100);
    c3Hero.opacityProp().setValueAtFrame(290, 0);

    var c3Sub = addTextLayer("03_spat_sub", "// UNLIMITED CREATIVE FREEDOM", 24, "#00f2fe", "center");
    c3Sub.position().setValue([1120, 610]);
    c3Sub.opacityProp().setValueAtFrame(205, 0);
    c3Sub.opacityProp().setValueAtFrame(225, 100);
    c3Sub.opacityProp().setValueAtFrame(278, 100);
    c3Sub.opacityProp().setValueAtFrame(290, 0);

    // =========================================================================
    // SECTION 4: [290f - 420f] GRAND FINALE // BRAND OUTRO
    // =========================================================================
    var c4Ring = addCircleLayer("04_final_ring", 960, 540, 240, "#1a1f2e");
    c4Ring.set3DEnabled(true);
    c4Ring.scale().setValueAtFrame(290, [0.1, 0.1]);
    c4Ring.scale().setValueAtFrame(325, [1.1, 1.1]);
    c4Ring.scale().setValueAtFrame(345, [1.0, 1.0]);
    c4Ring.opacityProp().setValueAtFrame(290, 0);
    c4Ring.opacityProp().setValueAtFrame(315, 60);
    c4Ring.opacityProp().setValueAtFrame(395, 60);
    c4Ring.opacityProp().setValueAtFrame(415, 0);

    var c4Brand = addTextLayer("04_final_brand", "FRICTION 2.5D", 128, "#00f2fe", "center");
    c4Brand.set3DEnabled(true);
    c4Brand.position().setValue([960, 495]);
    c4Brand.scale().setValueAtFrame(300, [0.65, 0.65]);
    c4Brand.scale().setValueAtFrame(330, [1.08, 1.08]);
    c4Brand.scale().setValueAtFrame(350, [1.0, 1.0]);
    c4Brand.scale().setValueAtFrame(415, [1.06, 1.06]);
    c4Brand.rotationY().setValueAtFrame(300, -20);
    c4Brand.rotationY().setValueAtFrame(340, 0);
    c4Brand.opacityProp().setValueAtFrame(300, 0);
    c4Brand.opacityProp().setValueAtFrame(318, 100);
    c4Brand.opacityProp().setValueAtFrame(395, 100);
    c4Brand.opacityProp().setValueAtFrame(415, 0);

    var c4Line = addRectLayer("04_final_line", 610, 580, 700, 4, "#ff3366");
    c4Line.scale().setValueAtFrame(320, [0.01, 1]);
    c4Line.scale().setValueAtFrame(350, [1.0, 1]);
    c4Line.opacityProp().setValueAtFrame(320, 0);
    c4Line.opacityProp().setValueAtFrame(330, 100);
    c4Line.opacityProp().setValueAtFrame(395, 100);
    c4Line.opacityProp().setValueAtFrame(415, 0);

    var c4Tagline = addTextLayer("04_final_tagline", "NEXT-GEN MOTION GRAPHICS SUITE", 30, "#f5f6fa", "center");
    c4Tagline.position().setValue([960, 630]);
    c4Tagline.opacityProp().setValueAtFrame(325, 0);
    c4Tagline.opacityProp().setValueAtFrame(345, 100);
    c4Tagline.opacityProp().setValueAtFrame(395, 100);
    c4Tagline.opacityProp().setValueAtFrame(415, 0);

    var c4Footer = addTextLayer("04_final_footer", "POWERED BY C++17 • QT5 • SKIA 2D • GLSL SHADERS", 18, "#718093", "center");
    c4Footer.position().setValue([960, 690]);
    c4Footer.opacityProp().setValueAtFrame(340, 0);
    c4Footer.opacityProp().setValueAtFrame(360, 100);
    c4Footer.opacityProp().setValueAtFrame(395, 100);
    c4Footer.opacityProp().setValueAtFrame(415, 0);

    return {
        success: true,
        totalLayers: scene.numLayers,
        duration: scene.duration,
        fps: scene.fps
    };
})();
"""

    print("Sending Kinetic Typography PV script to Friction 2.5D via MCP...")
    res = eval_friction_js(js_script, "Generate Kinetic Typography PV")
    print("Friction Response:", json.dumps(res, indent=2))
    
    seek_and_play(0)
    print("Timeline reset to frame 0. Ready for preview!")

if __name__ == "__main__":
    main()
