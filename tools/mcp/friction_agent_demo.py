#!/usr/bin/env python3
"""
Friction 2.5D AI Agent Live Demo
Demonstrates real-time control of Friction 2.5D:
- Query active scene
- Create animated shapes (rect, circle, text)
- Add keyframes on position, rotation, 2.5D 3D rotations, and scale
- Capture viewport screenshot
"""

import urllib.request
import json
import base64
import time

URL = "http://127.0.0.1:9527"

def call_tool(tool_name: str, args: dict = None) -> dict:
    req_body = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": tool_name,
            "arguments": args or {}
        }
    }
    req = urllib.request.Request(
        f"{URL}/mcp",
        data=json.dumps(req_body).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read().decode("utf-8"))

def eval_script(script: str) -> dict:
    req = urllib.request.Request(
        f"{URL}/api/eval",
        data=json.dumps({"script": script, "undoGroupName": "AI Demo Generator"}).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req) as r:
        return json.loads(r.read().decode("utf-8"))

def main():
    print("=== Friction 2.5D AI Agent Demo ===")
    
    # 1. Check Scene Status
    print("1. Fetching scene metadata...")
    scene_res = call_tool("friction_get_scene_info")
    print("Scene status:", json.dumps(scene_res, indent=2, ensure_ascii=False))

    # 2. Run AI Animation Script
    print("\n2. Creating multi-layer 2.5D animation with keyframes...")
    script = """
    // Create animated card
    var card = app.activeScene.addRect('AICard', 0, 0, 300, 200);
    card.set3DEnabled(true);
    card.position().setValueAtFrame(0, [200, 200]);
    card.position().setValueAtFrame(60, [700, 400]);
    card.rotation().setValueAtFrame(0, 0);
    card.rotation().setValueAtFrame(60, 180);
    card.rotationX().setValueAtFrame(0, 0);
    card.rotationX().setValueAtFrame(60, 45);
    card.rotationY().setValueAtFrame(0, 0);
    card.rotationY().setValueAtFrame(60, 60);

    // Create title text
    var title = app.activeScene.addText('AITitle', 'Created by AI Agent');
    title.position().setValueAtFrame(0, [200, 100]);
    title.position().setValueAtFrame(60, [700, 300]);

    // Seek timeline to mid-animation
    app.activeScene.currentFrame = 30;
    """
    eval_res = eval_script(script)
    print("Execution result:", eval_res)

    # 3. Capture Viewport Screenshot
    print("\n3. Capturing real-time visual feedback...")
    capture_res = call_tool("friction_capture_viewport", {"format": "png", "quality": 90})
    if "result" in capture_res and "content" in capture_res["result"]:
        content = capture_res["result"]["content"][0]
        if content.get("type") == "image":
            img_bytes = base64.b64decode(content["data"])
            with open("friction_ai_preview.png", "wb") as f:
                f.write(img_bytes)
            print("Preview frame successfully saved to friction_ai_preview.png!")
        else:
            print("Capture output:", content)

    print("\nLive demo complete!")

if __name__ == "__main__":
    main()
