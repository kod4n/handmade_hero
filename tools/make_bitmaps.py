"""Generates the sample bitmaps in data/.

    python tools/make_bitmaps.py

Writes 32-bit BI_BITFIELDS BMPs whose channel masks are 0xAARRGGBB, which is
what load_bmp in src/game.cpp assumes. Alpha is straight (not premultiplied).

  background.bmp  960x540  top-down grass field, opaque
  hero_front.bmp  48x64    adventurer sprite, one per facing, with transparent
  hero_back.bmp            surround and a soft drop shadow; feet touch y=60,
  hero_left.bmp            centered at x=24
  hero_right.bmp

  ground_sand_00.bmp   128x128  ground patches: irregular blobs with soft
  ground_sand_01.bmp            alpha edges and a transparent surround, to
  ground_rocks_00.bmp           stamp over the grass and over each other
  ground_rocks_01.bmp           in any order
  ground_grass_00.bmp
  ground_grass_01.bmp
  tuft_00.bmp     32x28    grass tufts (short, lush, dry); root at x=16, y=24
  tuft_01.bmp
  tuft_02.bmp
  tree_00.bmp     96x128   trees for room borders (round, apple, pine) with a
  tree_01.bmp              drop shadow; trunk base at x=48, y=120
  tree_02.bmp

Every bitmap comes from a fixed seed, so rerunning reproduces the same files.
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


# ---- shared helpers for the scenery below ----------------------------------

# Light comes from the top left and slightly toward the viewer, matching the
# highlights on the background pebbles. Image y points down.
LIGHT = (-0.45, -0.55, 0.70)


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def lerp3(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(3)]


def lit(nx, ny, nz):
    """Diffuse term for a unit normal, in [0, 1]."""
    return max(0.0, nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2])


def new_layer(width, height):
    """Rows of [r, g, b, a]: rgb 0-255 floats, a 0-1, straight alpha."""
    return [[[0.0, 0.0, 0.0, 0.0] for _ in range(width)] for _ in range(height)]


def over(layer, x, y, rgb, a):
    """Composites rgb at coverage a over the pixel (straight alpha "over")."""
    if a <= 0.0 or not (0 <= y < len(layer) and 0 <= x < len(layer[0])):
        return
    a = min(a, 1.0)
    p = layer[y][x]
    out_a = a + p[3] * (1.0 - a)
    for i in range(3):
        p[i] = (rgb[i] * a + p[i] * p[3] * (1.0 - a)) / out_a
    p[3] = out_a


def layer_pixels(layer):
    """Fully transparent pixels are written as 0, not as whatever color is
    under them, so viewers that ignore BMP alpha still show the shape."""
    out = []
    for row in layer:
        out_row = []
        for r, g, b, a in row:
            a8 = clamp8(a * 255)
            out_row.append((clamp8(r), clamp8(g), clamp8(b), a8) if a8 else (0, 0, 0, 0))
        out.append(out_row)
    return out


def shadow_ellipse(layer, cx, cy, rx, ry, strength):
    """Soft drop shadow, darkest at the center, like the hero's."""
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            d = math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry)
            if d < 1.0:
                over(layer, x, y, (0, 0, 0), strength * (1.0 - d * d))


def stone(layer, cx, cy, rx, ry, color, rng):
    """A lit, antialiased pebble with a small shadow down and to the right."""
    shadow_ellipse(layer, cx + 0.6 * rx, cy + 0.5 * ry, rx, 0.8 * ry, 0.35)
    tint = rng.uniform(0.92, 1.08)
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            u = (x + 0.5 - cx) / rx
            v = (y + 0.5 - cy) / ry
            d = math.hypot(u, v)
            coverage = (1.0 - d) * min(rx, ry) + 0.5
            if coverage <= 0.0:
                continue
            nz = math.sqrt(max(0.0, 1.0 - min(d, 1.0) ** 2))
            shade = tint * (0.45 + 0.75 * lit(u, v, nz)) + (rng.random() - 0.5) * 0.08
            over(layer, x, y, [c * shade for c in color], coverage)


# ---- ground patches -------------------------------------------------------
#
# Irregular blobs with a soft, ragged alpha edge and a transparent surround,
# so they can be stamped over the base grass and over each other in any
# order and still blend. Two variants per material avoid obvious repeats.

PATCH_SIZE = 128  # a bit over 2 tiles (60 px each)


