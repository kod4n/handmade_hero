"""Generates the sample bitmaps in data/.

    python tools/make_bitmaps.py

Writes 32-bit BI_BITFIELDS BMPs whose channel masks are 0xAARRGGBB, which is
what load_bmp in src/game.cpp assumes. Alpha is straight (not premultiplied).

  background.bmp  960x540  top-down grass field, opaque
  hero_front.bmp  48x64    adventurer sprite, one per facing, with transparent
  hero_back.bmp            surround and a soft drop shadow; feet touch y=60,
  hero_left.bmp            centered at x=24
  hero_right.bmp
"""

import math
import random
import struct
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent.parent / "data"


def write_bmp(path, width, height, pixels):
    """pixels: top-down rows of (r, g, b, a) tuples, 0-255."""
    info_size = 56  # BITMAPV3INFOHEADER: BITMAPINFOHEADER + RGBA masks
    offset = 14 + info_size
    image_size = width * height * 4
    out = bytearray()
    out += struct.pack("<2sIHHI", b"BM", offset + image_size, 0, 0, offset)
    out += struct.pack(
        "<IiiHHIIiiIIIIII",
        info_size,
        width,
        height,  # positive: rows stored bottom-up
        1,
        32,
        3,  # BI_BITFIELDS
        image_size,
        2835,
        2835,
        0,
        0,
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000,
    )
    for row in reversed(pixels):
        for r, g, b, a in row:
            out += bytes((b, g, r, a))
    path.write_bytes(out)
    print(f"wrote {path} ({width}x{height})")


def clamp8(v):
    return max(0, min(255, int(round(v))))


# ---- background -----------------------------------------------------------


def value_noise(width, height, cell, rng):
    """Smoothly interpolated random grid, values in [0, 1)."""
    gw = width // cell + 2
    gh = height // cell + 2
    grid = [[rng.random() for _ in range(gw)] for _ in range(gh)]
    out = []
    for y in range(height):
        gy, fy = divmod(y / cell, 1.0)
        gy = int(gy)
        ty = fy * fy * (3 - 2 * fy)
        row = []
        for x in range(width):
            gx, fx = divmod(x / cell, 1.0)
            gx = int(gx)
            tx = fx * fx * (3 - 2 * fx)
            top = grid[gy][gx] + (grid[gy][gx + 1] - grid[gy][gx]) * tx
            bot = grid[gy + 1][gx] + (grid[gy + 1][gx + 1] - grid[gy + 1][gx]) * tx
            row.append(top + (bot - top) * ty)
        out.append(row)
    return out


def make_background():
    width, height = 960, 540
    rng = random.Random(1234)
    coarse = value_noise(width, height, 96, rng)
    fine = value_noise(width, height, 12, rng)

    base = (78, 140, 62)
    pixels = []
    for y in range(height):
        row = []
        for x in range(width):
            n = 0.65 * coarse[y][x] + 0.35 * fine[y][x]  # [0, 1)
            shade = 0.75 + 0.45 * n
            # Per-pixel grain so it doesn't look airbrushed.
            shade += (rng.random() - 0.5) * 0.08
            row.append([base[0] * shade, base[1] * shade, base[2] * shade])
        pixels.append(row)

    def put(px, py, color, alpha=1.0):
        if 0 <= px < width and 0 <= py < height:
            p = pixels[py][px]
            for i in range(3):
                p[i] = p[i] * (1 - alpha) + color[i] * alpha

    # Grass blades: short vertical strokes, darker at the root.
    for _ in range(9000):
        bx, by = rng.randrange(width), rng.randrange(height)
        length = rng.randint(2, 5)
        light = rng.random() < 0.4
        for i in range(length):
            color = (120, 186, 88) if light else (44, 92, 40)
            put(bx, by - i, color, 0.55)

    # Pebbles: small grey blobs with a highlight.
    for _ in range(140):
        cx, cy = rng.randrange(width), rng.randrange(height)
        r = rng.uniform(1.5, 3.5)
        grey = rng.randint(120, 165)
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                d = math.hypot(dx, dy * 1.3)
                if d <= r:
                    put(cx + dx, cy + dy, (grey, grey - 4, grey - 12), 0.9)
        put(cx - 1, cy - 1, (220, 220, 210), 0.7)

    # Flower clusters: a few petals around a yellow center.
    petal_colors = [(240, 240, 235), (235, 120, 160), (150, 140, 230), (245, 200, 80)]
    for _ in range(70):
        cx, cy = rng.randrange(width), rng.randrange(height)
        color = rng.choice(petal_colors)
        for _ in range(rng.randint(3, 7)):
            fx = cx + rng.randint(-10, 10)
            fy = cy + rng.randint(-8, 8)
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                put(fx + dx, fy + dy, color)
            put(fx, fy, (250, 210, 60))

    # Soft vignette to frame the play area.
    cx, cy = width / 2, height / 2
    max_d = math.hypot(cx, cy)
    out = []
    for y in range(height):
        row = []
        for x in range(width):
            d = math.hypot(x - cx, y - cy) / max_d
            v = 1.0 - 0.35 * d * d
            r, g, b = pixels[y][x]
            row.append((clamp8(r * v), clamp8(g * v), clamp8(b * v), 255))
        out.append(row)
    write_bmp(DATA_DIR / "background.bmp", width, height, out)


