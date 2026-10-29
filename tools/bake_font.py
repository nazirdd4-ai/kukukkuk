#!/usr/bin/env python3
"""Bake a signed-distance-field glyph atlas into src/include/BaseMenu/font.h.

Usage:
    python3 tools/bake_font.py <font.ttf> src/include/BaseMenu/font.h [base_px] [spread_px]

Requires: Pillow, numpy, scipy (offline tooling only, never part of the Android build).
After regenerating font.h, refresh its entry in src/THIRD_PARTY.sha256:
    cd src && sha256sum include/BaseMenu/font.h
"""
import sys, os, math, hashlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

SS = 4  # supersample factor for the distance computation

# ASCII + Cyrillic (А-я, Ё, ё) + a few typographic symbols
CODEPOINTS = (list(range(32, 127)) + [0x401] + list(range(0x410, 0x450)) + [0x451]
              + [0xAB, 0xBB, 0xB0, 0xB7, 0xD7, 0x2014, 0x2026, 0x2022, 0x2116])

# ── vector icon badges (U+E000..U+E004, Private Use Area — never collides with real text) ──
# Each draws a flat white silhouette on an (ICON_PX + 2*R) square canvas at SS supersample,
# fed through the identical distance_transform_edt SDF pipeline as the TTF glyphs below, so
# Gfx::icon() reuses text()'s exact lookup/sampling code — no new GL texture, no shader change.
ICON_PX = 32
ICON_CODEPOINTS = {
    0xE000: "home",     # ГЛАВНАЯ / HOME tab badge
    0xE001: "crosshair",# AIMBOT tab badge
    0xE002: "eye",      # ESP tab badge
    0xE003: "chip",     # MEMORY tab badge
    0xE004: "gear",     # CONFIG tab badge
}

def _icon_canvas(ss, r):
    L = (ICON_PX + 2 * r) * ss
    img = Image.new("L", (L, L), 0)
    d = ImageDraw.Draw(img)
    # content lives in the inner ICON_PX*ss square, offset by the r*ss spread border
    o = r * ss
    s = ICON_PX * ss
    return img, d, o, s

def draw_icon(kind, ss, r):
    img, d, o, s = _icon_canvas(ss, r)
    cx, cy = o + s / 2, o + s / 2
    if kind == "home":
        # head + shoulders, flat "account" glyph
        headR = s * 0.17
        d.ellipse([cx - headR, o + s * 0.10, cx + headR, o + s * 0.10 + 2 * headR], fill=255)
        bodyTop = o + s * 0.52
        d.pieslice([cx - s * 0.33, bodyTop, cx + s * 0.33, bodyTop + s * 0.66], 180, 360, fill=255)
    elif kind == "crosshair":
        ring = s * 0.015
        rad = s * 0.36
        d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=255, width=max(1, int(ring * 2.4)))
        tick, gap, th = s * 0.16, s * 0.07, max(1, int(ring * 2.6))
        for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            x0 = cx + dx * (rad + gap); y0 = cy + dy * (rad + gap)
            x1 = cx + dx * (rad + gap + tick); y1 = cy + dy * (rad + gap + tick)
            d.line([x0, y0, x1, y1], fill=255, width=th)
        dot = s * 0.045
        d.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=255)
    elif kind == "eye":
        # vesica piscis: two circles offset vertically, lens = intersection of both disks
        rad = s * 0.40
        off = s * 0.235
        yy, xx = np.mgrid[0:img.size[1], 0:img.size[0]]
        dA = np.hypot(xx - cx, yy - (cy - off))
        dB = np.hypot(xx - cx, yy - (cy + off))
        lens = (dA <= rad) & (dB <= rad)
        pupilR = s * 0.13
        dC = np.hypot(xx - cx, yy - cy)
        lens = lens & ~(dC <= pupilR * 0.42)   # hollow pupil ring, matches other icons' hole style
        out = np.where(lens, 255, 0).astype(np.uint8)
        return out, o, s
    elif kind == "chip":
        half = s * 0.26
        d.rectangle([cx - half, cy - half, cx + half, cy + half], fill=255)
        pinLen, pinTh, pinGap = s * 0.09, s * 0.035, s * 0.20
        for sx in (-1, 1):
            for k in (-1, 1):
                py = cy + k * pinGap
                x0 = cx + sx * half; x1 = x0 + sx * (half + pinLen)
                d.line([x0, py, x1, py], fill=255, width=max(1, int(pinTh)))
        for sy in (-1, 1):
            for k in (-1, 1):
                px = cx + k * pinGap
                y0 = cy + sy * half; y1 = y0 + sy * (half + pinLen)
                d.line([px, y0, px, y1], fill=255, width=max(1, int(pinTh)))
    elif kind == "gear":
        teeth, outerR, rootR, toothW = 8, s * 0.40, s * 0.30, 0.34
        pts = []
        for i in range(teeth * 2):
            ang = math.pi * i / teeth
            rad = outerR if (i % 2 == 0) else rootR
            halfw = toothW * (math.pi / teeth)
            a0, a1 = ang - halfw, ang + halfw
            if i % 2 == 0:
                pts.append((cx + rootR * math.cos(a0), cy + rootR * math.sin(a0)))
                pts.append((cx + outerR * math.cos(ang), cy + outerR * math.sin(ang)))
                pts.append((cx + rootR * math.cos(a1), cy + rootR * math.sin(a1)))
            else:
                pts.append((cx + rootR * math.cos(ang), cy + rootR * math.sin(ang)))
        d.polygon(pts, fill=255)
        holeR = s * 0.13
        d.ellipse([cx - holeR, cy - holeR, cx + holeR, cy + holeR], fill=0)
    return img, o, s


