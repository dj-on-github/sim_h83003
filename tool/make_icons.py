#!/usr/bin/env python3
"""Generates the sim_h83003 application icon and every platform variant.

The design deliberately matches the sim_6502 icon so the two simulators
read as a family: a black DIP package with silver pins, a pin-1 dot and a
top notch, sitting on a vertical gradient, with the part number across the
body in heavy sans (zeros carry the same centre dot). The only intentional
difference is the background colour — blue here, matching the app's accent,
so the two are easy to tell apart in a dock or launcher.

Geometry below was measured off sim_6502's 1024px master so proportions,
the highlight band and the pin pitch line up. The chip body is wider than
the 6502's because "H8/3003" is seven glyphs rather than four; keeping the
letters near the original's cap height matters more for legibility at small
sizes than keeping the body width identical.

Run from the project root:  python3 tool/make_icons.py
"""

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Supersampling factor: everything is drawn this many times larger and then
# downsampled, which is what gives clean edges on the curves and lettering.
S = 4
SIZE = 1024

FONT_PATH = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
TEXT = "H8/3003"

# ---- Palette --------------------------------------------------------------
BG_TOP = (16, 84, 150)      # blue counterpart of the 6502 icon's green
BG_BOTTOM = (4, 33, 68)
BODY = (20, 22, 26)         # chip body, as measured off the 6502 icon
BAND = (44, 46, 49)         # lighter band across the upper third
NOTCH = (13, 14, 17)
PIN_TOP = (208, 214, 221)
PIN_BOTTOM = (150, 157, 167)
PIN1_RING = (96, 99, 104)
INK = (236, 240, 244)       # lettering

# ---- Geometry (in 1024px units, scaled by S when drawn) -------------------
CHIP_L, CHIP_R = 145, 879
CHIP_T, CHIP_B = 277, 747
CHIP_RADIUS = 30

BAND_FADE_IN = (277, 315)   # body -> band
BAND_HOLD_END = 415         # band holds to here
BAND_FADE_OUT = 450         # ...then back to body by here

NOTCH_R = 46
PIN1_OFFSET = (49, 58)      # from the chip's top-left corner
PIN1_R = 15

PIN_COUNT = 6
PIN_FIRST_TOP = 299
PIN_PITCH = 78
PIN_H = 33
PIN_OUT = 69                # how far a pin sticks out from the body
PIN_UNDER = 12              # how far it tucks under the body edge
PIN_RADIUS = 7

TEXT_CENTER_Y = 525
TEXT_MARGIN = 38            # clear space each side of the lettering
ZERO_DOT = 0.55             # centre dot side, as a fraction of the '0' counter


def px(v):
    """Scales a 1024-space coordinate into the supersampled canvas."""
    return int(round(v * S))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def vertical_gradient(size, top, bottom):
    """A one-pixel-wide gradient stretched to [size] — cheaper and smoother
    than filling row by row at supersampled resolution."""
    w, h = size
    strip = Image.new("RGB", (1, h))
    sp = strip.load()
    for y in range(h):
        sp[0, y] = lerp(top, bottom, y / max(1, h - 1))
    return strip.resize((w, h), Image.BILINEAR)


def chip_layer(w, h):
    """The chip body: solid [BODY] with the soft [BAND] highlight across its
    upper third, matching the fade in/out of the 6502 icon."""
    strip = Image.new("RGB", (1, h))
    sp = strip.load()
    for y in range(h):
        y1024 = CHIP_T + y / S
        if y1024 < BAND_FADE_IN[0]:
            c = BODY
        elif y1024 < BAND_FADE_IN[1]:
            t = (y1024 - BAND_FADE_IN[0]) / (BAND_FADE_IN[1] - BAND_FADE_IN[0])
            c = lerp(BODY, BAND, t)
        elif y1024 < BAND_HOLD_END:
            c = BAND
        elif y1024 < BAND_FADE_OUT:
            t = (y1024 - BAND_HOLD_END) / (BAND_FADE_OUT - BAND_HOLD_END)
            c = lerp(BAND, BODY, t)
        else:
            c = BODY
        sp[0, y] = c
    return strip.resize((w, h), Image.BILINEAR)


def rounded_mask(size, radius):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1],
                                        radius=radius, fill=255)
    return m


