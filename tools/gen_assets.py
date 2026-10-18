#!/usr/bin/env python3
"""Regenerates every generated asset for PongBrew 2.

Needs: python3, Pillow, numpy, ffmpeg (for the two songs).
Run from the project root:  python3 tools/gen_assets.py

Outputs
  assets/menu_bg.bmp         custom main-menu background (1920x1080, title baked in)
  assets/game_bg.bmp         in-game arena background (1920x1080)
  assets/credits.bmp         credit image for MexrlDev (resized from assets_src/)
  assets/menu_music.mp3      menu song (from assets_src/Menu_source.mp3), 48 kHz stereo, tags stripped
  assets/game_music.mp3      in-game song (from assets_src/Game_source.mp3), same encoding
  sce_sys/icon0.png          512x512 XMB icon
  sce_sys/pic1.png           1920x1080 splash
  src/font_data.h            embedded Poppins Bold glyph atlases (alpha only)
"""
import math, os, random, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def P(*a): return os.path.join(ROOT, *a)

W, H = 1920, 1080
FONT_PATHS = [
    "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf",
    P("tools", "Poppins-Bold.ttf"),
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]
def font(size):
    for p in FONT_PATHS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

rng = random.Random(7)
nrng = np.random.default_rng(7)

def lerp(a, b, t): return a + (b - a) * t

def vgrad(h, stops):
    """stops: [(pos 0..1, (r,g,b))] -> h x 3 float array"""
    out = np.zeros((h, 3), dtype=np.float32)
    ys = np.linspace(0, 1, h)
    for c in range(3):
        out[:, c] = np.interp(ys, [s[0] for s in stops], [s[1][c] for s in stops])
    return out

def glow(layer, radius, strength=1.0):
    """layer: RGBA PIL image drawn on black transparent; returns blurred additive copy."""
    b = layer.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        a = np.asarray(b).astype(np.float32); a[..., :3] *= strength
        b = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")
    return b

def add(base_rgb, layer_rgba):
    """additive blend of an RGBA layer onto an RGB float array (alpha weights)."""
    l = np.asarray(layer_rgba).astype(np.float32)
    base_rgb += l[..., :3] * (l[..., 3:4] / 255.0)
    return base_rgb

def over(base_rgb, layer_rgba):
    l = np.asarray(layer_rgba).astype(np.float32)
    a = l[..., 3:4] / 255.0
    return base_rgb * (1 - a) + l[..., :3] * a

def vignette(arr, power=0.45):
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)
    v = 1.0 - np.clip((d - 0.55) / 0.95, 0, 1) ** 1.6 * power * 2
    return arr * v[..., None]

