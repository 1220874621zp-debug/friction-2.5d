#!/usr/bin/env python3
"""
Friction 2.5D - Semantic Declarative Motion Graphics & Kinetic Typography Engine
Provides a composable, orthogonal HTML-like DSL for AI agents & motion designers.

Supports:
- 2.5D Spatially-transformed cards, typography, geometry
- Per-character AE text animators (typewriter, rise, wave, breathe, spin, drop, zoom, etc.)
- Typography components: <text-strip>, <glow-cards>, <vertical-text>, <scatter-text>, <formula-scatter>
- Motion graphics geometry: <concentric>, <burst-lines>, <ruler>, <bg-blocks>, <hud>, <crosshair>, <barcode>
- 35+ Realtime GPU raster effects (glow, glitch, scanlines, chromatic_aberration, liquid_glass, blur, etc.)
- Layout containers: <col>, <row>, <card>, <seq>
"""

import sys
import os
import json
import re
import socket
import argparse
import urllib.request
import urllib.error
import xml.etree.ElementTree as ET
from typing import Dict, List, Any, Optional, Tuple

DEFAULT_HTTP_PORT = 9527
DEFAULT_SOCKET_PATH = r"\\.\pipe\friction_mcp" if sys.platform == "win32" else "/tmp/friction_mcp.sock"

# Default CJK fallback font
DEFAULT_FONT = "Noto Sans CJK JP" if sys.platform != "win32" else "Microsoft YaHei"

def sanitize_markup(xml_text: str) -> str:
    # 1. Allow 3d="true" (XML attributes cannot start with digit)
    xml_text = re.sub(r'\b3d\s*=', 'is3D=', xml_text, flags=re.IGNORECASE)
    # 2. Convert html self-closing or void tags
    xml_text = re.sub(r'<(hr|br|img)([^>]*)>', r'<\1\2/>', xml_text)
    # 3. Replace -> in attribute values with _to_
    def fix_arrow(m):
        return m.group(0).replace("->", "_to_")
    xml_text = re.sub(r'="[^"]*"', fix_arrow, xml_text)
    return xml_text

