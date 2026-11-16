"""Generates the tileable pixel-art detail textures (needs Pillow).

  python Tools/make_textures.py

Output: SourceArt/T_Detail.png  grey mottling, multiplied onto vertex colours (128 = unchanged)
        SourceArt/T_Ground.png  the same idea for the terrain, with coloured specks (leaf litter, pebbles)
        SourceArt/T_Foliage.png alpha cut-out leaf, branch and grass tiles for the foliage cards
        SourceArt/T_TerrainColor.png colour of the landscape (grass patches, road, rock), from Tools/terrain.py
"""
import math
import os
import random

from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "SourceArt")
N = 64


def blotches(rng, cell, levels):
    """Tileable blotchy value map: coarse random cells, jittered edges, a few tone levels."""
    cells = N // cell
    coarse = [[rng.choice(levels) for _ in range(cells)] for _ in range(cells)]
    grid = [[0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            jx = x + rng.randint(-2, 2)
            jy = y + rng.randint(-2, 2)
            grid[y][x] = coarse[(jy // cell) % cells][(jx // cell) % cells]
    return grid


def make_detail():
    rng = random.Random(1)
    big = blotches(rng, 8, [90, 114, 128, 150])
    small = blotches(rng, 2, [-22, -10, 0, 0, 10, 22])
    img = Image.new("RGB", (N, N))
    for y in range(N):
        for x in range(N):
            v = max(0, min(255, big[y][x] + small[y][x]))
            img.putpixel((x, y), (v, v, v))
    img.save(os.path.join(OUT, "T_Detail.png"))


def make_ground():
    rng = random.Random(2)
    big = blotches(rng, 8, [84, 110, 128, 154])
    small = blotches(rng, 2, [-24, -12, 0, 0, 12, 24])
    img = Image.new("RGB", (N, N))
    for y in range(N):
        for x in range(N):
            v = max(0, min(255, big[y][x] + small[y][x]))
            img.putpixel((x, y), (v, v, v))
    # Specks: fallen leaves (warm), pebbles (pale) and dark gaps, one or two pixels each.
    for _ in range(170):
        x, y = rng.randrange(N), rng.randrange(N)
        colour = rng.choice([(190, 120, 70), (175, 135, 80), (150, 150, 150), (80, 84, 80), (84, 80, 76)])
        img.putpixel((x, y), colour)
        if rng.random() < 0.4:
            img.putpixel(((x + 1) % N, y), colour)
    img.save(os.path.join(OUT, "T_Ground.png"))


def make_foliage():
    """T_Foliage.png: 2x2 atlas of 64x64 alpha cut-out tiles, used on the cards of trees, bushes and grass.
    Grey tones multiply the vertex colour of the card (128 = unchanged), so one tile serves every leaf colour.
    Tiles: (0,0) conifer branch, trunk end on the left; (1,0) leaf cluster; (0,1) grass tuft; (1,1) bush leaves."""
    T = 64
    atlas = Image.new("RGBA", (T * 2, T * 2), (128, 128, 128, 0))

    def put(tile, x, y, tone):
        if 0 <= x < T and 0 <= y < T:
            tone = max(40, min(230, tone))
            atlas.putpixel((tile[0] * T + x, tile[1] * T + y), (tone, tone, tone, 255))

    # Conifer branch: a drooping spine with needles on both sides, ragged along the edge.
    rng = random.Random(11)
    for x in range(T):
        t = x / (T - 1)
        spine = 30 + int(8 * t * t)
        half = int(3 + 17 * (1 - t) ** 0.7)
        for dy in range(-half, half + 1):
            edge = half - ((x * 3 + (0 if dy < 0 else 2)) % 4) - rng.randint(0, 2)
            if abs(dy) <= edge:
                streak = (x + abs(dy) * 2) % 5
                tone = (150 if dy < 0 else 112) + (26 if streak == 0 else -22 if streak == 3 else 0) - (30 if abs(dy) <= 1 else 0)
                put((0, 0), x, spine + dy, tone)

    # Leaf cluster / bush: overlapping round dabs, light on top, each with a lit upper edge.
    for tile, seed, count in (((1, 0), 21, 46), ((1, 1), 22, 40)):
        rng = random.Random(seed)
        dabs = []
        for _ in range(count):
            a, d = rng.uniform(0, 6.2832), rng.uniform(0, 1) ** 0.6 * 21
            dabs.append((32 + d * math.cos(a), 32 + d * math.sin(a), rng.uniform(4, 8)))
        for cx, cy, r in sorted(dabs, key=lambda dab: -dab[1]):   # lower dabs first, upper ones on top
            base = int(170 - (cy - 10) * 1.7) + rng.randint(-10, 10)
            for y in range(int(cy - r), int(cy + r) + 1):
                for x in range(int(cx - r), int(cx + r) + 1):
                    dd = (x - cx) ** 2 + (y - cy) ** 2
                    if dd <= r * r:
                        lit = (y - cy) < -r * 0.35 and (x - cx) < r * 0.4
                        put(tile, x, y, base + (30 if lit else -18 if (y - cy) > r * 0.45 else 0))

    # Grass tuft: drawn at half size and doubled, so its texels match the rest of the world.
    rng = random.Random(31)
    small = Image.new("RGBA", (32, 32), (128, 128, 128, 0))
    for _ in range(13):
        base, height, lean = rng.randint(3, 28), rng.randint(11, 30), rng.uniform(-6, 6)
        for i in range(height):
            f = i / height
            x = int(base + lean * f * f)
            tone = int(92 + 90 * f) + rng.randint(-8, 8)
            for w in range(2 if f < 0.6 else 1):
                if 0 <= x + w < 32:
                    small.putpixel((x + w, 31 - i), (tone, tone, tone, 255))
    atlas.paste(small.resize((T, T), Image.NEAREST), (0, T))
    atlas.save(os.path.join(OUT, "T_Foliage.png"))


def make_terrain_colour():
    """Colour of the ground, one texel per terrain.STEP: the landscape material looks it up by world position."""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import terrain
    n = int(terrain.SIZE / terrain.STEP) + 1
    img = Image.new("RGB", (n, n))
    for j in range(n):
        for i in range(n):
            c = terrain.colour(-terrain.SIZE / 2 + i * terrain.STEP, -terrain.SIZE / 2 + j * terrain.STEP)
            img.putpixel((i, j), tuple(int(round(v * 255)) for v in c))
    img.save(os.path.join(OUT, "T_TerrainColor.png"))


make_detail()
make_ground()
make_foliage()
make_terrain_colour()
print("textures written to", OUT)
