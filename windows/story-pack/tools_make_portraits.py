#!/usr/bin/env python3
"""Original placeholder portraits (stylised duelist silhouettes) for the story mod.
Not based on any official artwork: generic bust shapes, colour themes and glowing eyes."""
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/portrait_pack/story/portraits"
os.makedirs(OUT, exist_ok=True)
W, H = 360, 540
S = 2  # supersampling

def rgba(c, a=255):
    return (int(c[0]), int(c[1]), int(c[2]), int(a))

def mask_new():
    return Image.new("L", (W * S, H * S), 0)

def body_mask(build):
    m = mask_new(); d = ImageDraw.Draw(m)
    sw = {"slim": 150, "normal": 168, "broad": 184}[build]
    cx = W * S // 2
    # shoulders / torso
    top = 360 * S
    d.rounded_rectangle([cx - sw * S, top, cx + sw * S, (H + 60) * S], radius=90 * S, fill=255)
    # neck
    d.rounded_rectangle([cx - 34 * S, 260 * S, cx + 34 * S, 380 * S], radius=20 * S, fill=255)
    # trapezius slope
    d.polygon([(cx - 40 * S, 300 * S), (cx - sw * S + 30 * S, top + 10 * S), (cx + sw * S - 30 * S, top + 10 * S), (cx + 40 * S, 300 * S)], fill=255)
    return m

def head_mask():
    m = mask_new(); d = ImageDraw.Draw(m)
    cx = W * S // 2
    d.ellipse([cx - 74 * S, 120 * S, cx + 74 * S, 300 * S], fill=255)
    # jaw
    d.polygon([(cx - 70 * S, 230 * S), (cx, 318 * S), (cx + 70 * S, 230 * S)], fill=255)
    return m

def hair_mask(style):
    m = mask_new(); d = ImageDraw.Draw(m)
    cx = W * S // 2
    if style == "short":
        d.ellipse([cx - 84 * S, 100 * S, cx + 84 * S, 236 * S], fill=255)
        for i in range(-3, 4):
            x = cx + i * 22 * S
            d.polygon([(x - 18 * S, 160 * S), (x + 4 * S, 205 * S + abs(i) * 4 * S), (x + 22 * S, 160 * S)], fill=255)
    elif style == "long":
        d.ellipse([cx - 88 * S, 98 * S, cx + 88 * S, 240 * S], fill=255)
        d.rounded_rectangle([cx - 96 * S, 150 * S, cx - 52 * S, 400 * S], radius=30 * S, fill=255)
        d.rounded_rectangle([cx + 52 * S, 150 * S, cx + 96 * S, 400 * S], radius=30 * S, fill=255)
    elif style == "swept":
        d.ellipse([cx - 86 * S, 96 * S, cx + 84 * S, 230 * S], fill=255)
        d.polygon([(cx - 90 * S, 150 * S), (cx + 40 * S, 120 * S), (cx + 120 * S, 175 * S), (cx + 20 * S, 190 * S), (cx - 60 * S, 215 * S)], fill=255)
    elif style == "hood":
        d.ellipse([cx - 104 * S, 84 * S, cx + 104 * S, 330 * S], fill=255)
        d.polygon([(cx - 104 * S, 220 * S), (cx - 150 * S, 400 * S), (cx + 150 * S, 400 * S), (cx + 104 * S, 220 * S)], fill=255)
    elif style == "cap":
        d.ellipse([cx - 82 * S, 104 * S, cx + 82 * S, 220 * S], fill=255)
        d.rounded_rectangle([cx - 86 * S, 150 * S, cx + 130 * S, 172 * S], radius=10 * S, fill=255)
    elif style == "tall":
        d.ellipse([cx - 84 * S, 100 * S, cx + 84 * S, 230 * S], fill=255)
        d.polygon([(cx - 70 * S, 140 * S), (cx - 30 * S, 40 * S), (cx + 10 * S, 120 * S), (cx + 50 * S, 50 * S), (cx + 76 * S, 150 * S)], fill=255)
    return m

