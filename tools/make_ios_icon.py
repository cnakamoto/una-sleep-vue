#!/usr/bin/env python3
"""Generate the SleepVue iOS app icon (1024x1024).

Same "Zz" composition as the watch app icon (tools/make_icons.py):
big Z lower-left, small Z upper-right, Poppins-SemiBold, soft blue-white
ink. iOS icons are opaque full-bleed squares (transparency is flattened
and corners are masked by the system), so the glyph pair sits on a dark
navy field matching the app's dark theme.

Output: ios/SleepVueSync/Assets.xcassets/AppIcon.appiconset/icon_1024.png
Run with the project .venv python (Pillow):  .venv/bin/python tools/make_ios_icon.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT = "SleepVue/Software/Apps/TouchGFX-GUI/assets/fonts/Poppins-SemiBold.ttf"
OUT = "ios/SleepVueSync/Assets.xcassets/AppIcon.appiconset/icon_1024.png"

SIZE = 1024
INK = (205, 220, 255, 255)        # same as the watch icon
BG_TOP = (26, 26, 46)             # #1a1a2e — dark navy
BG_BOTTOM = (15, 15, 23)          # #0f0f17 — matches the app's #101018

# Watch-icon composition (60px design) scaled to 1024.
S = SIZE / 60
BIG = dict(font=round(34 * S), xy=(round(21 * S), round(35 * S)))
SMALL = dict(font=round(20 * S), xy=(round(43 * S), round(17 * S)))


def background() -> Image.Image:
    """Subtle vertical gradient BG_TOP -> BG_BOTTOM."""
    img = Image.new("RGBA", (SIZE, SIZE))
    d = ImageDraw.Draw(img)
    for y in range(SIZE):
        t = y / (SIZE - 1)
        d.line([(0, y), (SIZE, y)], fill=tuple(
            round(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3)
        ) + (255,))
    return img


def main():
    img = background()
    d = ImageDraw.Draw(img)
    # anchor="mm": x,y is the glyph's optical center (same as make_icons.py)
    d.text(BIG["xy"], "Z", font=ImageFont.truetype(FONT, BIG["font"]), fill=INK, anchor="mm")
    d.text(SMALL["xy"], "Z", font=ImageFont.truetype(FONT, SMALL["font"]), fill=INK, anchor="mm")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.convert("RGB").save(OUT)  # iOS icons: no alpha
    print("wrote", OUT)


if __name__ == "__main__":
    main()