# ---- hero -----------------------------------------------------------------

# Each facing is a 16x20 grid. Front and back are symmetric, so only their
# left halves are written out and mirrored; left is right mirrored.
HERO_FRONT_LEFT_HALF = [
    ".....KKK",
    "....KHHH",
    "...KHHHH",
    "..KHHhSS",
    "..KHhSSS",
    "..KHhSES",
    "..KHhSSS",
    "...KHhSS",
    "..KKHHCC",
    ".KHHCCCC",
    ".KHCCCCC",
    "KHhCCcCC",
    "KHhCcCCC",
    "KSKBBBBY",
    ".KhCcCCC",
    "..KPPPPP",
    "..KPPPK.",
    "..KPPPK.",
    "..KOOOK.",
    "..KKKKK.",
]

# Hood covers the head, the cape tail hangs over the tunic, no buckle.
HERO_BACK_LEFT_HALF = [
    ".....KKK",
    "....KHHH",
    "...KHHHH",
    "..KHHhHH",
    "..KHhHHH",
    "..KHhHHH",
    "..KHhHHH",
    "...KHhHH",
    "..KKHHHH",
    ".KHHCHHH",
    ".KHCCHhH",
    "KHhCCcHh",
    "KHhCcCKH",
    "KSKBBBBB",
    ".KhCcCCC",
    "..KPPPPP",
    "..KPPPK.",
    "..KPPPK.",
    "..KOOOK.",
    "..KKKKK.",
]

# Profile facing right: hood at the back, face and boot toes at the front,
# the near arm hanging over the belt.
HERO_RIGHT = [
    "......KKKK......",
    ".....KHHHHK.....",
    "....KHHHHHHK....",
    "...KHHHHHSSSK...",
    "...KHhHHSSSSK...",
    "...KHhHHSSESK...",
    "...KHhHHSSSSK...",
    "....KHhHHSSK....",
    "....KKHHCCK.....",
    "...KHHCCCCCK....",
    "...KHCCCCCCK....",
    "...KhCCcCCCK....",
    "...KhCCcCCCK....",
    "...KBBBSBBBK....",
    "....KCcCCCK.....",
    "....KPPPPPK.....",
    "....KPPKPPK.....",
    "....KPPKPPK.....",
    "....KOOKOOOK....",
    "....KKKKKKKK....",
]

HERO_PALETTE = {
    "K": (27, 27, 42),     # outline
    "H": (42, 157, 143),   # hood
    "h": (31, 112, 102),   # hood shade
    "S": (242, 199, 155),  # skin
    "E": (27, 27, 42),     # eyes
    "C": (231, 111, 81),   # tunic
    "c": (181, 82, 59),    # tunic shade
    "B": (107, 66, 38),    # belt
    "Y": (244, 211, 94),   # buckle
    "P": (61, 64, 91),     # trousers
    "O": (74, 47, 27),     # boots
}


def mirror_halves(left_half):
    return [row + row[::-1] for row in left_half]


def make_hero(name, grid):
    assert len(grid) == 20 and all(len(row) == 16 for row in grid), name
    scale = 3
    sprite_w, sprite_h = len(grid[0]) * scale, len(grid) * scale  # 48x60
    width, height = sprite_w, sprite_h + 4  # room for the shadow below the feet
    feet_y = sprite_h

    pixels = [[(0, 0, 0, 0) for _ in range(width)] for _ in range(height)]

    # Drop shadow: soft black ellipse centered on the feet, alpha falls off
    # toward the edge so it exercises partial blending.
    cx, rx, ry = width / 2, 16.0, 4.0
    for y in range(height):
        for x in range(width):
            d = math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - feet_y) / ry)
            if d < 1.0:
                pixels[y][x] = (0, 0, 0, clamp8(110 * (1.0 - d * d)))

    for gy, line in enumerate(grid):
        for gx, ch in enumerate(line):
            if ch == ".":
                continue
            r, g, b = HERO_PALETTE[ch]
            for sy in range(scale):
                for sx in range(scale):
                    pixels[gy * scale + sy][gx * scale + sx] = (r, g, b, 255)

    write_bmp(DATA_DIR / f"{name}.bmp", width, height, pixels)


if __name__ == "__main__":
    make_background()
    make_hero("hero_front", mirror_halves(HERO_FRONT_LEFT_HALF))
    make_hero("hero_back", mirror_halves(HERO_BACK_LEFT_HALF))
    make_hero("hero_right", HERO_RIGHT)
    make_hero("hero_left", [row[::-1] for row in HERO_RIGHT])