def halftone(arr, region, spacing=18, strength=0.10, colr=(120, 200, 255)):
    x0, y0, x1, y1 = region
    lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for y in range(y0, y1, spacing):
        for x in range(x0 + ((y // spacing) % 2) * spacing // 2, x1, spacing):
            # radius shrinks away from the corner the region is anchored to
            t = 1 - math.hypot(x - x0, y - y0) / math.hypot(x1 - x0, y1 - y0)
            r = max(0, 4.2 * t)
            if r > 0.6:
                d.ellipse((x - r, y - r, x + r, y + r), fill=colr + (255,))
    a = np.asarray(lay).astype(np.float32)
    arr += a[..., :3] * (a[..., 3:4] / 255.0) * strength
    return arr

# ---------------------------------------------------------------- menu bg
def make_menu_bg():
    arr = np.zeros((H, W, 3), dtype=np.float32)
    sky = vgrad(H, [(0.0, (6, 9, 30)), (0.38, (18, 30, 92)), (0.62, (74, 38, 130)), (0.74, (214, 50, 138)), (1.0, (20, 12, 48))])
    arr[:] = sky[:, None, :]

    # stars
    st = Image.new("RGBA", (W, H), (0, 0, 0, 0)); sd = ImageDraw.Draw(st)
    for _ in range(420):
        x = rng.randrange(W); y = int(rng.random() ** 1.7 * 640)
        r = rng.choice([1, 1, 1, 2, 2, 3]); a = rng.randrange(110, 255)
        c = rng.choice([(200, 230, 255), (150, 255, 235), (255, 190, 235)])
        sd.ellipse((x - r, y - r, x + r, y + r), fill=c + (a,))
    arr = add(arr, st)
    arr = add(arr, glow(st, 3, 1.5))

    # big moon with scanline cuts
    cx, cy, R = 1390, 430, 250
    yy, xx = np.mgrid[0:H, 0:W]
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    disc = np.clip((R - dist) / 2.5, 0, 1)
    t = np.clip((yy - (cy - R)) / (2 * R), 0, 1)
    moon = np.stack([lerp(120, 255, t), lerp(235, 70, t), lerp(235, 170, t)], axis=-1)
    cut = np.ones_like(t)
    for k in range(14):
        yk = cy + 20 + k * 17
        thick = 2 + k * 0.9
        cut[(yy > yk) & (yy < yk + thick)] = 0.0
    cut = np.where(yy > cy + 10, cut, 1.0)
    arr = arr * (1 - disc[..., None] * cut[..., None]) + moon * (disc * cut)[..., None]
    halo = np.clip(1 - np.clip(dist - R, 0, 400) / 400, 0, 1) ** 3 * 0.55
    arr += halo[..., None] * np.array([90, 60, 190], dtype=np.float32)

    # skyline (two layers)
    sk = Image.new("RGBA", (W, H), (0, 0, 0, 0)); skd = ImageDraw.Draw(sk)
    base = 760
    def city(layer_color, hmin, hmax, wmin, wmax, y_base, windows):
        x = -20
        while x < W + 20:
            bw = rng.randrange(wmin, wmax); bh = rng.randrange(hmin, hmax)
            skd.rectangle((x, y_base - bh, x + bw, y_base + 4), fill=layer_color)
            if rng.random() < 0.3:
                skd.rectangle((x + bw // 2 - 3, y_base - bh - rng.randrange(20, 60), x + bw // 2 + 3, y_base - bh), fill=layer_color)
            if windows:
                for wy in range(y_base - bh + 14, y_base - 12, 22):
                    for wx in range(x + 10, x + bw - 12, 20):
                        if rng.random() < 0.28:
                            col = rng.choice([(255, 220, 120), (120, 255, 230), (255, 130, 210)])
                            skd.rectangle((wx, wy, wx + 7, wy + 10), fill=col + (230,))
            x += bw + rng.randrange(0, 10)
    city((40, 26, 92, 255), 80, 300, 50, 120, base, False)
    city((14, 10, 40, 255), 60, 380, 60, 150, base + 20, True)
    sky_layer = np.asarray(sk).astype(np.float32)
    a = sky_layer[..., 3:4] / 255.0
    arr = arr * (1 - a) + sky_layer[..., :3] * a
    arr = add(arr, glow(sk.copy().point(lambda v: v), 6, 0.0))  # no-op placeholder keeps pipeline simple

    # floor: dark plane + neon perspective grid
    horizon = base + 20
    floor = vgrad(H - horizon, [(0, (36, 14, 70)), (1, (6, 4, 18))])
    arr[horizon:] = floor[:, None, :]
    gl = Image.new("RGBA", (W, H), (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
    vx = W * 0.5
    for i in range(-30, 31):
        x_bottom = vx + i * 190
        gd.line((vx + i * 14, horizon, x_bottom, H), fill=(255, 70, 190, 200), width=2)
    for k in range(1, 16):
        t = (k / 15.0) ** 2.2
        y = horizon + t * (H - horizon)
        gd.line((0, y, W, y), fill=(120, 230, 255, int(80 + 120 * t)), width=2)
    gl2 = glow(gl, 4, 1.6)
    arr = add(arr, gl2); arr = add(arr, gl)

    # pong motif: ball with long trail + two paddles on the right half
    pm = Image.new("RGBA", (W, H), (0, 0, 0, 0)); pd = ImageDraw.Draw(pm)
    for i in range(18):
        r = 22 - i * 0.9; x = 1090 + i * 30; y = 300 + i * 40
        pd.ellipse((x - r, y - r, x + r, y + r), fill=(255, 235, 140, max(10, 230 - i * 14)))
    pd.rounded_rectangle((1260, 120, 1620, 150), 14, fill=(255, 110, 140, 255))
    pd.rounded_rectangle((1010, 905, 1370, 935), 14, fill=(110, 225, 255, 255))
    arr = add(arr, glow(pm, 14, 1.8)); arr = over(arr, pm)

    arr = halftone(arr, (W - 520, 0, W, 360), 20, 0.16, (255, 120, 200))
    arr = halftone(arr, (0, H - 360, 520, H), 20, 0.12, (100, 210, 255))

    # left side darkening so menu text reads
    xs = np.linspace(0, 1, W)
    dark = np.clip(0.80 - xs / 0.52, 0, 0.80)
    arr *= (1 - dark)[None, :, None]
    arr = vignette(arr, 0.5)

    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")

    # baked-in title
    tl = Image.new("RGBA", (W, H), (0, 0, 0, 0)); td = ImageDraw.Draw(tl)
    f = font(230)
    td.text((120, 70), "PONG", font=f, fill=(255, 255, 255, 255))
    glow_l = Image.new("RGBA", (W, H), (0, 0, 0, 0)); gdd = ImageDraw.Draw(glow_l)
    gdd.text((120, 70), "PONG", font=f, fill=(90, 200, 255, 255))
    for i, off in enumerate((5, 10)):
        gdd.text((120 + off, 70 + off), "PONG", font=f, fill=(255, 70, 170, 255))
    img = Image.alpha_composite(img.convert("RGBA"), glow(glow_l, 22, 1.4))
    img = Image.alpha_composite(img, glow_l.point(lambda v: v) if False else Image.new("RGBA", (W, H), (0, 0, 0, 0)))
    # hard drop shadow offsets then the white face
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0)); shd = ImageDraw.Draw(sh)
    shd.text((130, 80), "PONG", font=f, fill=(255, 70, 170, 255))
    shd.text((125, 75), "PONG", font=f, fill=(90, 200, 255, 255))
    img = Image.alpha_composite(img, sh)
    img = Image.alpha_composite(img, tl)
    d = ImageDraw.Draw(img)
    d.text((128, 322), "PS4 HOMEBREW  /  BY MEXRLDEV", font=font(34), fill=(190, 225, 255, 255))
    d.rectangle((128, 372, 628, 376), fill=(255, 90, 180, 255))
    return img.convert("RGB")

# ---------------------------------------------------------------- game bg
def make_game_bg():
    arr = np.zeros((H, W, 3), dtype=np.float32)
    sky = vgrad(H, [(0, (5, 8, 24)), (0.5, (12, 20, 56)), (1.0, (6, 9, 28))])
    arr[:] = sky[:, None, :]
    yy, xx = np.mgrid[0:H, 0:W]
    # soft colored wells at the two goal ends
    for (cx, cy, col) in ((W / 2, -120, (120, 40, 70)), (W / 2, H + 120, (30, 90, 120))):
        d = np.sqrt(((xx - cx) / 1100.0) ** 2 + ((yy - cy) / 420.0) ** 2)
        arr += (np.clip(1 - d, 0, 1) ** 2)[..., None] * np.array(col, dtype=np.float32) * 0.55
    gl = Image.new("RGBA", (W, H), (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
    for x in range(0, W + 1, 120):
        gd.line((x, 0, x, H), fill=(110, 150, 255, 26), width=1)
    for y in range(0, H + 1, 120):
        gd.line((0, y, W, y), fill=(110, 150, 255, 26), width=1)
    # court frame
    gd.rounded_rectangle((60, 40, W - 60, H - 40), 30, outline=(120, 190, 255, 70), width=3)
    gd.ellipse((W / 2 - 150, H / 2 - 150, W / 2 + 150, H / 2 + 150), outline=(120, 190, 255, 55), width=3)
    arr = add(arr, gl)
    arr = halftone(arr, (0, 0, 420, 300), 22, 0.08, (255, 120, 190))
    arr = halftone(arr, (W - 420, H - 300, W, H), 22, 0.08, (100, 210, 255))
    arr = vignette(arr, 0.55)
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")

# ---------------------------------------------------------------- splash / icon
def make_splash(menu_img):
    img = menu_img.copy().convert("RGBA")
    dark = Image.new("RGBA", (W, H), (0, 0, 0, 90))
    img = Image.alpha_composite(img, dark)
    d = ImageDraw.Draw(img)
    f = font(44)
    txt = "LOADING"
    w = d.textlength(txt, font=f)
    d.text(((W - w) / 2, H - 150), txt, font=f, fill=(210, 235, 255, 255))
    return img.convert("RGB")

def make_icon():
    S = 512
    arr = np.zeros((S, S, 3), dtype=np.float32)
    arr[:] = vgrad(S, [(0, (10, 14, 48)), (0.6, (58, 30, 122)), (1, (170, 40, 120))])[:, None, :]
    lay = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(lay)
    d.rounded_rectangle((120, 70, 392, 104), 17, fill=(255, 110, 140, 255))
    d.rounded_rectangle((120, 408, 392, 442), 17, fill=(110, 225, 255, 255))
    for i in range(7):
        r = 26 - i * 2.2
        d.ellipse((290 - i * 20 - r, 170 + i * 28 - r, 290 - i * 20 + r, 170 + i * 28 + r), fill=(255, 235, 140, max(30, 255 - i * 36)))
    for y in range(120, 400, 40):
        d.rectangle((250, y, 262, y + 20), fill=(255, 255, 255, 60))
    arr = add(arr, glow(lay, 12, 1.6)); arr = over(arr, lay)
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")
    return img

# ---------------------------------------------------------------- font atlas header
# (pixel size at the native 1920x1080 canvas, number of characters). Sizes of 56 and up only carry ASCII 32..95
# (digits, capitals, punctuation); the game only draws capitals and digits at those sizes.
FONT_SPECS = [(24, 95), (26, 95), (28, 95), (30, 95), (32, 95), (34, 95), (36, 95), (38, 95), (40, 95),
              (52, 95), (56, 64), (64, 95), (80, 64), (90, 64), (104, 64), (110, 64), (130, 64), (150, 64)]

FONT_SPECS_540 = [(12, 95), (13, 95), (14, 95), (15, 95), (16, 95), (17, 95), (18, 95), (19, 95), (20, 95), (26, 95), (32, 95),
                  (48, 64), (64, 64)]      # half-size atlases for the optional 960x540 build (make SD=1)

def make_font_header(specs=None, name="font_data.h"):
    """One alpha atlas per size, so text is blitted 1:1 at the 1920x1080 canvas (no scaling in the hot path)."""
    out = ["/* generated by tools/gen_assets.py, Poppins Bold (SIL Open Font License) */",
           "#ifndef FONT_DATA_H", "#define FONT_DATA_H", "",
           "typedef struct { int px, cell_w, cell_h, cols, pad, atlas_w, atlas_h, nch; const float *adv; const unsigned char *pix; } FontAtlas;", ""]
    rows_out = []
    specs = specs or FONT_SPECS
    for k, (size, nch) in enumerate(specs):
        chars = [chr(c) for c in range(32, 32 + nch)]
        f = font(size)
        asc, desc = f.getmetrics()
        pad = 2
        cell_h = asc + desc + 2 * pad
        cell_w = int(size * 1.25) + 2 * pad
        cols = 16
        rows = int(math.ceil(len(chars) / cols))
        atlas = Image.new("L", (cols * cell_w, rows * cell_h), 0)
        d = ImageDraw.Draw(atlas)
        adv = []
        for i, ch in enumerate(chars):
            cx = (i % cols) * cell_w; cy = (i // cols) * cell_h
            d.text((cx + pad, cy + pad), ch, font=f, fill=255)
            adv.append(f.getlength(ch))
        data = np.asarray(atlas, dtype=np.uint8).flatten()
        out.append("static const float FONT%d_ADV[%d] = {%s};" % (k, nch, ",".join("%.2f" % a for a in adv)))
        out.append("static const unsigned char FONT%d_PIX[%d] = {" % (k, data.size))
        for i in range(0, data.size, 40):
            out.append(",".join(str(int(v)) for v in data[i:i + 40]) + ",")
        out.append("};")
        out.append("")
        rows_out.append("    { %d, %d, %d, 16, %d, %d, %d, %d, FONT%d_ADV, FONT%d_PIX }," % (size, cell_w, cell_h, pad, 16 * cell_w, rows * cell_h, nch, k, k))
    out.append("#define FONT_N %d" % len(specs))
    out.append("static const FontAtlas FONTS[FONT_N] = {")
    out += rows_out
    out.append("};")
    out.append("")
    out.append("#endif")
    open(P("src", name), "w").write("\n".join(out) + "\n")

# ---------------------------------------------------------------- main
def save_bmp(img, path):
    img.convert("RGB").save(path, format="BMP")
    print("wrote", os.path.relpath(path, ROOT), os.path.getsize(path) // 1024, "KB")

def main():
    os.makedirs(P("assets"), exist_ok=True); os.makedirs(P("sce_sys"), exist_ok=True); os.makedirs(P("src"), exist_ok=True)
    menu = make_menu_bg()
    save_bmp(menu, P("assets", "menu_bg.bmp"))
    save_bmp(make_game_bg(), P("assets", "game_bg.bmp"))
    splash = make_splash(menu)
    splash.save(P("sce_sys", "pic1.png"), optimize=True)
    make_icon().save(P("sce_sys", "icon0.png"), optimize=True)
    print("wrote sce_sys/pic1.png, sce_sys/icon0.png")

    src = P("assets_src", "credit_MexrlDev.jpg")
    if os.path.exists(src):
        im = Image.open(src).convert("RGB")
        w = 560; h = int(im.height * w / im.width)
        save_bmp(im.resize((w, h), Image.LANCZOS), P("assets", "credits.bmp"))
    else:
        print("WARNING: assets_src/credit_MexrlDev.jpg missing")

    make_font_header(); make_font_header(FONT_SPECS_540, "font_data_540.h"); print("wrote src/font_data.h and font_data_540.h")

    for src, dst, what in (("Menu_source.mp3", "menu_music.mp3", "menu"), ("Game_source.mp3", "game_music.mp3", "game")):
        song = P("assets_src", src)
        if os.path.exists(song):
            subprocess.check_call(["ffmpeg", "-y", "-loglevel", "error", "-i", song, "-vn", "-map_metadata", "-1",
                                   "-ar", "48000", "-ac", "2", "-c:a", "libmp3lame", "-b:a", "160k", P("assets", dst)])
            print("wrote assets/" + dst, os.path.getsize(P("assets", dst)) // 1024, "KB")
        else:
            print("WARNING: " + what + " song source missing, it will be silent")

if __name__ == "__main__":
    main()