def draw_pins(canvas):
    """Six silver pins down each side, drawn before the body so their inner
    ends disappear under it."""
    pin_w = PIN_OUT + PIN_UNDER
    grad = vertical_gradient((px(pin_w), px(PIN_H)), PIN_TOP, PIN_BOTTOM)
    mask = rounded_mask(grad.size, px(PIN_RADIUS))
    for i in range(PIN_COUNT):
        top = PIN_FIRST_TOP + i * PIN_PITCH
        canvas.paste(grad, (px(CHIP_L - PIN_OUT), px(top)), mask)
        canvas.paste(grad, (px(CHIP_R - PIN_UNDER), px(top)), mask)


def fit_font(max_width, target_cap):
    """Picks the Arial Bold size whose cap height is [target_cap], backing off
    if that would make the string wider than [max_width]."""
    size = int(target_cap / 0.716)  # Arial's cap height as a fraction of em
    for _ in range(6):
        font = ImageFont.truetype(FONT_PATH, size)
        hb = font.getbbox("H")
        cap = hb[3] - hb[1]
        width = sum(font.getlength(ch) for ch in TEXT)
        if width > max_width:
            size = int(size * max_width / width)
            continue
        if abs(cap - target_cap) <= 2:
            break
        size = int(size * target_cap / cap)
    font = ImageFont.truetype(FONT_PATH, size)
    return font


def counter_box(font):
    """Measures the hole inside a '0' at this font size, so the centre dot can
    be sized to sit clear of the strokes (Arial Bold's counter is narrower
    than the 6502 icon's typeface, so a fixed fraction of cap height would
    bridge across to the glyph and read as a bar)."""
    b = font.getbbox("0")
    w, h = b[2] - b[0] + 4, b[3] - b[1] + 4
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).text((-b[0] + 2, -b[1] + 2), "0", font=font, fill=255)
    mp = m.load()
    midy, midx = h // 2, w // 2
    row = [x for x in range(w) if mp[x, midy] < 128 and
           any(mp[i, midy] > 128 for i in range(x)) and
           any(mp[i, midy] > 128 for i in range(x, w))]
    col = [y for y in range(h) if mp[midx, y] < 128 and
           any(mp[midx, i] > 128 for i in range(y)) and
           any(mp[midx, i] > 128 for i in range(y, h))]
    if not row or not col:
        return font.size * 0.12
    return min(max(row) - min(row), max(col) - min(col))


def draw_text(canvas):
    """Lays the part number out glyph by glyph so each zero can be given the
    centre dot the 6502 icon's zero has."""
    max_width = px(CHIP_R - CHIP_L - 2 * TEXT_MARGIN)
    font = fit_font(max_width, px(118))

    hb = font.getbbox("H")
    cap = hb[3] - hb[1]
    advances = [font.getlength(ch) for ch in TEXT]
    total = sum(advances)
    x = px((CHIP_L + CHIP_R) / 2) - total / 2
    baseline = px(TEXT_CENTER_Y) + cap / 2

    # A soft drop shadow under the lettering, as on the 6502 icon.
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    cx = x
    for ch, adv in zip(TEXT, advances):
        sd.text((cx + px(3), baseline + px(4)), ch, font=font,
                fill=(0, 0, 0, 130), anchor="ls")
        cx += adv
    shadow = shadow.filter(ImageFilter.GaussianBlur(px(2)))
    canvas.paste(shadow, (0, 0), shadow)

    d = ImageDraw.Draw(canvas)
    dot = counter_box(font) * ZERO_DOT
    cx = x
    for ch, adv in zip(TEXT, advances):
        d.text((cx, baseline), ch, font=font, fill=INK, anchor="ls")
        if ch == "0":
            # Centre the dot in the glyph's counter.
            b = font.getbbox(ch)
            gx = cx + (b[0] + b[2]) / 2
            gy = baseline - cap / 2
            d.rounded_rectangle(
                [gx - dot / 2, gy - dot / 2, gx + dot / 2, gy + dot / 2],
                radius=dot * 0.12, fill=INK)
        cx += adv


# The chip is drawn wider than the 6502's (seven glyphs, not four), so the
# finished artwork is scaled down slightly to leave the same margin around
# the pins as the 6502 icon has. Keeps the two the same visual weight in a
# dock or launcher.
ART_SCALE = 0.92
MASKABLE_SCALE = 0.76


