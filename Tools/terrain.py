"""Shape of the test valley, shared by the Blender mesh builder and the UE level builder.

Pure math (no bpy / unreal imports) so both tools get exactly the same ground.
All values are in Unreal units (cm).
"""
import math

SIZE = 16000.0      # the ground covers [-SIZE/2, SIZE/2] on both axes
STEP = 100.0        # spacing of the samples of the colour map

# The landscape: COMPONENTS x COMPONENTS components of QUADS x QUADS quads each.
QUADS = 63
COMPONENTS = 4
SAMPLES = QUADS * COMPONENTS + 1        # height samples along one side
SPACING = SIZE / (SAMPLES - 1)          # world units between two height samples

GRASS = [(0.20, 0.30, 0.12), (0.24, 0.33, 0.13), (0.29, 0.36, 0.14), (0.33, 0.37, 0.16)]
DIRT = [(0.36, 0.22, 0.13), (0.42, 0.26, 0.15), (0.47, 0.31, 0.19)]
ROCK = [(0.34, 0.35, 0.38), (0.40, 0.41, 0.44)]


def _smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def path_center(x):
    """Y position of the dirt road at a given X."""
    return 520.0 * math.sin(x / 1500.0) + 160.0 * math.sin(x / 520.0 + 1.0)


def path_weight(x, y):
    """1 on the road, 0 away from it."""
    if abs(x) > 6200.0:
        return 0.0
    return 1.0 - _smooth(90.0, 210.0, abs(y - path_center(x)))


def height(x, y):
    r = math.hypot(x, y)
    hills = (150.0 * math.sin(x / 1400.0 + 0.5) * math.cos(y / 1100.0)
             + 80.0 * math.sin((x + y) / 620.0)
             + 35.0 * math.sin(x / 260.0) * math.sin(y / 310.0))
    # Flat clearing around the start, rolling hills beyond, a rim of high ground at the edge.
    h = hills * _smooth(350.0, 1600.0, r)
    rim = _smooth(5200.0, 7800.0, r)
    h += 1900.0 * rim * rim
    # The road is worn a little into the ground.
    h -= 18.0 * path_weight(x, y)
    return h


def slope(x, y):
    """Steepness (rise over run) at a point."""
    d = 50.0
    dx = (height(x + d, y) - height(x - d, y)) / (2 * d)
    dy = (height(x, y + d) - height(x, y - d)) / (2 * d)
    return math.hypot(dx, dy)


def colour(x, y):
    """Ground colour at a point: dirt on the road, rock on steep slopes, patchy grass elsewhere."""
    # Large patches from a coarse cell hash, speckle from a fine one.
    patch = noise(math.floor(x / 500.0), math.floor(y / 500.0))
    fine = noise(x * 0.37, y * 0.53)
    if path_weight(x, y) + (fine - 0.5) * 0.5 > 0.5:
        return DIRT[int(fine * 2.999)]
    if slope(x, y) > 0.55 + fine * 0.15:
        return ROCK[int(fine * 1.999)]
    return GRASS[min(3, int((patch * 0.7 + fine * 0.3) * 4))]


def noise(x, y):
    """Cheap deterministic value in [0, 1) for colour variation and scattering."""
    v = math.sin(x * 12.9898 + y * 78.233) * 43758.5453
    return v - math.floor(v)
