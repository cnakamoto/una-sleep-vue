#!/usr/bin/env python3
"""Generate the SleepAnalytics app icons (Zz glyph, diagonally offset).

Renders Resources/icon_60x60.png and Resources/icon_30x30.png using the
project's Poppins-SemiBold. Run with the project .venv python (Pillow).
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT = "SleepAnalytics/Software/Apps/TouchGFX-GUI/assets/fonts/Poppins-SemiBold.ttf"
OUT_DIR = "SleepAnalytics/Resources"

# Soft blue-white — reads clearly on the watch's dark menu, sleep-flavored
INK = (205, 220, 255, 255)


def render(size: int, big: int, small: int, big_xy, small_xy) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    f_big = ImageFont.truetype(FONT, big)
    f_small = ImageFont.truetype(FONT, small)
    # anchor="mm": x,y is the glyph's optical center
    d.text(big_xy, "Z", font=f_big, fill=INK, anchor="mm")
    d.text(small_xy, "Z", font=f_small, fill=INK, anchor="mm")
    return img


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # 60x60: big Z lower-left, small Z upper-right (diagonal ascent)
    render(60, big=34, small=20, big_xy=(21, 35), small_xy=(43, 17)) \
        .save(f"{OUT_DIR}/icon_60x60.png")

    # 30x30: same composition, tighter
    render(30, big=17, small=10, big_xy=(10, 18), small_xy=(21, 9)) \
        .save(f"{OUT_DIR}/icon_30x30.png")

    print("wrote", f"{OUT_DIR}/icon_60x60.png", "and", f"{OUT_DIR}/icon_30x30.png")


if __name__ == "__main__":
    main()