class FrictionMarkupCompiler:
    def __init__(self, xml_content: str):
        self.raw_xml = xml_content.strip()
        self.js_lines: List[str] = []
        self.layer_counter = 0

    def compile(self) -> str:
        clean_xml = sanitize_markup(self.raw_xml)
        if not re.match(r'^\s*<scene\b', clean_xml, re.IGNORECASE):
            clean_xml = f"<scene>{clean_xml}</scene>"
        
        try:
            root = ET.fromstring(clean_xml)
        except ET.ParseError as e:
            raise ValueError(f"Invalid Friction HTML/XML Markup: {e}")

        # Scene Settings
        width = int(self.get_attr(root, ["width", "w"], 1920))
        height = int(self.get_attr(root, ["height", "h"], 1080))
        fps = float(self.get_attr(root, ["fps"], 60.0))
        duration = float(self.get_attr(root, ["duration", "dur", "d"], 8.0))
        bg_color = self.get_attr(root, ["bg", "background", "color", "fill"], "#08090e")

        self.js_lines.append("// --- Friction 2.5D Motion Engine ---")
        self.js_lines.append("var s = app.activeScene;")
        self.js_lines.append("if (!s) throw new Error('No active scene');")
        self.js_lines.append(f"var W = {width}; var H = {height}; var FPS = {fps};")
        self.js_lines.append(f"s.width = W; s.height = H; s.fps = FPS; s.duration = {duration};")
        self.js_lines.append("var CX = W / 2.0; var CY = H / 2.0;")
        self.js_lines.append("var S = Math.min(W / 1920.0, H / 1080.0);")
        self.js_lines.append("function f(t) { return Math.round(t * FPS); }")
        
        # Clear existing layers
        self.js_lines.append("""
        var oldLayers = s.layers();
        for (var i = oldLayers.length - 1; i >= 0; i--) {
            try { oldLayers[i].remove(); } catch(e) {}
        }
        """)

        # Add background if not transparent
        if bg_color != "transparent":
            self.js_lines.append(f"var bg = s.addRect('00_Background', 0, 0, W, H);")
            self.js_lines.append(f"bg.position().setValue([0, 0]);")
            self.js_lines.append(f"bg.setFillColor('{bg_color}');")
            self.js_lines.append(f"bg.setStrokeWidth(0);")
            self.js_lines.append(f"bg.setStrokeColor('transparent');")

        # Process elements
        for elem in root:
            self.render_node(elem, 0.0, duration, "CX", "CY")

        self.js_lines.append("s.currentFrame = 0;")
        self.js_lines.append("return { success: true, numLayers: s.numLayers, duration: s.duration, fps: s.fps };")
        return "\n".join(self.js_lines)

    def next_var(self, prefix: str = "lay") -> str:
        self.layer_counter += 1
        return f"{prefix}_{self.layer_counter}"

    def get_attr(self, elem: ET.Element, names: List[str], default: Any = "") -> Any:
        for name in names:
            for k, v in elem.attrib.items():
                if k.lower() == name.lower():
                    return v
        return default

    def parse_range(self, val_str: str, default_start: float = 0.0, default_end: float = 0.0) -> Tuple[float, float]:
        """Supports '30->0', '30_to_0', '30 to 0', or single numbers '30'"""
        if not val_str:
            return default_start, default_end
        val_str = str(val_str).replace("->", "_to_")
        if "_to_" in val_str:
            p = [x.strip() for x in val_str.split("_to_") if x.strip()]
            if len(p) >= 2:
                try:
                    return float(p[0]), float(p[1])
                except ValueError:
                    pass
        try:
            v = float(str(val_str).strip())
            return v, v
        except ValueError:
            return default_start, default_end

    def render_node(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any, parent_3d: Optional[Dict[str, Any]] = None):
        tag = elem.tag.lower()
        if tag in ["seq", "sequence", "act", "scene-section"]:
            self.render_sequence(elem)
        elif tag in ["col", "column", "vstack", "valign"]:
            self.render_stack(elem, t_from, t_to, cx, cy, is_vertical=True, parent_3d=parent_3d)
        elif tag in ["row", "hstack", "halign"]:
            self.render_stack(elem, t_from, t_to, cx, cy, is_vertical=False, parent_3d=parent_3d)
        elif tag in ["card", "div", "box", "rect", "rectangle"]:
            self.render_card_or_rect(elem, t_from, t_to, cx, cy)
        elif tag in ["text", "h1", "h2", "h3", "p", "span", "stagger-text", "kinetic-text"]:
            self.render_text(elem, t_from, t_to, cx, cy, parent_3d=parent_3d)
        elif tag in ["text-strip", "ribbon"]:
            self.render_text_strip(elem, t_from, t_to, cx, cy)
        elif tag in ["glow-cards", "text-cards", "char-grid"]:
            self.render_glow_cards(elem, t_from, t_to, cx, cy)
        elif tag in ["vertical-text", "v-text"]:
            self.render_vertical_text(elem, t_from, t_to, cx, cy)
        elif tag in ["scatter-text", "scattered-text"]:
            self.render_scatter_text(elem, t_from, t_to)
        elif tag in ["formula-scatter", "formula-text"]:
            self.render_formula_scatter(elem, t_from, t_to)
        elif tag in ["concentric", "concentric-circles"]:
            self.render_concentric(elem, t_from, t_to, cx, cy)
        elif tag in ["burst-lines", "ray-burst"]:
            self.render_burst_lines(elem, t_from, t_to, cx, cy)
        elif tag in ["ruler", "scale-bar"]:
            self.render_ruler(elem, t_from, t_to)
        elif tag in ["bg-blocks", "background-blocks"]:
            self.render_bg_blocks(elem, t_from, t_to)
        elif tag in ["line", "divider", "hr"]:
            self.render_line(elem, t_from, t_to, cx, cy, parent_3d=parent_3d)
        elif tag in ["circle", "ring"]:
            self.render_circle(elem, t_from, t_to, cx, cy)
        elif tag in ["hud"]:
            self.render_hud(elem, t_from, t_to)
        elif tag in ["crosshair"]:
            self.render_crosshair(elem, t_from, t_to)
        elif tag in ["barcode"]:
            self.render_barcode(elem, t_from, t_to)

    def render_sequence(self, elem: ET.Element):
        t_from = float(self.get_attr(elem, ["from", "start", "t0"], 0.0))
        t_to = float(self.get_attr(elem, ["to", "end", "t1"], 3.0))
        for child in elem:
            self.render_node(child, t_from, t_to, "CX", "CY")

    def render_hud(self, elem: ET.Element, t_from: float, t_to: float):
        title = self.get_attr(elem, ["title"], "// FRICTION 2.5D MOTION ENGINE")
        sub = self.get_attr(elem, ["sub", "subtitle"], "REALTIME VECTOR GRAPHICS")
        timecode = str(self.get_attr(elem, ["timecode"], "true")).lower() == "true"
        brackets = str(self.get_attr(elem, ["brackets"], "true")).lower() == "true"
        color = self.get_attr(elem, ["color"], "#718096")
        accent = self.get_attr(elem, ["accent"], "#00f2fe")
        border = self.get_attr(elem, ["border", "borderColor"], "#1e2538")
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)

        v_top = self.next_var("hud_top")
        v_bot = self.next_var("hud_bot")
        v_tl = self.next_var("hud_tl")
        v_tr = self.next_var("hud_tr")

        self.js_lines.append(f"""
        // --- HUD Overlay ---
        var {v_top} = s.addRect('{v_top}', 60*S, 42*S, W - 120*S, 1.5*S);
        {v_top}.setFillColor('{border}');
        {v_top}.setStrokeWidth(0);
        var {v_bot} = s.addRect('{v_bot}', 60*S, H - 42*S, W - 120*S, 1.5*S);
        {v_bot}.setFillColor('{border}');
        {v_bot}.setStrokeWidth(0);

        var {v_tl} = s.addText('{v_tl}', '{title}');
        {v_tl}.setFontFamily('{font}');
        {v_tl}.setFontSize(Math.round(15 * S));
        {v_tl}.setFillColor('{accent}');
        {v_tl}.setTextAlignment('left');
        {v_tl}.position().setValue([80 * S, 68 * S]);
        {v_tl}.opacityProp().setValue(85);

        var {v_tr} = s.addText('{v_tr}', '{sub}');
        {v_tr}.setFontFamily('{font}');
        {v_tr}.setFontSize(Math.round(15 * S));
        {v_tr}.setFillColor('{color}');
        {v_tr}.setTextAlignment('right');
        {v_tr}.position().setValue([W - 80 * S, 68 * S]);
        {v_tr}.opacityProp().setValue(75);
        """)

        if timecode:
            v_bl = self.next_var("hud_bl")
            v_br = self.next_var("hud_br")
            self.js_lines.append(f"""
            var {v_bl} = s.addText('{v_bl}', 'REC [ 00:00:10:00 ] // 60 FPS');
            {v_bl}.setFontFamily('{font}');
            {v_bl}.setFontSize(Math.round(15 * S));
            {v_bl}.setFillColor('#ff2a6d');
            {v_bl}.setTextAlignment('left');
            {v_bl}.position().setValue([80 * S, H - 65 * S]);
            {v_bl}.opacityProp().setValue(90);

            var {v_br} = s.addText('{v_br}', '2.5D KINETIC TYPOGRAPHY SUITE');
            {v_br}.setFontFamily('{font}');
            {v_br}.setFontSize(Math.round(15 * S));
            {v_br}.setFillColor('{color}');
            {v_br}.setTextAlignment('right');
            {v_br}.position().setValue([W - 80 * S, H - 65 * S]);
            {v_br}.opacityProp().setValue(75);
            """)

        if brackets:
            v_b1 = self.next_var("hud_b1")
            v_b2 = self.next_var("hud_b2")
            self.js_lines.append(f"""
            var {v_b1} = s.addRect('{v_b1}', 50*S, 35*S, 20*S, 2);
            {v_b1}.setFillColor('{accent}');
            {v_b1}.setStrokeWidth(0);
            var {v_b2} = s.addRect('{v_b2}', W - 70*S, 35*S, 20*S, 2);
            {v_b2}.setFillColor('{accent}');
            {v_b2}.setStrokeWidth(0);
            """)

    def render_stack(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any, is_vertical: bool, parent_3d: Optional[Dict[str, Any]] = None):
        gap = float(self.get_attr(elem, ["gap", "spacing"], 20))
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")
        stack_cx = f"({custom_x}*S)" if custom_x else cx
        stack_cy = f"({custom_y}*S)" if custom_y else cy

        items = list(elem)
        total_items = len(items)
        if total_items == 0:
            return

        est_sizes = []
        for child in items:
            ctag = child.tag.lower()
            if ctag in ["text", "h1", "h2", "h3", "p", "span", "stagger-text", "kinetic-text"]:
                size = float(self.get_attr(child, ["size", "font-size", "fontsize"], 92 if ctag == "h1" else 28))
                est_sizes.append(size * 1.25)
            elif ctag in ["line", "divider", "hr"]:
                est_sizes.append(float(self.get_attr(child, ["h", "height"], 3)))
            elif ctag in ["card", "div", "box", "rect"]:
                h = float(self.get_attr(child, ["h", "height"], 200))
                est_sizes.append(h)
            else:
                est_sizes.append(30)

        total_span = sum(est_sizes) + (total_items - 1) * gap
        curr_offset = -total_span / 2.0

        for i, child in enumerate(items):
            item_span = est_sizes[i]
            offset_center = curr_offset + item_span / 2.0
            curr_offset += item_span + gap

            pos_y = f"({stack_cy} + ({offset_center} * S))" if is_vertical else stack_cy
            pos_x = stack_cx if is_vertical else f"({stack_cx} + ({offset_center} * S))"

            self.render_node(child, t_from, t_to, pos_x, pos_y, parent_3d=parent_3d)

    def render_card_or_rect(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        w = float(self.get_attr(elem, ["w", "width"], 1100))
        h = float(self.get_attr(elem, ["h", "height"], 450))
        bg = self.get_attr(elem, ["bg", "background", "fill"], "#101422")
        border = self.get_attr(elem, ["border", "stroke", "borderColor"], "")
        border_w = float(self.get_attr(elem, ["border-width", "strokeWidth", "stroke-width", "bw"], 1.5 if border else 0.0))
        is_3d = str(self.get_attr(elem, ["is3D", "3d", "spatial"], "false")).lower() == "true"
        rot_y = self.get_attr(elem, ["rotY", "rotateY", "rot-y"], "")
        rot_x = self.get_attr(elem, ["rotX", "rotateX", "rot-x"], "")
        z_pos = self.get_attr(elem, ["z", "zPosition", "z-pos"], "")
        motion_in = self.get_attr(elem, ["in", "enter"], "")
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")

        pos_cx = f"({custom_x}*S)" if custom_x else cx
        pos_cy = f"({custom_y}*S)" if custom_y else cy

        v_rect = self.next_var("card")
        self.js_lines.append(f"""
        var {v_rect}_w = Math.round({w} * S);
        var {v_rect}_h = Math.round({h} * S);
        var {v_rect} = s.addRect('{v_rect}', -{v_rect}_w/2, -{v_rect}_h/2, {v_rect}_w, {v_rect}_h);
        {v_rect}.position().setValue([{pos_cx}, {pos_cy}]);
        {v_rect}.setFillColor('{bg}');
        """)

        if border and border != "none" and border != "transparent":
            self.js_lines.append(f"""
            {v_rect}.setStrokeColor('{border}');
            {v_rect}.setStrokeWidth({border_w});
            """)
        else:
            self.js_lines.append(f"""
            {v_rect}.setStrokeWidth(0);
            {v_rect}.setStrokeColor('transparent');
            """)

        if is_3d or rot_y or rot_x or z_pos:
            self.js_lines.append(f"{v_rect}.set3DEnabled(true);")
            if rot_y:
                ry0, ry1 = self.parse_range(rot_y)
                self.js_lines.append(f"{v_rect}.rotationY().setValueAtFrame(f({t_from}), {ry0});")
                self.js_lines.append(f"{v_rect}.rotationY().setValueAtFrame(f({t_from + 0.55}), {ry1});")
            if rot_x:
                rx0, rx1 = self.parse_range(rot_x)
                self.js_lines.append(f"{v_rect}.rotationX().setValueAtFrame(f({t_from}), {rx0});")
                self.js_lines.append(f"{v_rect}.rotationX().setValueAtFrame(f({t_from + 0.55}), {rx1});")
            if z_pos:
                z0, z1 = self.parse_range(z_pos)
                self.js_lines.append(f"{v_rect}.zPosition().setValueAtFrame(f({t_from}), {z0});")
                self.js_lines.append(f"{v_rect}.zPosition().setValueAtFrame(f({t_from + 0.55}), {z1});")

        if motion_in:
            self.apply_motion_in(v_rect, motion_in, t_from, pos_cx, pos_cy)
        self.apply_common_modifiers(v_rect, elem)
        self.apply_opacity_envelope(v_rect, t_from, t_to, 0.3)

        if len(elem) > 0:
            parent_3d = None
            if is_3d or rot_y or rot_x or z_pos:
                parent_3d = {
                    "is3D": True,
                    "rotX": rot_x,
                    "rotY": rot_y,
                    "z": "15"
                }
            self.render_stack(elem, t_from, t_to, pos_cx, pos_cy, is_vertical=True, parent_3d=parent_3d)

    def render_text(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any, parent_3d: Optional[Dict[str, Any]] = None):
        text = (elem.text or "").strip()
        tag = elem.tag.lower()
        default_size = 96 if tag == "h1" else (48 if tag == "h2" else 26)
        size = float(self.get_attr(elem, ["size", "font-size", "fontSize"], default_size))
        color = self.get_attr(elem, ["color", "fill"], "#ffffff")
        font = self.get_attr(elem, ["font", "font-family", "fontFamily"], DEFAULT_FONT)
        align = self.get_attr(elem, ["align", "textAlign", "text-align"], "center")
        
        # 3D settings with parent inheritance
        is_3d_attr = self.get_attr(elem, ["is3D", "3d", "spatial"], "")
        if is_3d_attr != "":
            is_3d = str(is_3d_attr).lower() == "true"
        elif parent_3d and parent_3d.get("is3D"):
            is_3d = True
        else:
            is_3d = tag in ["h1", "stagger-text"]

        rot_y = self.get_attr(elem, ["rotY", "rotateY", "rot-y"], parent_3d.get("rotY", "") if parent_3d else "")
        rot_x = self.get_attr(elem, ["rotX", "rotateX", "rot-x"], parent_3d.get("rotX", "") if parent_3d else "")
        z_pos = self.get_attr(elem, ["z", "zPosition", "z-pos"], parent_3d.get("z", "") if parent_3d else "")
        motion_in = self.get_attr(elem, ["in", "enter"], "pop" if tag == "h1" else "fade")
        text_anim = self.get_attr(elem, ["anim", "text-anim", "textAnim", "preset"], "")
        dur_scale = float(self.get_attr(elem, ["anim-scale", "dur-scale"], 1.0))
        delay = float(self.get_attr(elem, ["delay"], 0.0))
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")

        pos_cx = f"({custom_x}*S)" if custom_x else cx
        pos_cy = f"({custom_y}*S)" if custom_y else cy

        act_from = t_from + delay
        v_text = self.next_var("txt")
        safe_text = text.replace("'", "\\'").replace("\n", "\\n")

        self.js_lines.append(f"""
        var {v_text} = s.addText('{v_text}', '{safe_text}');
        {v_text}.setFontFamily('{font}');
        {v_text}.setFontSize(Math.round({size} * S));
        {v_text}.setFillColor('{color}');
        {v_text}.setTextAlignment('{align}');
        {v_text}.position().setValue([{pos_cx}, {pos_cy}]);
        """)

        # Call C++ TextAnimPresets if specified
        if text_anim:
            self.js_lines.append(f"{v_text}.applyTextPreset('{text_anim}', f({act_from}), {dur_scale}, false);")

        if is_3d or rot_y or rot_x or z_pos:
            self.js_lines.append(f"{v_text}.set3DEnabled(true);")
            if rot_y:
                ry0, ry1 = self.parse_range(rot_y)
                self.js_lines.append(f"{v_text}.rotationY().setValueAtFrame(f({act_from}), {ry0});")
                self.js_lines.append(f"{v_text}.rotationY().setValueAtFrame(f({act_from + 0.45}), {ry1});")
            if rot_x:
                rx0, rx1 = self.parse_range(rot_x)
                self.js_lines.append(f"{v_text}.rotationX().setValueAtFrame(f({act_from}), {rx0});")
                self.js_lines.append(f"{v_text}.rotationX().setValueAtFrame(f({act_from + 0.45}), {rx1});")
            if z_pos:
                z0, z1 = self.parse_range(z_pos)
                self.js_lines.append(f"{v_text}.zPosition().setValueAtFrame(f({act_from}), {z0});")
                self.js_lines.append(f"{v_text}.zPosition().setValueAtFrame(f({act_from + 0.45}), {z1});")

        if not text_anim and motion_in:
            self.apply_motion_in(v_text, motion_in, act_from, pos_cx, pos_cy)

        self.apply_common_modifiers(v_text, elem)
        self.apply_opacity_envelope(v_text, act_from, t_to, 0.25)

    def render_text_strip(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        """Rotated text ribbon / banner (Yorushika style)"""
        text = self.get_attr(elem, ["text"], (elem.text or "").strip())
        rot = float(self.get_attr(elem, ["rot", "rotation"], -22.0))
        w = float(self.get_attr(elem, ["w", "width"], 70))
        h = float(self.get_attr(elem, ["h", "height"], 600))
        bg = self.get_attr(elem, ["bg", "strip-bg", "stripColor"], "#ffffff")
        color = self.get_attr(elem, ["color", "textColor"], "#08090e")
        size = float(self.get_attr(elem, ["size", "font-size"], 50))
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)

        v_strip = self.next_var("strip_box")
        v_txt = self.next_var("strip_txt")

        self.js_lines.append(f"""
        // --- Text Strip ---
        var {v_strip}_w = Math.round({w} * S);
        var {v_strip}_h = Math.round({h} * S);
        var {v_strip} = s.addRect('{v_strip}', -{v_strip}_w/2, -{v_strip}_h/2, {v_strip}_w, {v_strip}_h);
        {v_strip}.position().setValue([{cx}, {cy}]);
        {v_strip}.setFillColor('{bg}');
        {v_strip}.rotation().setValue({rot});

        var {v_txt} = s.addText('{v_txt}', '{text}');
        {v_txt}.setFontFamily('{font}');
        {v_txt}.setFontSize(Math.round({size} * S));
        {v_txt}.setFillColor('{color}');
        {v_txt}.setTextAlignment('center');
        {v_txt}.position().setValue([{cx}, {cy}]);
        {v_txt}.rotation().setValue({rot});
        """)
        self.apply_opacity_envelope(v_strip, t_from, t_to, 0.3)
        self.apply_opacity_envelope(v_txt, t_from, t_to, 0.3)

    def render_glow_cards(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        """Segmented character tiles / glowing cards (BluePlane / HystericNight style)"""
        text = self.get_attr(elem, ["text"], (elem.text or "").strip())
        card_bg = self.get_attr(elem, ["card-bg", "bg"], "#ffffff")
        color = self.get_attr(elem, ["color", "textColor"], "#08090e")
        card_size = float(self.get_attr(elem, ["card-size", "size"], 100))
        font_size = float(self.get_attr(elem, ["font-size", "fontSize"], 64))
        gap = float(self.get_attr(elem, ["gap"], 18))
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)
        is_3d = str(self.get_attr(elem, ["is3D", "3d"], "true")).lower() == "true"

        chars = list(text.replace(" ", ""))
        total = len(chars)
        if total == 0:
            return

        total_w = total * card_size + (total - 1) * gap
        start_x = -total_w / 2.0

        for i, ch in enumerate(chars):
            offset_x = start_x + i * (card_size + gap) + card_size / 2.0
            v_c = self.next_var(f"card_tile_{i}")
            v_t = self.next_var(f"card_ch_{i}")
            delay = i * 0.08

            self.js_lines.append(f"""
            var {v_c}_s = Math.round({card_size} * S);
            var {v_c} = s.addRect('{v_c}', -{v_c}_s/2, -{v_c}_s/2, {v_c}_s, {v_c}_s);
            {v_c}.position().setValue([{cx} + ({offset_x} * S), {cy}]);
            {v_c}.setFillColor('{card_bg}');
            {v_c}.setCornerRadius(8 * S);

            var {v_t} = s.addText('{v_t}', '{ch}');
            {v_t}.setFontFamily('{font}');
            {v_t}.setFontSize(Math.round({font_size} * S));
            {v_t}.setFillColor('{color}');
            {v_t}.setTextAlignment('center');
            {v_t}.position().setValue([{cx} + ({offset_x} * S), {cy}]);
            """)

            if is_3d:
                self.js_lines.append(f"""
                {v_c}.set3DEnabled(true);
                {v_c}.rotationY().setValueAtFrame(f({t_from + delay}), 45);
                {v_c}.rotationY().setValueAtFrame(f({t_from + delay + 0.4}), 0);
                {v_t}.set3DEnabled(true);
                {v_t}.rotationY().setValueAtFrame(f({t_from + delay}), 45);
                {v_t}.rotationY().setValueAtFrame(f({t_from + delay + 0.4}), 0);
                """)

            self.apply_opacity_envelope(v_c, t_from + delay, t_to, 0.25)
            self.apply_opacity_envelope(v_t, t_from + delay, t_to, 0.25)

    def render_vertical_text(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        """Vertical Japanese/Chinese typesetting"""
        text = self.get_attr(elem, ["text"], (elem.text or "").strip())
        size = float(self.get_attr(elem, ["size", "font-size"], 24))
        color = self.get_attr(elem, ["color"], "#cbd5e1")
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)
        spacing = float(self.get_attr(elem, ["spacing", "gap"], 6))
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")

        pos_cx = f"({custom_x}*S)" if custom_x else cx
        pos_cy = f"({custom_y}*S)" if custom_y else cy

        chars = list(text)
        for i, ch in enumerate(chars):
            v_ch = self.next_var(f"v_ch_{i}")
            offset_y = i * (size + spacing)
            self.js_lines.append(f"""
            var {v_ch} = s.addText('{v_ch}', '{ch}');
            {v_ch}.setFontFamily('{font}');
            {v_ch}.setFontSize(Math.round({size} * S));
            {v_ch}.setFillColor('{color}');
            {v_ch}.setTextAlignment('center');
            {v_ch}.position().setValue([{pos_cx}, {pos_cy} + ({offset_y} * S)]);
            """)
            self.apply_opacity_envelope(v_ch, t_from + i * 0.04, t_to, 0.2)

    def render_scatter_text(self, elem: ET.Element, t_from: float, t_to: float):
        """Scattered decorative characters in background"""
        chars_pool = self.get_attr(elem, ["chars"], "つもにはでをがのへとサイエンス01")
        count = int(self.get_attr(elem, ["count"], 8))
        color = self.get_attr(elem, ["color"], "#718096")
        min_s = float(self.get_attr(elem, ["min-size"], 16))
        max_s = float(self.get_attr(elem, ["max-size"], 36))
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)

        # Stable pseudo-random grid scatter
        for i in range(count):
            ch = chars_pool[i % len(chars_pool)]
            size = min_s + (i * 7) % int(max_s - min_s + 1)
            px = 120 + (i * 240 + 70) % 1680
            py = 140 + (i * 180 + 50) % 800
            v_sc = self.next_var("sc_ch")
            self.js_lines.append(f"""
            var {v_sc} = s.addText('{v_sc}', '{ch}');
            {v_sc}.setFontFamily('{font}');
            {v_sc}.setFontSize(Math.round({size} * S));
            {v_sc}.setFillColor('{color}');
            {v_sc}.setTextAlignment('center');
            {v_sc}.position().setValue([{px} * S, {py} * S]);
            {v_sc}.opacityProp().setValue(35);
            """)
            self.apply_opacity_envelope(v_sc, t_from, t_to, 0.3)

    def render_formula_scatter(self, elem: ET.Element, t_from: float, t_to: float):
        """Scattered science / math formulas (BluePlane style)"""
        formulas = [
            "E = mc^2", "∇ × B = μ₀J", "iℏ∂ψ/∂t = Ĥψ", "ΔxΔp ≥ ℏ/2",
            "S = k_B ln Ω", "F = dp/dt", "λ = h/p", "∮E⋅dA = Q/ε₀",
            "det(A - λI) = 0", "Q.E.D. // 0x7F"
        ]
        count = int(self.get_attr(elem, ["count"], 8))
        color = self.get_attr(elem, ["color"], "#64748b")
        font = self.get_attr(elem, ["font"], DEFAULT_FONT)

        for i in range(count):
            text = formulas[i % len(formulas)]
            px = 140 + (i * 280 + 90) % 1640
            py = 120 + (i * 190 + 60) % 840
            v_fm = self.next_var("fm_txt")
            self.js_lines.append(f"""
            var {v_fm} = s.addText('{v_fm}', '{text}');
            {v_fm}.setFontFamily('{font}');
            {v_fm}.setFontSize(Math.round(18 * S));
            {v_fm}.setFillColor('{color}');
            {v_fm}.setTextAlignment('left');
            {v_fm}.position().setValue([{px} * S, {py} * S]);
            {v_fm}.opacityProp().setValue(40);
            """)
            self.apply_opacity_envelope(v_fm, t_from, t_to, 0.3)

    def render_concentric(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        """Concentric geometric structure circles / orbits"""
        count = int(self.get_attr(elem, ["count"], 4))
        max_r = float(self.get_attr(elem, ["max-r", "r"], 280))
        stroke = self.get_attr(elem, ["stroke", "color"], "#00f2fe")
        bw = float(self.get_attr(elem, ["bw", "stroke-width"], 1.5))
        is_3d = str(self.get_attr(elem, ["is3D", "3d"], "true")).lower() == "true"
        rot_x = self.get_attr(elem, ["rotX", "rot-x"], "55->70")
        rot_y = self.get_attr(elem, ["rotY", "rot-y"], "0->90")

        step = max_r / float(count)
        for i in range(1, count + 1):
            r = step * i
            v_ring = self.next_var(f"concentric_{i}")
            self.js_lines.append(f"""
            var {v_ring} = s.addEllipse('{v_ring}', 0, 0, Math.round({r} * S));
            {v_ring}.position().setValue([{cx}, {cy}]);
            {v_ring}.setFillColor('transparent');
            {v_ring}.setStrokeColor('{stroke}');
            {v_ring}.setStrokeWidth({bw});
            """)
            if is_3d:
                self.js_lines.append(f"{v_ring}.set3DEnabled(true);")
                if rot_x:
                    rx0, rx1 = self.parse_range(rot_x)
                    self.js_lines.append(f"{v_ring}.rotationX().setValueAtFrame(f({t_from}), {rx0});")
                    self.js_lines.append(f"{v_ring}.rotationX().setValueAtFrame(f({t_to}), {rx1});")
                if rot_y:
                    ry0, ry1 = self.parse_range(rot_y)
                    self.js_lines.append(f"{v_ring}.rotationY().setValueAtFrame(f({t_from}), {ry0});")
                    self.js_lines.append(f"{v_ring}.rotationY().setValueAtFrame(f({t_to}), {ry1});")

            self.apply_opacity_envelope(v_ring, t_from, t_to, 0.3)

    def render_burst_lines(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        """Radial burst rays / speed lines"""
        count = int(self.get_attr(elem, ["count", "rays"], 8))
        inner = float(self.get_attr(elem, ["inner"], 80))
        outer = float(self.get_attr(elem, ["outer"], 450))
        color = self.get_attr(elem, ["color"], "#ff2a6d")
        bw = float(self.get_attr(elem, ["bw", "strokeWidth"], 1.5))
        rot = self.get_attr(elem, ["rot", "rotation"], "0->30")

        for i in range(count):
            angle = i * (360.0 / count)
            v_ray = self.next_var(f"burst_ray_{i}")
            line_len = outer - inner
            center_dist = inner + line_len / 2.0
            self.js_lines.append(f"""
            var {v_ray} = s.addRect('{v_ray}', -{line_len}*S/2, -{bw}*S/2, {line_len}*S, {bw}*S);
            {v_ray}.position().setValue([{cx}, {cy}]);
            {v_ray}.setAnchorPoint([{-center_dist} * S, 0]);
            {v_ray}.setFillColor('{color}');
            {v_ray}.setStrokeWidth(0);
            {v_ray}.rotation().setValue({angle});
            """)
            if rot:
                r0, r1 = self.parse_range(rot)
                self.js_lines.append(f"{v_ray}.rotation().setValueAtFrame(f({t_from}), {angle + r0});")
                self.js_lines.append(f"{v_ray}.rotation().setValueAtFrame(f({t_to}), {angle + r1});")

            self.apply_opacity_envelope(v_ray, t_from, t_to, 0.25)

    def render_ruler(self, elem: ET.Element, t_from: float, t_to: float):
        """Technical measurement ruler scale"""
        x = float(self.get_attr(elem, ["x"], 160))
        y = float(self.get_attr(elem, ["y"], 960))
        w = float(self.get_attr(elem, ["w", "width"], 1600))
        spacing = float(self.get_attr(elem, ["spacing", "gap"], 40))
        color = self.get_attr(elem, ["color"], "#1e2538")
        tick_color = self.get_attr(elem, ["tick-color"], "#00f2fe")

        v_bar = self.next_var("ruler_bar")
        self.js_lines.append(f"""
        var {v_bar} = s.addRect('{v_bar}', {x}*S, {y}*S, {w}*S, 1.5*S);
        {v_bar}.setFillColor('{color}');
        {v_bar}.setStrokeWidth(0);
        """)
        self.apply_opacity_envelope(v_bar, t_from, t_to, 0.25)

        num_ticks = int(w / spacing)
        for i in range(num_ticks + 1):
            tx = x + i * spacing
            th = 10 if (i % 4 == 0) else 5
            v_tk = self.next_var(f"ruler_tk_{i}")
            self.js_lines.append(f"""
            var {v_tk} = s.addRect('{v_tk}', {tx}*S - 0.5, {y}*S - {th}*S, 1*S, {th}*S);
            {v_tk}.setFillColor('{tick_color}');
            {v_tk}.setStrokeWidth(0);
            """)
            self.apply_opacity_envelope(v_tk, t_from, t_to, 0.25)

    def render_bg_blocks(self, elem: ET.Element, t_from: float, t_to: float):
        """Geometric depth / abstract background blocks"""
        count = int(self.get_attr(elem, ["count"], 6))
        color = self.get_attr(elem, ["color"], "#101422")
        alpha = float(self.get_attr(elem, ["alpha"], 40))

        for i in range(count):
            bw = 200 + (i * 120 + 80) % 500
            bh = 150 + (i * 90 + 60) % 400
            bx = 100 + (i * 310 + 40) % 1500
            by = 80 + (i * 220 + 30) % 700
            v_blk = self.next_var(f"bg_blk_{i}")
            self.js_lines.append(f"""
            var {v_blk} = s.addRect('{v_blk}', {bx}*S, {by}*S, {bw}*S, {bh}*S);
            {v_blk}.setFillColor('{color}');
            {v_blk}.setStrokeWidth(0);
            {v_blk}.opacityProp().setValue({alpha});
            """)
            self.apply_opacity_envelope(v_blk, t_from, t_to, 0.3)

    def render_line(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any, parent_3d: Optional[Dict[str, Any]] = None):
        w = float(self.get_attr(elem, ["w", "width"], 500))
        h = float(self.get_attr(elem, ["h", "height"], 3))
        color = self.get_attr(elem, ["color", "fill"], "#ff2a6d")
        motion_in = self.get_attr(elem, ["in", "enter"], "expand")
        delay = float(self.get_attr(elem, ["delay"], 0.1))
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")

        pos_cx = f"({custom_x}*S)" if custom_x else cx
        pos_cy = f"({custom_y}*S)" if custom_y else cy

        act_from = t_from + delay
        v_line = self.next_var("line")
        self.js_lines.append(f"""
        var {v_line}_w = Math.round({w} * S);
        var {v_line}_h = Math.round({h} * S);
        var {v_line} = s.addRect('{v_line}', -{v_line}_w/2, -{v_line}_h/2, {v_line}_w, {v_line}_h);
        {v_line}.position().setValue([{pos_cx}, {pos_cy}]);
        {v_line}.setFillColor('{color}');
        {v_line}.setStrokeWidth(0);
        """)

        if parent_3d and parent_3d.get("is3D"):
            self.js_lines.append(f"{v_line}.set3DEnabled(true);")
            rot_y = parent_3d.get("rotY")
            rot_x = parent_3d.get("rotX")
            if rot_y:
                ry0, ry1 = self.parse_range(rot_y)
                self.js_lines.append(f"{v_line}.rotationY().setValueAtFrame(f({act_from}), {ry0});")
                self.js_lines.append(f"{v_line}.rotationY().setValueAtFrame(f({act_from + 0.45}), {ry1});")
            if rot_x:
                rx0, rx1 = self.parse_range(rot_x)
                self.js_lines.append(f"{v_line}.rotationX().setValueAtFrame(f({act_from}), {rx0});")
                self.js_lines.append(f"{v_line}.rotationX().setValueAtFrame(f({act_from + 0.45}), {rx1});")
            self.js_lines.append(f"{v_line}.zPosition().setValue(15);")

        if motion_in in ["expand", "expand-x"]:
            self.js_lines.append(f"""
            {v_line}.scale().setValueAtFrame(f({act_from}), [0.01, 1]);
            {v_line}.scale().setValueAtFrame(f({act_from + 0.35}), [1.0, 1]);
            """)
        
        self.apply_common_modifiers(v_line, elem)
        self.apply_opacity_envelope(v_line, act_from, t_to, 0.2)

    def render_circle(self, elem: ET.Element, t_from: float, t_to: float, cx: Any, cy: Any):
        r = float(self.get_attr(elem, ["radius", "r"], 200))
        color = self.get_attr(elem, ["color", "fill"], "transparent")
        stroke = self.get_attr(elem, ["stroke", "border"], "#00f2fe")
        stroke_w = float(self.get_attr(elem, ["strokeWidth", "stroke-width", "bw"], 1.5))
        is_3d = str(self.get_attr(elem, ["is3D", "3d", "spatial"], "false")).lower() == "true"
        rot_x = self.get_attr(elem, ["rotX", "rotateX", "rot-x"], "")
        rot_y = self.get_attr(elem, ["rotY", "rotateY", "rot-y"], "")
        custom_x = self.get_attr(elem, ["x"], "")
        custom_y = self.get_attr(elem, ["y"], "")

        pos_cx = f"({custom_x}*S)" if custom_x else cx
        pos_cy = f"({custom_y}*S)" if custom_y else cy

        v_circ = self.next_var("circle")
        self.js_lines.append(f"""
        var {v_circ}_r = Math.round({r} * S);
        var {v_circ} = s.addEllipse('{v_circ}', 0, 0, {v_circ}_r);
        {v_circ}.position().setValue([{pos_cx}, {pos_cy}]);
        {v_circ}.setFillColor('{color}');
        {v_circ}.setStrokeColor('{stroke}');
        {v_circ}.setStrokeWidth({stroke_w});
        """)

        if is_3d or rot_x or rot_y:
            self.js_lines.append(f"{v_circ}.set3DEnabled(true);")
            if rot_x:
                rx0, rx1 = self.parse_range(rot_x)
                self.js_lines.append(f"{v_circ}.rotationX().setValueAtFrame(f({t_from}), {rx0});")
                self.js_lines.append(f"{v_circ}.rotationX().setValueAtFrame(f({t_to}), {rx1});")
            if rot_y:
                ry0, ry1 = self.parse_range(rot_y)
                self.js_lines.append(f"{v_circ}.rotationY().setValueAtFrame(f({t_from}), {ry0});")
                self.js_lines.append(f"{v_circ}.rotationY().setValueAtFrame(f({t_to}), {ry1});")

        self.apply_common_modifiers(v_circ, elem)
        self.apply_opacity_envelope(v_circ, t_from, t_to, 0.3)

    def render_crosshair(self, elem: ET.Element, t_from: float, t_to: float):
        x = float(self.get_attr(elem, ["x"], 100))
        y = float(self.get_attr(elem, ["y"], 100))
        size = float(self.get_attr(elem, ["size"], 14))
        color = self.get_attr(elem, ["color"], "#00f2fe")
        v_h = self.next_var("ch_h")
        v_v = self.next_var("ch_v")
        self.js_lines.append(f"""
        var {v_h} = s.addRect('{v_h}', {x}*S - {size}*S/2, {y}*S - 0.75, {size}*S, 1.5);
        {v_h}.setFillColor('{color}');
        {v_h}.setStrokeWidth(0);
        var {v_v} = s.addRect('{v_v}', {x}*S - 0.75, {y}*S - {size}*S/2, 1.5, {size}*S);
        {v_v}.setFillColor('{color}');
        {v_v}.setStrokeWidth(0);
        """)
        self.apply_opacity_envelope(v_h, t_from, t_to, 0.2)
        self.apply_opacity_envelope(v_v, t_from, t_to, 0.2)

    def render_barcode(self, elem: ET.Element, t_from: float, t_to: float):
        x = float(self.get_attr(elem, ["x"], 100))
        y = float(self.get_attr(elem, ["y"], 100))
        w = float(self.get_attr(elem, ["w", "width"], 100))
        h = float(self.get_attr(elem, ["h", "height"], 16))
        color = self.get_attr(elem, ["color"], "#718096")
        v_bc = self.next_var("barcode")
        self.js_lines.append(f"""
        var {v_bc} = s.addRect('{v_bc}', {x}*S, {y}*S, {w}*S, {h}*S);
        {v_bc}.setFillColor('{color}');
        {v_bc}.setStrokeWidth(0);
        {v_bc}.opacityProp().setValue(40);
        """)
        self.apply_opacity_envelope(v_bc, t_from, t_to, 0.2)

    def apply_common_modifiers(self, var_name: str, elem: ET.Element):
        # 1. Raster Effects
        effects = self.get_attr(elem, ["effects", "effect", "filter"], "")
        if effects:
            for eff in effects.replace(",", " ").split():
                eff = eff.strip()
                if eff:
                    self.js_lines.append(f"{var_name}.addEffect('{eff}');")

        # 2. Blend mode
        blend = self.get_attr(elem, ["blend", "blend-mode", "blendMode"], "")
        if blend:
            self.js_lines.append(f"{var_name}.setBlendMode('{blend}');")

        # 3. Corner radius
        radius = self.get_attr(elem, ["radius", "round", "corner-radius", "rx", "r"], "")
        if radius and elem.tag.lower() in ["card", "div", "box", "rect", "rectangle"]:
            try:
                r_val = float(radius)
                self.js_lines.append(f"{var_name}.setCornerRadius(Math.round({r_val} * S));")
            except ValueError:
                pass

        # 4. Letter spacing & line spacing
        if elem.tag.lower() in ["text", "h1", "h2", "h3", "p", "span", "stagger-text", "kinetic-text"]:
            ls = self.get_attr(elem, ["letter-spacing", "letterSpacing", "spacing"], "")
            if ls:
                try:
                    self.js_lines.append(f"{var_name}.setLetterSpacing({float(ls)});")
                except ValueError:
                    pass
            linesp = self.get_attr(elem, ["line-spacing", "lineSpacing"], "")
            if linesp:
                try:
                    self.js_lines.append(f"{var_name}.setLineSpacing({float(linesp)});")
                except ValueError:
                    pass

    def apply_motion_in(self, var_name: str, motion_name: str, t_in: float, cx: Any, cy: Any):
        m = motion_name.lower()
        if m in ["pop", "pop-spring", "spring", "bounce"]:
            self.js_lines.append(f"""
            {var_name}.scale().setValueAtFrame(f({t_in}), [0.15, 0.15]);
            {var_name}.scale().setValueAtFrame(f({t_in + 0.28}), [1.18, 1.18]);
            {var_name}.scale().setValueAtFrame(f({t_in + 0.42}), [0.96, 0.96]);
            {var_name}.scale().setValueAtFrame(f({t_in + 0.55}), [1.0, 1.0]);
            """)
        elif m in ["slide-up", "up"]:
            self.js_lines.append(f"""
            {var_name}.position().setValueAtFrame(f({t_in}), [{cx}, {cy} + 35*S]);
            {var_name}.position().setValueAtFrame(f({t_in + 0.35}), [{cx}, {cy}]);
            """)
        elif m in ["slide-down", "down"]:
            self.js_lines.append(f"""
            {var_name}.position().setValueAtFrame(f({t_in}), [{cx}, {cy} - 35*S]);
            {var_name}.position().setValueAtFrame(f({t_in + 0.35}), [{cx}, {cy}]);
            """)
        elif m in ["slide-left", "left"]:
            self.js_lines.append(f"""
            {var_name}.position().setValueAtFrame(f({t_in}), [{cx} + 120*S, {cy}]);
            {var_name}.position().setValueAtFrame(f({t_in + 0.35}), [{cx}, {cy}]);
            """)
        elif m in ["slide-right", "right"]:
            self.js_lines.append(f"""
            {var_name}.position().setValueAtFrame(f({t_in}), [{cx} - 120*S, {cy}]);
            {var_name}.position().setValueAtFrame(f({t_in + 0.35}), [{cx}, {cy}]);
            """)
        elif m in ["zoom", "zoom-in", "scale"]:
            self.js_lines.append(f"""
            {var_name}.scale().setValueAtFrame(f({t_in}), [0.6, 0.6]);
            {var_name}.scale().setValueAtFrame(f({t_in + 0.4}), [1.0, 1.0]);
            """)

    def apply_opacity_envelope(self, var_name: str, t_from: float, t_to: float, fade_dur: float = 0.25):
        self.js_lines.append(f"""
        {var_name}.opacityProp().setValueAtFrame(f(0.0), 0);
        if (f({t_from}) > 0) {{ {var_name}.opacityProp().setValueAtFrame(f({t_from}), 0); }}
        {var_name}.opacityProp().setValueAtFrame(f({t_from + fade_dur}), 100);
        {var_name}.opacityProp().setValueAtFrame(f({t_to - fade_dur}), 100);
        {var_name}.opacityProp().setValueAtFrame(f({t_to}), 0);
        """)

def send_request(req_obj: dict, port: int = DEFAULT_HTTP_PORT, socket_path: str = DEFAULT_SOCKET_PATH) -> dict:
    if os.path.exists(socket_path) or sys.platform == "win32":
        try:
            if sys.platform == "win32":
                with open(socket_path, "r+b", buffering=0) as pipe:
                    pipe.write((json.dumps(req_obj) + "\n").encode("utf-8"))
                    return json.loads(pipe.readline().decode("utf-8"))
            else:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                    s.connect(socket_path)
                    s.sendall((json.dumps(req_obj) + "\n").encode("utf-8"))
                    res_data = b""
                    while True:
                        chunk = s.recv(4096)
                        if not chunk: break
                        res_data += chunk
                        if b"\n" in res_data: break
                    return json.loads(res_data.decode("utf-8").strip())
        except Exception:
            pass

    url = f"http://127.0.0.1:{port}/mcp"
    req_data = json.dumps(req_obj).encode("utf-8")
    req = urllib.request.Request(url, data=req_data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))

def render_markup_to_friction(markup_xml: str, port: int = DEFAULT_HTTP_PORT, socket_path: str = DEFAULT_SOCKET_PATH) -> dict:
    compiler = FrictionMarkupCompiler(markup_xml)
    js_code = compiler.compile()

    req = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "friction_eval_script",
            "arguments": {
                "script": js_code,
                "undoGroupName": "Render Friction Markup"
            }
        }
    }
    return send_request(req, port, socket_path)

