"""Generate the CrosEC Control application icon. Requires Pillow."""
from PIL import Image, ImageDraw, ImageFilter
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets"
OUT.mkdir(exist_ok=True)
S = 1024

# Transparent canvas and a softly rounded Windows-style tile.
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
shadow = Image.new("RGBA", im.size, (0, 0, 0, 0))
sd = ImageDraw.Draw(shadow)
sd.rounded_rectangle((72, 82, 952, 962), radius=224, fill=(0, 0, 0, 150))
shadow = shadow.filter(ImageFilter.GaussianBlur(38))
im.alpha_composite(shadow)

mask = Image.new("L", im.size, 0)
ImageDraw.Draw(mask).rounded_rectangle((60, 54, 964, 958), radius=224, fill=255)
bg = Image.new("RGBA", im.size)
p = bg.load()
for y in range(S):
    t = max(0.0, min(1.0, (y - 54) / 904))
    for x in range(S):
        # Subtle blue cast toward the upper-left.
        glow = max(0.0, 1.0 - math.hypot(x - 250, y - 180) / 800)
        p[x, y] = (int(18 + 7 * glow), int(25 + 14 * glow), int(38 + 24 * glow - 5*t), 255)
im.alpha_composite(Image.composite(bg, Image.new("RGBA", im.size), mask))

d = ImageDraw.Draw(im)
# Fine tile rim.
d.rounded_rectangle((62, 56, 962, 956), radius=220, outline=(105, 171, 255, 105), width=12)
d.rounded_rectangle((79, 73, 945, 939), radius=205, outline=(255, 255, 255, 25), width=4)

cx, cy = 512, 500
# Blue halo around the EC silicon emblem.
halo = Image.new("RGBA", im.size, (0, 0, 0, 0))
hd = ImageDraw.Draw(halo)
hd.ellipse((214, 202, 810, 798), fill=(25, 119, 255, 100))
halo = halo.filter(ImageFilter.GaussianBlur(75))
im.alpha_composite(halo)
d = ImageDraw.Draw(im)

# Chip pins: eight sturdy traces behind the package.
pin_color = (89, 177, 255, 255)
for angle in range(0, 360, 45):
    a = math.radians(angle)
    r1, r2 = 318, 390
    x1, y1 = cx + math.cos(a)*r1, cy + math.sin(a)*r1
    x2, y2 = cx + math.cos(a)*r2, cy + math.sin(a)*r2
    d.line((x1, y1, x2, y2), fill=pin_color, width=32)
    r = 19
    d.rounded_rectangle((x2-r, y2-r, x2+r, y2+r), radius=8, fill=(139, 207, 255, 255))

# Hexagonal EC package.
def hex_points(radius, rotation=-30):
    return [(cx + math.cos(math.radians(rotation+i*60))*radius,
             cy + math.sin(math.radians(rotation+i*60))*radius) for i in range(6)]

d.polygon(hex_points(326), fill=(13, 74, 151, 255))
d.line(hex_points(326) + [hex_points(326)[0]], fill=(112, 203, 255, 255), width=30, joint="curve")
d.polygon(hex_points(276), fill=(20, 38, 60, 255))
d.line(hex_points(276) + [hex_points(276)[0]], fill=(42, 124, 228, 255), width=12, joint="curve")

# Fan symbol, three rounded blades around a bright central bearing.
fan = Image.new("RGBA", im.size, (0, 0, 0, 0))
fd = ImageDraw.Draw(fan)
# One tapered blade points upward, then rotate it twice.
blade = Image.new("RGBA", im.size, (0, 0, 0, 0))
bd = ImageDraw.Draw(blade)
bd.ellipse((445, 282, 579, 505), fill=(232, 247, 255, 255))
bd.polygon([(451, 430), (512, 500), (576, 420), (558, 333), (478, 342)], fill=(232, 247, 255, 255))
for angle in (0, 120, 240):
    fan.alpha_composite(blade.rotate(angle, center=(cx, cy), resample=Image.Resampling.BICUBIC))
fd = ImageDraw.Draw(fan)
fd.ellipse((430, 418, 594, 582), fill=(17, 74, 137, 255), outline=(143, 218, 255, 255), width=18)
fd.ellipse((476, 464, 548, 536), fill=(240, 250, 255, 255))
im.alpha_composite(fan)

# Tiny status pulse provides a distinct lower-right accent at taskbar sizes.
d = ImageDraw.Draw(im)
d.ellipse((720, 704, 855, 839), fill=(20, 34, 48, 255), outline=(118, 206, 255, 255), width=14)
d.ellipse((755, 739, 820, 804), fill=(58, 220, 151, 255))

# Master PNG and a Windows ICO containing every common shell size.
png = OUT / "CrosECControl.png"
ico = OUT / "CrosECControl.ico"
im.save(png, optimize=True)
im.save(ico, format="ICO", sizes=[(16,16), (20,20), (24,24), (32,32), (40,40), (48,48), (64,64), (96,96), (128,128), (256,256)])
print(png)
print(ico)