def face_cut(style):
    """area of the face left visible (dark) inside hood"""
    m = mask_new(); d = ImageDraw.Draw(m)
    cx = W * S // 2
    if style == "hood":
        d.ellipse([cx - 66 * S, 150 * S, cx + 66 * S, 312 * S], fill=255)
    return m

def arr(m):
    return np.asarray(m.resize((W, H), Image.LANCZOS), dtype=np.float32) / 255.0

def make(slug, style, build, c1, c2, emblem):
    body = arr(body_mask(build))
    head = arr(head_mask())
    hair = arr(hair_mask(style))
    sil = np.clip(body + head + hair, 0, 1)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    c1 = np.array(c1, np.float32); c2 = np.array(c2, np.float32)

    # base fill: dark tint of c1, lighter toward the bottom
    dark = c1 * 0.16 + 8
    grad = np.clip((yy - 250) / 300, 0, 1)[..., None]
    fill = dark * (1 - grad) + (c1 * 0.42) * grad
    # hair a bit more saturated
    hair_only = np.clip(hair - head * 0.0, 0, 1)[..., None]
    fill = fill * (1 - hair_only * 0.5) + (c1 * 0.30 + 10) * hair_only * 0.5

    # rim light: silhouette minus shifted silhouette (light from upper left and right)
    def shift(a, dx, dy):
        return np.roll(np.roll(a, dy, axis=0), dx, axis=1)
    rim_l = np.clip(sil - shift(sil, 5, 3), 0, 1)
    rim_r = np.clip(sil - shift(sil, -5, 3), 0, 1)
    rim = Image.fromarray((np.clip(rim_l * 1.0 + rim_r * 0.7, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.6))
    rim = np.asarray(rim, np.float32)[..., None] / 255.0
    col = fill + rim * (c2 * 0.9 + 40)

    img = np.zeros((H, W, 4), np.float32)
    img[..., :3] = col
    img[..., 3] = sil * 255

    # hood: dark face opening
    fc = arr(face_cut(style))
    if fc.any():
        img[..., :3] = img[..., :3] * (1 - fc[..., None] * 0.75)

    pil = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(pil, "RGBA")
    cx = W // 2

    # coat collar + stripe
    d.line([(cx - 120, 372), (cx, 470), (cx + 120, 372)], fill=rgba(c2, 200), width=5)
    d.line([(cx - 132, 392), (cx - 8, 500)], fill=rgba(c1, 150), width=3)
    d.line([(cx + 132, 392), (cx + 8, 500)], fill=rgba(c1, 150), width=3)

    # chest emblem
    ex, ey, r = cx, 500, 16
    if emblem == "circle":
        d.ellipse([ex - r, ey - r, ex + r, ey + r], outline=rgba(c2, 230), width=3)
        d.ellipse([ex - 5, ey - 5, ex + 5, ey + 5], fill=rgba(c2, 230))
    elif emblem == "hex":
        pts = [(ex + r * math.cos(math.pi / 3 * i + math.pi / 6), ey + r * math.sin(math.pi / 3 * i + math.pi / 6)) for i in range(6)]
        d.polygon(pts, outline=rgba(c2, 230), width=3)
    elif emblem == "star":
        pts = []
        for i in range(10):
            rr = r if i % 2 == 0 else r * 0.45
            a = -math.pi / 2 + math.pi / 5 * i
            pts.append((ex + rr * math.cos(a), ey + rr * math.sin(a)))
        d.polygon(pts, fill=rgba(c2, 220))
    elif emblem == "diamond":
        d.polygon([(ex, ey - r), (ex + r * 0.7, ey), (ex, ey + r), (ex - r * 0.7, ey)], outline=rgba(c2, 230), width=3)

    # glowing eyes
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for side in (-1, 1):
        x = cx + side * 30
        gd.polygon([(x - 17, 222), (x + 2 * side, 214), (x + 17, 220), (x - side * 2, 228)], fill=rgba(c2, 255))
    glow_b = glow.filter(ImageFilter.GaussianBlur(6))
    pil = Image.alpha_composite(pil, glow_b)
    pil = Image.alpha_composite(pil, glow_b)
    pil = Image.alpha_composite(pil, glow)

    # soft back glow (outside the silhouette) so it reads on dark scenes
    halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    hd = ImageDraw.Draw(halo)
    hd.ellipse([cx - 150, 60, cx + 150, 420], fill=rgba(c1, 90))
    halo = halo.filter(ImageFilter.GaussianBlur(40))
    # fade the bottom of the bust so it blends into the dialog box
    out = Image.alpha_composite(halo, pil)
    a = np.asarray(out, np.float32)
    fade = np.clip((H - yy) / 70, 0, 1)
    a[..., 3] *= fade
    Image.fromarray(a.astype(np.uint8), "RGBA").save(os.path.join(OUT, slug + ".png"), optimize=True)

CHARS = [
    # slug, story name, style, build, main colour, light colour, emblem
    ("player", "คุณ", "short", "normal", (40, 150, 200), (140, 240, 255), "star"),
    ("joey", "โจโนะอุจิ", "short", "normal", (60, 150, 80), (255, 210, 90), "circle"),
    ("marik", "มาลิก", "hood", "slim", (120, 60, 170), (255, 200, 80), "diamond"),
    ("kaiba", "ไคบะ", "swept", "broad", (40, 80, 200), (200, 235, 255), "diamond"),
    ("yami_yugi", "ยามิยูกิ", "tall", "normal", (150, 40, 70), (255, 200, 90), "star"),
    ("manjoume", "มันโจเมะ", "swept", "slim", (40, 40, 70), (150, 170, 255), "hex"),
    ("ryo", "ไคเซอร์ เรียว", "long", "broad", (40, 120, 170), (150, 240, 255), "hex"),
    ("judai", "จูได", "short", "normal", (190, 50, 40), (255, 190, 90), "star"),
    ("crow", "โครว์", "tall", "slim", (40, 50, 90), (255, 160, 60), "diamond"),
    ("jack", "แจ็ค แอตลาส", "swept", "broad", (180, 170, 190), (255, 225, 120), "diamond"),
    ("yusei", "ยูเซย์", "tall", "normal", (40, 70, 160), (255, 210, 110), "hex"),
    ("shark", "ชาร์ค", "swept", "slim", (90, 60, 150), (170, 255, 230), "diamond"),
    ("kaito", "ไคโตะ", "long", "slim", (150, 170, 200), (120, 230, 255), "hex"),
    ("yuma", "ยูมะ", "tall", "normal", (200, 60, 90), (255, 220, 120), "star"),
    ("reiji", "เรย์จิ", "swept", "broad", (130, 130, 150), (255, 90, 90), "hex"),
    ("yuri", "ยูริ", "short", "slim", (130, 50, 160), (230, 140, 255), "diamond"),
    ("yuya", "ยูยะ", "cap", "normal", (60, 150, 90), (255, 110, 90), "star"),
    ("soulburner", "โซลเบิร์นเนอร์", "tall", "broad", (200, 70, 30), (255, 210, 90), "diamond"),
    ("revolver", "รีวอลเวอร์", "hood", "broad", (170, 170, 180), (255, 120, 80), "hex"),
    ("playmaker", "เพลย์เมกเกอร์", "swept", "normal", (40, 140, 110), (150, 255, 200), "hex"),
    ("ai", "ไอ", "short", "slim", (90, 50, 140), (190, 140, 255), "circle"),
    ("mystery", "เสียงปริศนา", "hood", "slim", (50, 50, 60), (200, 60, 255), "circle"),
]

names = {}
for slug, name, style, build, c1, c2, em in CHARS:
    make(slug, style, build, c1, c2, em)
    names[name] = slug + ".png"

with open(os.path.join(OUT, "portraits.json"), "w", encoding="utf-8") as f:
    json.dump({
        "_comment": "ชื่อผู้พูด/คู่ต่อสู้ -> ไฟล์ภาพในโฟลเดอร์นี้ (PNG/JPG, แนะนำภาพพื้นหลังโปร่งใส สัดส่วน 2:3). อยากเปลี่ยนภาพ: วางไฟล์ทับชื่อเดิม หรือแก้ชื่อไฟล์ตรงนี้",
        "player": "player.png",
        "default": "mystery.png",
        "names": names,
    }, f, ensure_ascii=False, indent=1)
print("ok", len(CHARS))
