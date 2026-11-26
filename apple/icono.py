#!/usr/bin/env python3
"""Dibuja el icono de HieroOCR para macOS e iOS (el anj dorado sobre azul, como
el de Android) a 1024 píxeles:
  icono-macos.png  cuadrado redondeado con margen (como los iconos de macOS)
  icono-ios.png    a sangre y sin transparencia (iOS le pone las esquinas)
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BLUE, GOLD = (28, 51, 110, 255), (236, 190, 72, 255)


def ankh(d, cx, top, size, width):
    # el lazo (un óvalo), el travesaño y el mástil
    lw, lh = size * 0.34, size * 0.40
    d.ellipse([cx - lw / 2, top, cx + lw / 2, top + lh], outline=GOLD, width=width)
    bar = top + lh + width * 0.2
    d.line([cx - size * 0.30, bar, cx + size * 0.30, bar], fill=GOLD, width=width)
    d.line([cx, top + lh - width * 0.5, cx, top + size], fill=GOLD, width=width)


def draw(full_bleed):
    s = 4096
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0) if not full_bleed else BLUE)
    d = ImageDraw.Draw(im)
    if not full_bleed:
        m = int(s * 0.098)   # la rejilla de iconos de macOS: 824 de 1024
        d.rounded_rectangle([m, m, s - m, s - m], radius=int(s * 0.18), fill=BLUE)
    size = s * (0.62 if full_bleed else 0.54)
    ankh(d, s / 2, (s - size) / 2, size, int(s * 0.026))
    return im.resize((1024, 1024), Image.LANCZOS)


draw(False).save(os.path.join(HERE, "icono-macos.png"))
img = draw(True).convert("RGB")
img.save(os.path.join(HERE, "icono-ios.png"))
print("icono-macos.png, icono-ios.png")