def blob_mask(size, rng):
    """Coverage in [0, 1] for a roughly circular blob: 1 in the middle, fading
    to exactly 0 before the square's border, so the corners are empty."""
    edge = value_noise(size, size, 10, rng)
    # Small wobbles keep it from being a perfect circle without making it lumpy.
    harmonics = [(k, rng.uniform(0.01, 0.03), rng.uniform(0.0, 2 * math.pi)) for k in (2, 3, 5)]
    c = size / 2
    # Max radius 0.42 * 1.09 * size ~= 0.46 * size, inside the 0.5 border
    # even after the edge noise pulls the fade outward.
    base_r = 0.42 * size
    mask = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x + 0.5 - c, y + 0.5 - c
            theta = math.atan2(dy, dx)
            r = base_r * (1.0 + sum(a * math.sin(k * theta + p) for k, a, p in harmonics))
            d = math.hypot(dx, dy) / r + (edge[y][x] - 0.5) * 0.12
            row.append(1.0 - smoothstep(0.75, 1.0, d))
        mask.append(row)
    return mask


def shaded_base(size, rng, base, coarse_cell, fine_cell, lo, span, grain):
    coarse = value_noise(size, size, coarse_cell, rng)
    fine = value_noise(size, size, fine_cell, rng)
    layer = new_layer(size, size)
    for y in range(size):
        for x in range(size):
            n = 0.65 * coarse[y][x] + 0.35 * fine[y][x]
            shade = lo + span * n + (rng.random() - 0.5) * grain
            layer[y][x] = [base[0] * shade, base[1] * shade, base[2] * shade, 1.0]
    return layer, coarse