def render(scale=ART_SCALE):
    """Draws the icon at 1024px. [scale] shrinks the artwork within the frame
    (maskable web icons need a wider safe margin)."""
    big = (SIZE * S, SIZE * S)
    canvas = vertical_gradient(big, BG_TOP, BG_BOTTOM).convert("RGB")

    art = Image.new("RGBA", big, (0, 0, 0, 0))

    # Chip shadow.
    shadow = Image.new("RGBA", big, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        [px(CHIP_L), px(CHIP_T + 10), px(CHIP_R), px(CHIP_B + 16)],
        radius=px(CHIP_RADIUS), fill=(0, 0, 0, 120))
    shadow = shadow.filter(ImageFilter.GaussianBlur(px(9)))
    art.paste(shadow, (0, 0), shadow)

    draw_pins(art)

    # Chip body with its highlight band.
    body_size = (px(CHIP_R - CHIP_L), px(CHIP_B - CHIP_T))
    body = chip_layer(*body_size)
    art.paste(body, (px(CHIP_L), px(CHIP_T)),
              rounded_mask(body_size, px(CHIP_RADIUS)))

    d = ImageDraw.Draw(art)
    # Package notch, centred on the top edge.
    ncx = px((CHIP_L + CHIP_R) / 2)
    d.pieslice([ncx - px(NOTCH_R), px(CHIP_T) - px(NOTCH_R),
                ncx + px(NOTCH_R), px(CHIP_T) + px(NOTCH_R)],
               start=0, end=180, fill=NOTCH, outline=(72, 74, 78),
               width=px(2))
    # Pin-1 dot.
    dx, dy = px(CHIP_L + PIN1_OFFSET[0]), px(CHIP_T + PIN1_OFFSET[1])
    d.ellipse([dx - px(PIN1_R), dy - px(PIN1_R),
               dx + px(PIN1_R), dy + px(PIN1_R)],
              outline=PIN1_RING, width=px(3))

    draw_text(art)

    if scale != 1.0:
        w = int(big[0] * scale)
        art = art.resize((w, w), Image.LANCZOS)
        off = (big[0] - w) // 2
        shifted = Image.new("RGBA", big, (0, 0, 0, 0))
        shifted.paste(art, (off, off), art)
        art = shifted

    canvas.paste(art, (0, 0), art)
    return canvas.resize((SIZE, SIZE), Image.LANCZOS)


def save(img, path, size):
    full = os.path.join(ROOT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    img.resize((size, size), Image.LANCZOS).save(full)
    print(f"  {path} ({size}x{size})")


def main():
    icon = render()
    maskable = render(scale=MASKABLE_SCALE)  # chip inside the safe zone

    print("master:")
    save(icon, "assets/icon/app_icon_1024.png", 1024)

    print("macOS:")
    for s in (16, 32, 64, 128, 256, 512, 1024):
        save(icon,
             f"macos/Runner/Assets.xcassets/AppIcon.appiconset/app_icon_{s}.png",
             s)

    print("iOS:")
    ios = {
        "Icon-App-20x20@1x.png": 20, "Icon-App-20x20@2x.png": 40,
        "Icon-App-20x20@3x.png": 60, "Icon-App-29x29@1x.png": 29,
        "Icon-App-29x29@2x.png": 58, "Icon-App-29x29@3x.png": 87,
        "Icon-App-40x40@1x.png": 40, "Icon-App-40x40@2x.png": 80,
        "Icon-App-40x40@3x.png": 120, "Icon-App-60x60@2x.png": 120,
        "Icon-App-60x60@3x.png": 180, "Icon-App-76x76@1x.png": 76,
        "Icon-App-76x76@2x.png": 152, "Icon-App-83.5x83.5@2x.png": 167,
        "Icon-App-1024x1024@1x.png": 1024,
    }
    for name, s in ios.items():
        save(icon, f"ios/Runner/Assets.xcassets/AppIcon.appiconset/{name}", s)

    print("Android:")
    for folder, s in (("mipmap-mdpi", 48), ("mipmap-hdpi", 72),
                      ("mipmap-xhdpi", 96), ("mipmap-xxhdpi", 144),
                      ("mipmap-xxxhdpi", 192)):
        save(icon, f"android/app/src/main/res/{folder}/ic_launcher.png", s)

    print("web:")
    save(icon, "web/favicon.png", 16)
    save(icon, "web/icons/Icon-192.png", 192)
    save(icon, "web/icons/Icon-512.png", 512)
    save(maskable, "web/icons/Icon-maskable-192.png", 192)
    save(maskable, "web/icons/Icon-maskable-512.png", 512)

    print("Windows:")
    ico = os.path.join(ROOT, "windows/runner/resources/app_icon.ico")
    icon.save(ico, format="ICO",
              sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128),
                     (256, 256)])
    print(f"  windows/runner/resources/app_icon.ico (multi-size)")


if __name__ == "__main__":
    main()