def main():
    parser = argparse.ArgumentParser(description="Friction 2.5D Semantic HTML Motion Graphics DSL")
    parser.add_argument("file", nargs="?", help="Input .html / .xml file")
    parser.add_argument("-m", "--markup", "--code", type=str, help="Inline HTML markup string")
    parser.add_argument("--compile-only", action="store_true", help="Output compiled JavaScript without sending")
    parser.add_argument("--port", type=int, default=DEFAULT_HTTP_PORT, help="Friction TCP HTTP port")
    parser.add_argument("--socket", type=str, default=DEFAULT_SOCKET_PATH, help="Friction IPC socket path")
    args = parser.parse_args()

    if args.markup:
        markup = args.markup
    elif args.file:
        with open(args.file, "r", encoding="utf-8") as f:
            markup = f.read()
    else:
        # Example demonstrating rich composable MG primitives
        markup = """
<scene width="1920" height="1080" fps="60" duration="8.0" bg="#08090e">
  <hud title="// MOTION ENGINE" sub="60 FPS // SKIA" timecode="true" brackets="true" />
  <crosshair x="120" y="240" size="14" color="#00f2fe" />
  <barcode x="1700" y="230" w="100" h="16" color="#718093" />

  <!-- Act 1: 2.5D Floating Card with Staggered Typewriter Text -->
  <seq from="0.0" to="2.6">
    <bg-blocks count="5" color="#0f1422" alpha="30" />
    <card w="1200" h="460" bg="#101422" border="#1e283d" 3d="true" rotY="-28->0" rotX="16->0">
      <col gap="16">
        <text size="22" color="#00f2fe" in="slide-down">KINETIC ENGINE // PHASE 01</text>
        <h1 size="92" color="#ffffff" anim="rise">KINETIC TYPOGRAPHY</h1>
        <line w="650" color="#ff2a6d" in="expand" />
        <p size="24" color="#94a3b8" in="slide-up">REALTIME 2.5D VECTOR MOTION GRAPHICS</p>
      </col>
    </card>
  </seq>

  <!-- Act 2: Concentric Geometry & Text Ribbon -->
  <seq from="2.6" to="5.2">
    <concentric count="4" max-r="260" stroke="#00f2fe" bw="1.5" 3d="true" rotX="55->70" rotY="0->90" />
    <burst-lines count="8" inner="70" outer="420" color="#ff2a6d" rot="0->30" />
    <col gap="18">
      <text size="24" color="#ffd000" in="slide-down">GEOMETRIC STRUCTURE // 秩序重构</text>
      <h1 size="108" color="#ffffff" anim="zoom">存 在 証 明</h1>
      <line w="520" color="#00f2fe" in="expand" />
      <p size="24" color="#cbd5e1">THE HARMONY OF 2.5D MOTION GRAPHICS</p>
    </col>
    <ruler x="160" y="960" w="1600" spacing="40" color="#1e2538" tick-color="#00f2fe" />
  </seq>

  <!-- Act 3: Glowing Character Cards & Formula Scatter -->
  <seq from="5.2" to="8.0">
    <formula-scatter count="10" color="#64748b" />
    <card w="1300" h="480" bg="#0d121f" border="#ff2a6d" radius="16" 3d="true" rotX="-22->0">
      <col gap="16">
        <text size="24" color="#ffd000" in="slide-down">Q.E.D. // FINAL THEOREM ESTABLISHED</text>
        <h1 size="110" color="#ffffff" anim="typewriter">サイエンス</h1>
        <line w="720" color="#00f2fe" in="expand" />
        <text size="40" color="#ff2a6d" in="zoom">S C I E N C E !</text>
        <p size="22" color="#e2e8f0">MIMI feat. 重音テト // PRODUCED IN FRICTION 2.5D</p>
      </col>
    </card>
  </seq>
</scene>
"""

    if args.compile_only:
        compiler = FrictionMarkupCompiler(markup)
        print(compiler.compile())
        return

    print("Compiling and sending HTML Motion Markup to Friction 2.5D...")
    try:
        res = render_markup_to_friction(markup, args.port, args.socket)
        print("Friction Response:", json.dumps(res, indent=2))
        print("Scene successfully compiled and updated in Friction")
    except Exception as e:
        print(f"Could not connect to Friction ({e})")

if __name__ == "__main__":
    main()