def main():
    font_path, out_path = sys.argv[1], sys.argv[2]
    B = int(sys.argv[3]) if len(sys.argv) > 3 else 40   # em size of the baked glyphs, px
    R = int(sys.argv[4]) if len(sys.argv) > 4 else 5    # SDF spread, px
    hi = ImageFont.truetype(font_path, B * SS)

    l, t, r, b = hi.getbbox("H", anchor="ls")
    cap = -t / SS

    glyphs = []
    for cp in CODEPOINTS:
        ch = chr(cp)
        adv = hi.getlength(ch) / SS
        bb = hi.getbbox(ch, anchor="ls")
        empty = (ch == " ") or (bb[2] <= bb[0]) or (bb[3] <= bb[1])
        if empty:
            glyphs.append(dict(cp=cp, w=0, h=0, bx=0, by=0, adv=adv, img=None))
            continue
        l, t, r, b = bb
        left = math.floor(l / SS) - R
        top = math.ceil(-t / SS) + R            # px above baseline
        w = math.ceil(r / SS) - math.floor(l / SS) + 2 * R
        h = top + math.ceil(b / SS) + R
        canvas = Image.new("L", (w * SS, h * SS), 0)
        ImageDraw.Draw(canvas).text((-left * SS, top * SS), ch, font=hi, anchor="ls", fill=255)
        m = np.asarray(canvas) >= 128
        din = distance_transform_edt(m)
        dout = distance_transform_edt(~m)
        d = np.where(m, din - 0.5, -(dout - 0.5)) / SS         # >0 inside, base px
        d = d.reshape(h, SS, w, SS).mean(axis=(1, 3))
        a = np.clip(0.5 + d / (2.0 * R), 0.0, 1.0)
        glyphs.append(dict(cp=cp, w=w, h=h, bx=left, by=top, adv=adv,
                           img=np.round(a * 255).astype(np.uint8)))

    for cp, kind in ICON_CODEPOINTS.items():
        canvas, o, s = draw_icon(kind, SS, R)
        w = h = (ICON_PX + 2 * R)
        m = np.asarray(canvas) >= 128
        din = distance_transform_edt(m)
        dout = distance_transform_edt(~m)
        d = np.where(m, din - 0.5, -(dout - 0.5)) / SS
        d = d.reshape(h, SS, w, SS).mean(axis=(1, 3))
        a = np.clip(0.5 + d / (2.0 * R), 0.0, 1.0)
        glyphs.append(dict(cp=cp, w=w, h=h, bx=0, by=0, adv=float(w),
                           img=np.round(a * 255).astype(np.uint8)))

    # shelf packing, tallest first
    AW = 512
    order = sorted([g for g in glyphs if g["img"] is not None], key=lambda g: -g["h"])
    x = y = rowh = 0
    for g in order:
        if x + g["w"] + 1 > AW:
            x, y, rowh = 0, y + rowh + 1, 0
        g["x"], g["y"] = x, y
        x += g["w"] + 1
        rowh = max(rowh, g["h"])
    AH = y + rowh
    atlas = np.zeros((AH, AW), np.uint8)
    for g in order:
        atlas[g["y"]:g["y"] + g["h"], g["x"]:g["x"] + g["w"]] = g["img"]
    for g in glyphs:
        g.setdefault("x", 0); g.setdefault("y", 0)
    glyphs.sort(key=lambda g: g["cp"])

    fsha = hashlib.sha256(open(font_path, "rb").read()).hexdigest()[:16]
    with open(out_path, "w") as f:
        f.write("#pragma once\n")
        f.write("// GENERATED by tools/bake_font.py - do not edit by hand.\n")
        f.write("// Source font: %s (sha256 %s...)\n" % (os.path.basename(font_path), fsha))
        f.write("// DejaVu Sans: (c) Bitstream Vera / DejaVu authors, free license (keep the notice when redistributing the font data).\n")
        f.write("// Signed-distance-field atlas, 8-bit: 0.5 = glyph edge, >0.5 inside. base=%dpx em, spread=%dpx.\n" % (B, R))
        f.write("// Glyph rect (x,y,w,h) includes the spread border. bx = left edge relative to the pen, by = top edge above the baseline.\n")
        f.write("#include <cstdint>\n\n")
        f.write("static const int   kFontAtlasW = %d;\n" % AW)
        f.write("static const int   kFontAtlasH = %d;\n" % AH)
        f.write("static const float kFontBase   = %d.f;   // baked em size, px\n" % B)
        f.write("static const float kFontSpread = %d.f;   // SDF spread, px\n" % R)
        f.write("static const float kFontCap    = %.4ff; // cap height (H) at base size, px\n\n" % cap)
        f.write("struct FontGlyph { uint32_t cp; uint16_t x, y, w, h; int16_t bx, by; float adv; };\n")
        f.write("static const int kFontGlyphCount = %d;\n" % len(glyphs))
        f.write("static const FontGlyph kFontGlyphs[kFontGlyphCount] = {\n")
        for g in glyphs:
            f.write("    {0x%X,%d,%d,%d,%d,%d,%d,%.4ff},\n" % (g["cp"], g["x"], g["y"], g["w"], g["h"], g["bx"], g["by"], g["adv"]))
        f.write("};\n\n")
        f.write("static const unsigned char kFontAtlas[kFontAtlasW * kFontAtlasH] = {\n")
        flat = atlas.reshape(-1)
        for i in range(0, len(flat), 32):
            f.write("    " + ",".join(str(int(v)) for v in flat[i:i + 32]) + ",\n")
        f.write("};\n")
    print("glyphs=%d atlas=%dx%d cap=%.3f bytes=%d" % (len(glyphs), AW, AH, cap, os.path.getsize(out_path)))
    Image.fromarray(atlas).save(os.path.splitext(out_path)[0] + "_atlas_preview.png")

main()