def sand_texture(size, rng):
    layer, coarse = shaded_base(size, rng, (216, 188, 134), 32, 6, 0.86, 0.20, 0.08)
    for y in range(size):
        for x in range(size):
            # Faint wind ripples, bent by the coarse noise so they aren't straight.
            ripple = math.sin(0.8 * y + 0.25 * x + 9.0 * coarse[y][x])
            p = layer[y][x]
            for i in range(3):
                p[i] *= 1.0 + 0.025 * ripple
    for _ in range(size * size // 30):
        light = rng.random() < 0.5
        color = (245, 228, 190) if light else (150, 120, 80)
        over(layer, rng.randrange(size), rng.randrange(size), color, 0.5)
    return layer


def grass_texture(size, rng):
    # Same base color and blades as background.bmp, so it reads as thicker,
    # brighter turf on top of the field rather than a different plant.
    layer, _ = shaded_base(size, rng, (100, 170, 70), 40, 8, 0.82, 0.40, 0.10)
    for _ in range(size * size // 30):
        bx, by = rng.randrange(size), rng.randrange(size)
        light = rng.random() < 0.45
        color = (128, 196, 92) if light else (44, 92, 40)
        for i in range(rng.randint(2, 5)):
            over(layer, bx, by - i, color, 0.6)
    return layer


def rocks_texture(size, rng):
    # Packed dirt covered in gravel, with a few bigger stones on top.
    layer, _ = shaded_base(size, rng, (122, 106, 86), 24, 5, 0.80, 0.35, 0.10)
    greys = [(150, 146, 138), (128, 122, 114), (170, 162, 148), (112, 108, 104)]
    for _ in range(size * size // 70):
        r = rng.uniform(1.0, 2.4)
        stone(layer, rng.uniform(0, size), rng.uniform(0, size), r, r * 0.8, rng.choice(greys), rng)
    c = size / 2
    for _ in range(rng.randint(5, 8)):
        # Big stones stay near the middle, where the patch is opaque.
        angle, dist = rng.uniform(0, 2 * math.pi), rng.uniform(0, 0.25 * size)
        r = rng.uniform(4.0, 8.0)
        cx, cy = c + math.cos(angle) * dist, c + math.sin(angle) * dist
        stone(layer, cx, cy, r, r * rng.uniform(0.6, 0.85), rng.choice(greys), rng)
    return layer


def make_patch(name, seed, texture):
    rng = random.Random(seed)
    mask = blob_mask(PATCH_SIZE, rng)
    layer = texture(PATCH_SIZE, rng)
    for y in range(PATCH_SIZE):
        for x in range(PATCH_SIZE):
            layer[y][x][3] *= mask[y][x]
    write_bmp(DATA_DIR / f"{name}.bmp", PATCH_SIZE, PATCH_SIZE, layer_pixels(layer))


# ---- grass tufts ----------------------------------------------------------
#
# Small sprites to scatter over the ground. The tuft's root is at the
# bottom center: x = 16, y = 24 (TUFT_BASE_Y). The rows below are shadow.

TUFT_W, TUFT_H, TUFT_BASE_Y = 32, 28, 24


def blade(layer, x, y, angle, curve, length, width, root, tip):
    steps = max(8, int(length * 2))
    for s in range(steps + 1):
        t = s / steps
        a = angle + curve * t
        w = width * (1.0 - t) + 0.6
        color = lerp3(root, tip, t)
        for py in range(int(y - w) - 1, int(y + w) + 2):
            for px in range(int(x - w) - 1, int(x + w) + 2):
                cov = 0.5 * w - math.hypot(px + 0.5 - x, py + 0.5 - y) + 0.5
                if cov > 0.0:
                    over(layer, px, py, color, min(cov, 1.0))
        x += math.sin(a) * length / steps
        y -= math.cos(a) * length / steps


def make_tuft(name, seed, blades, height, root, tip):
    rng = random.Random(seed)
    layer = new_layer(TUFT_W, TUFT_H)
    cx = TUFT_W / 2
    shadow_ellipse(layer, cx, TUFT_BASE_Y, 10.0, 3.0, 0.35)
    # Outer blades first, so the upright middle ones overlap them.
    roots = sorted((rng.uniform(-6.0, 6.0) for _ in range(blades)), key=lambda o: -abs(o))
    for offset in roots:
        lean = offset / 6.0
        angle = 0.75 * lean + rng.uniform(-0.25, 0.25)
        curve = 0.5 * lean * rng.uniform(0.3, 1.0)
        length = height * rng.uniform(0.55, 1.0) * (1.0 - 0.3 * abs(lean))
        shade = rng.uniform(0.8, 1.1)
        blade(
            layer,
            cx + offset,
            TUFT_BASE_Y,
            angle,
            curve,
            length,
            rng.uniform(1.6, 2.4),
            [c * shade for c in root],
            [c * shade for c in tip],
        )
    write_bmp(DATA_DIR / f"{name}.bmp", TUFT_W, TUFT_H, layer_pixels(layer))


# ---- trees ----------------------------------------------------------------
#
# Sprites for lining the room borders. The trunk's base is at the bottom
# center: x = 48, y = 120 (TREE_BASE_Y), like the hero's feet at y = 60.
# Each tree is about 1.5 tiles wide.

TREE_W, TREE_H, TREE_BASE_Y = 96, 128, 120
BARK = (112, 78, 50)


def trunk(layer, cx, top, bottom, top_w, base_w, rng):
    grain = [rng.uniform(0, 2 * math.pi) for _ in range(TREE_W)]
    for y in range(int(top), int(bottom) + 1):
        t = (y - top) / (bottom - top)
        hw = 0.5 * (top_w + (base_w - top_w) * smoothstep(0.65, 1.0, t))  # root flare
        for x in range(int(cx - hw) - 1, int(cx + hw) + 2):
            u = (x + 0.5 - cx) / hw
            cov = (1.0 - abs(u)) * hw + 0.5
            if cov <= 0.0:
                continue
            nz = math.sqrt(max(0.0, 1.0 - min(abs(u), 1.0) ** 2))
            shade = 0.35 + 0.8 * lit(u, 0.0, nz)
            shade *= 0.88 + 0.12 * math.sin(1.3 * x + 0.12 * y + grain[x % TREE_W])
            if cov < 1.6:
                shade *= 0.6  # outline
            over(layer, x, y, [c * shade for c in BARK], min(cov, 1.0))


def canopy(layer, blobs, leaf, rng, fruit=None):
    """blobs: (cx, cy, r) circles; their union is the canopy."""
    leaves = value_noise(TREE_W, TREE_H, 3, rng)
    gx = sum(b[0] for b in blobs) / len(blobs)
    gy = sum(b[1] for b in blobs) / len(blobs)
    gr = max(math.hypot(b[0] - gx, b[1] - gy) + b[2] for b in blobs)
    for y in range(TREE_H):
        for x in range(TREE_W):
            px, py = x + 0.5, y + 0.5
            # Signed distance into the union, and the clump on top here.
            sd, best = -1e9, None
            for cx, cy, r in blobs:
                inside = r - math.hypot(px - cx, py - cy)
                if inside > sd:
                    sd, best = inside, (cx, cy, r)
            if sd + 0.5 <= 0.0:
                continue
            cx, cy, r = best
            # Each clump is a bump on one big dome: mix both normals.
            ux, uy = (px - cx) / r, (py - cy) / r
            vx, vy = (px - gx) / gr, (py - gy) / gr
            nx, ny = 0.55 * ux + 0.45 * vx, 0.55 * uy + 0.45 * vy
            nz = math.sqrt(max(0.0, 1.0 - min(1.0, nx * nx + ny * ny)))
            shade = 0.35 + 0.85 * lit(nx, ny, nz)
            shade *= 0.8 + 0.35 * leaves[y][x]
            shade *= 0.7 + 0.3 * min(1.0, sd / (0.35 * r))  # darker between clumps
            if sd < 1.6:
                shade *= 0.55  # outline
            over(layer, x, y, [c * shade for c in leaf], min(sd + 0.5, 1.0))
    for fx, fy in fruit or []:
        stone(layer, fx, fy, 2.2, 2.2, (200, 48, 40), rng)


def ring_blobs(cx, cy, count, ring_r, blob_r, rng):
    blobs = [(cx, cy, ring_r + 0.4 * blob_r)]
    for i in range(count):
        a = 2 * math.pi * i / count + rng.uniform(-0.25, 0.25)
        d = ring_r * rng.uniform(0.85, 1.1)
        blobs.append((cx + math.cos(a) * d, cy + 0.85 * math.sin(a) * d, blob_r * rng.uniform(0.85, 1.15)))
    return blobs


def make_round_tree(name, seed, leaf, ring_r, blob_r, fruit_count=0):
    rng = random.Random(seed)
    layer = new_layer(TREE_W, TREE_H)
    cx = TREE_W / 2
    shadow_ellipse(layer, cx + 4, TREE_BASE_Y, 34.0, 8.0, 0.45)
    trunk(layer, cx, 52, TREE_BASE_Y, 12, 20, rng)
    blobs = ring_blobs(cx, 48, 7, ring_r, blob_r, rng)
    fruit = []
    while len(fruit) < fruit_count:
        fx, fy = rng.uniform(20, 76), rng.uniform(22, 74)
        if any(math.hypot(fx - bx, fy - by) < br - 4 for bx, by, br in blobs):
            fruit.append((fx, fy))
    canopy(layer, blobs, leaf, rng, fruit)
    write_bmp(DATA_DIR / f"{name}.bmp", TREE_W, TREE_H, layer_pixels(layer))


def make_pine(name, seed, leaf):
    rng = random.Random(seed)
    layer = new_layer(TREE_W, TREE_H)
    cx = TREE_W / 2
    shadow_ellipse(layer, cx + 4, TREE_BASE_Y, 26.0, 7.0, 0.45)
    trunk(layer, cx, 90, TREE_BASE_Y, 9, 15, rng)
    needles = value_noise(TREE_W, TREE_H, 3, rng)
    tiers = [(6 + 20 * k, 6 + 20 * k + 38, 15 + 9 * k) for k in range(4)]  # apex, base, half width
    for apex, base, hw in reversed(tiers):  # bottom tier first; upper ones overlap it
        phase = rng.uniform(0, 2 * math.pi)
        for y in range(apex, base + 1):
            for x in range(int(cx - hw) - 1, int(cx + hw) + 2):
                px, py = x + 0.5, y + 0.5
                t = (py - apex) / (base - apex)
                half = hw * t
                hem = base - 3.0 * abs(math.sin(0.55 * px + phase))  # ragged bottom edge
                sd = min(half - abs(px - cx), hem - py)
                if sd + 0.5 <= 0.0:
                    continue
                u = (px - cx) / max(half, 1.0)
                shade = 0.95 - 0.3 * u  # light from the left
                shade *= 1.0 - 0.4 * t * t  # each tier's underside is in shade
                shade *= 0.82 + 0.3 * needles[y][x]
                if sd < 1.6:
                    shade *= 0.55  # outline
                over(layer, x, y, [c * shade for c in leaf], min(sd + 0.5, 1.0))
    write_bmp(DATA_DIR / f"{name}.bmp", TREE_W, TREE_H, layer_pixels(layer))


def make_scenery():
    make_patch("ground_sand_00", 100, sand_texture)
    make_patch("ground_sand_01", 101, sand_texture)
    make_patch("ground_rocks_00", 200, rocks_texture)
    make_patch("ground_rocks_01", 201, rocks_texture)
    make_patch("ground_grass_00", 300, grass_texture)
    make_patch("ground_grass_01", 301, grass_texture)

    make_tuft("tuft_00", 400, 9, 16, (40, 92, 38), (120, 190, 84))
    make_tuft("tuft_01", 401, 14, 22, (34, 84, 40), (104, 178, 80))
    make_tuft("tuft_02", 402, 10, 18, (96, 104, 44), (206, 196, 110))  # dry

    make_round_tree("tree_00", 500, (66, 132, 54), 20, 18)
    make_round_tree("tree_01", 501, (50, 112, 66), 18, 17, fruit_count=6)
    make_pine("tree_02", 502, (36, 96, 66))


if __name__ == "__main__":
    make_background()
    make_hero("hero_front", mirror_halves(HERO_FRONT_LEFT_HALF))
    make_hero("hero_back", mirror_halves(HERO_BACK_LEFT_HALF))
    make_hero("hero_right", HERO_RIGHT)
    make_hero("hero_left", [row[::-1] for row in HERO_RIGHT])
    make_scenery()
