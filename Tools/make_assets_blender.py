"""Builds the low-poly prototype meshes and exports them as FBX.

Run headless:
  blender.exe --background --factory-startup --python Tools/make_assets_blender.py

Output: SourceArt/SM_*.fbx  (vertex-coloured, 1 Blender unit = 1 m, pivot at the base)
Characters face -Y in Blender. Characters are exported once per animation pose (SM_Hero_00 ..., SM_Slime_00 ...).
"""
import math
import os
import random

import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "SourceArt")
sys.path.insert(0, os.path.join(ROOT, "Tools"))  # for terrain.py
os.makedirs(OUT, exist_ok=True)


# Simple collision boxes per mesh. Exported next to the mesh as UCX_<name>_NN objects,
# which Unreal imports as that mesh's collision instead of using the render triangles.
COLLISION = {}


class Builder:
    def __init__(self, name):
        self.name = name
        self.boxes = []
        self.bm = bmesh.new()
        self.col = self.bm.loops.layers.color.new("Col")
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        # Every part gets a number, stored on its vertices. The shape functions hand back [number], and
        # rotate / move look the vertices up again by it. Vertex references cannot be kept instead:
        # creating a sphere invalidates every reference made before it, so those parts would be left behind.
        self.part = self.bm.verts.layers.int.new("part")
        self.parts = 0
        self.has_cards = False

    def _tag(self, verts):
        self.parts += 1
        for v in verts:
            v[self.part] = self.parts
        return [self.parts]

    def verts(self, parts):
        """The vertices of the given parts, as they are now."""
        wanted = set(parts)
        return [v for v in self.bm.verts if v[self.part] in wanted]

    def _finish(self, geom_verts, color, smooth, matrix):
        bmesh.ops.transform(self.bm, matrix=matrix, verts=geom_verts)
        faces = {f for v in geom_verts for f in v.link_faces}
        for f in faces:
            f.smooth = smooth
            for loop in f.loops:
                loop[self.col] = (color[0], color[1], color[2], 1.0)
        return self._tag(geom_verts)

    @staticmethod
    def _matrix(loc, rot, scale):
        m = Matrix.Translation(Vector(loc))
        m @= Matrix.Rotation(math.radians(rot[2]), 4, "Z")
        m @= Matrix.Rotation(math.radians(rot[1]), 4, "Y")
        m @= Matrix.Rotation(math.radians(rot[0]), 4, "X")
        m @= Matrix.Diagonal(Vector((scale[0], scale[1], scale[2], 1.0)))
        return m

    def box(self, size, loc, color, rot=(0, 0, 0)):
        r = bmesh.ops.create_cube(self.bm, size=1.0)
        return self._finish(r["verts"], color, False, self._matrix(loc, rot, size))

    def cone(self, r1, r2, depth, loc, color, segments=8, rot=(0, 0, 0), smooth=False):
        """Cone / cylinder standing on `loc` (base at loc.z)."""
        r = bmesh.ops.create_cone(self.bm, cap_ends=True, cap_tris=False, segments=segments,
                                  radius1=r1, radius2=r2, depth=depth)
        m = self._matrix(loc, rot, (1, 1, 1)) @ Matrix.Translation((0, 0, depth / 2))
        return self._finish(r["verts"], color, smooth, m)

    def blob(self, radius, loc, color, scale=(1, 1, 1), subdiv=2, smooth=True, jitter=0.0, seed=0):
        r = bmesh.ops.create_icosphere(self.bm, subdivisions=subdiv, radius=radius)
        if jitter:
            rng = random.Random(seed)
            for v in r["verts"]:
                v.co *= 1.0 + rng.uniform(-jitter, jitter)
        return self._finish(r["verts"], color, smooth, self._matrix(loc, (0, 0, 0), scale))

    def prism(self, length_x, half_width_y, height, loc, color):
        """Triangular prism (gable) lying along X, base centred on `loc`."""
        hx = length_x / 2
        pts = [(-hx, -half_width_y, 0), (-hx, half_width_y, 0), (-hx, 0, height),
               (hx, -half_width_y, 0), (hx, half_width_y, 0), (hx, 0, height)]
        v = [self.bm.verts.new(p) for p in pts]
        for idx in [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)]:
            self.bm.faces.new([v[i] for i in idx])
        return self._finish(v, color, False, self._matrix(loc, (0, 0, 0), (1, 1, 1)))

    def wedge(self, width_x, thick_y, height, loc, color):
        """Flat pointed tip of a blade: a triangle in the XZ plane standing on `loc`, `thick_y` thick."""
        hx, hy = width_x / 2, thick_y / 2
        pts = [(-hx, -hy, 0), (hx, -hy, 0), (0, -hy, height), (-hx, hy, 0), (hx, hy, 0), (0, hy, height)]
        v = [self.bm.verts.new(p) for p in pts]
        for idx in [(0, 1, 2), (3, 5, 4), (0, 3, 4, 1), (1, 4, 5, 2), (0, 2, 5, 3)]:
            self.bm.faces.new([v[i] for i in idx])
        return self._finish(v, color, False, self._matrix(loc, (0, 0, 0), (1, 1, 1)))

    def card(self, width, height, tile, color, loc, rot=(0, 0, 0), origin=(0.5, 0.5)):
        """Flat quad showing one tile of the foliage atlas (T_Foliage, 2x2 tiles, see make_textures.py).
        It lies in its local XZ plane; `origin` is the point of the quad (0..1) that sits on `loc`.
        Card faces use the second material slot ("Foliage"); everything else the first."""
        ox, oz = origin
        x0, x1, z0, z1 = -ox * width, (1 - ox) * width, -oz * height, (1 - oz) * height
        inset = 0.5 / 128
        u0, u1 = tile[0] * 0.5 + inset, tile[0] * 0.5 + 0.5 - inset
        v1, v0 = 1 - tile[1] * 0.5 - inset, 0.5 - tile[1] * 0.5 + inset
        verts = [self.bm.verts.new(p) for p in ((x0, 0, z0), (x1, 0, z0), (x1, 0, z1), (x0, 0, z1))]
        face = self.bm.faces.new(verts)
        face.material_index = 1
        for loop, uv in zip(face.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
            loop[self.uv].uv = uv
            loop[self.col] = (color[0], color[1], color[2], 1.0)
        bmesh.ops.transform(self.bm, matrix=self._matrix(loc, rot, (1, 1, 1)), verts=verts)
        self.has_cards = True
        return self._tag(verts)

    def rotate(self, parts, pivot, rot):
        """Rotates already built parts around a joint (hip, shoulder, hand)."""
        m = Matrix.Translation(Vector(pivot)) @ self._matrix((0, 0, 0), rot, (1, 1, 1)) @ Matrix.Translation(-Vector(pivot))
        bmesh.ops.transform(self.bm, matrix=m, verts=self.verts(parts))

    def move(self, parts, offset):
        bmesh.ops.translate(self.bm, vec=Vector(offset), verts=self.verts(parts))

    def collide_box(self, size, loc):
        """Adds a collision box (e.g. just the trunk of a tree, so the canopy does not block)."""
        self.boxes.append((size, loc))

    def build(self):
        COLLISION[self.name] = []
        for index, (size, loc) in enumerate(self.boxes):
            bm = bmesh.new()
            r = bmesh.ops.create_cube(bm, size=1.0)
            bmesh.ops.transform(bm, matrix=self._matrix(loc, (0, 0, 0), size), verts=r["verts"])
            box_mesh = bpy.data.meshes.new(f"UCX_{self.name}_{index:02d}")
            bm.to_mesh(box_mesh)
            bm.free()
            box = bpy.data.objects.new(box_mesh.name, box_mesh)
            box.hide_render = True
            bpy.context.scene.collection.objects.link(box)
            COLLISION[self.name].append(box)
        mesh = bpy.data.meshes.new(self.name)
        self.bm.verts.layers.int.remove(self.part)      # bookkeeping only; keep it out of the exported mesh
        self.bm.normal_update()
        self.bm.to_mesh(mesh)
        self.bm.free()
        attr = mesh.color_attributes.get("Col")
        if attr:
            mesh.color_attributes.active_color = attr
            mesh.color_attributes.render_color_index = list(mesh.color_attributes).index(attr)
        if self.has_cards:
            for slot in ("Surface", "Foliage"):
                mesh.materials.append(bpy.data.materials.get(slot) or bpy.data.materials.new(slot))
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def shade(color, factor):
    return tuple(min(1.0, c * factor) for c in color)


HERO_POSES_PER_WEAPON = 11
HERO_COMBO_ATTACKS = 4      # extra sword attacks after the per-weapon poses, 3 poses each


def hero_rows():
    """The animation poses of the hero, one model each. Per weapon:
    0-1 idle, 2-5 walk, 6-8 attack (wind-up, strike, follow through), 9 dodge, 10 hurt."""
    rows = []
    for weapon in ("sword", "spear"):
        tilt = 35 if weapon == "sword" else 12
        rows.append(dict(weapon=weapon, tilt=tilt))
        rows.append(dict(weapon=weapon, tilt=tilt + 4, bob=-0.02, arm_l=-6, arm_r=5))
        for leg, bob in ((34, 0.0), (0, 0.04), (-34, 0.0), (0, 0.04)):
            rows.append(dict(weapon=weapon, tilt=tilt, leg=leg, arm_l=-leg * 0.9, arm_r=leg * 0.3, bob=bob, lean=6))
        if weapon == "sword":   # overhead chop
            rows += [dict(weapon=weapon, arm_r=150, tilt=150, lean=-8, leg=10),
                     dict(weapon=weapon, arm_r=-90, tilt=170, leg=-22, lean=14),
                     dict(weapon=weapon, arm_r=-50, tilt=175, leg=-22, lean=18, bob=-0.03)]
        else:                   # thrust
            rows += [dict(weapon=weapon, arm_r=40, tilt=50, lean=-8, leg=12),
                     dict(weapon=weapon, arm_r=-80, tilt=170, reach=0.30, leg=-26, lean=16),
                     dict(weapon=weapon, arm_r=-50, tilt=140, leg=-26, lean=10)]
        rows.append(dict(weapon=weapon, tilt=80, leg=44, arm_l=-70, arm_r=30, bob=-0.14, lean=34))     # dodge
        rows.append(dict(weapon=weapon, tilt=tilt, leg=-12, arm_l=70, arm_r=50, bob=-0.03, lean=-20))  # hurt

    # The other attacks of the sword combo (wind-up, strike, follow through each), from pose 22 on.
    # The order matches the combo steps in APixelPlayerCharacter.
    combo = [
        # Slash from the right across to the left.
        dict(arm_r=-80, arm_r_yaw=-95, tilt=175, twist=-32, lean=2, leg=12, arm_l=20),
        dict(arm_r=-85, arm_r_yaw=5, tilt=175, twist=14, lean=10, leg=-20, arm_l=-20),
        dict(arm_r=-80, arm_r_yaw=65, tilt=175, twist=36, lean=12, leg=-20, arm_l=-40, bob=-0.02),
        # Backhand, back from the left to the right.
        dict(arm_r=-80, arm_r_yaw=70, tilt=175, twist=36, lean=4, leg=-10, arm_l=-30),
        dict(arm_r=-85, arm_r_yaw=-15, tilt=175, twist=-10, lean=10, leg=18, arm_l=10),
        dict(arm_r=-78, arm_r_yaw=-100, tilt=175, twist=-34, lean=10, leg=18, arm_l=35, bob=-0.02),
        # Rising cut from low behind to overhead, with a small hop.
        dict(arm_r=25, tilt=200, roll=90, lean=14, leg=14, arm_l=-25, bob=-0.06),
        dict(arm_r=-105, tilt=178, roll=90, lean=-4, leg=-16, arm_l=20, bob=0.03),
        dict(arm_r=-165, tilt=170, roll=90, lean=-12, leg=-16, arm_l=40, bob=0.09),
        # Lunging stab: hand drawn back at the hip, then driven straight out.
        dict(arm_r=35, tilt=55, twist=-24, lean=-6, leg=16, arm_l=-45),
        dict(arm_r=-90, tilt=180, reach=0.28, twist=10, lean=24, leg=-34, arm_l=50, bob=-0.04),
        dict(arm_r=-85, tilt=180, reach=0.10, lean=14, leg=-24, arm_l=30),
    ]
    assert len(combo) == HERO_COMBO_ATTACKS * 3
    rows += [dict(weapon="sword", **pose) for pose in combo]
    return rows


def make_player(pose=None, name="SM_Player"):
    """The hero: short and big-headed so the face still reads when drawn in about 40 pixels. About 1.45 m tall, faces -Y.
    `pose`: leg / arm_l / arm_r swing in degrees, tilt of the weapon in the hand, bob, reach, lean of the upper body,
    arm_r_yaw (right arm swung sideways: negative out to the right, positive across the body), twist of the upper
    body (positive turns left), roll of the blade around its own length."""
    p = dict(weapon="sword", leg=0, arm_l=0, arm_r=0, tilt=35, bob=0.0, reach=0.0, lean=0, arm_r_yaw=0, twist=0, roll=0)
    p.update(pose or {})
    b = Builder(name)
    skin, skin_dark, hair, hair_dark = (0.90, 0.66, 0.50), (0.74, 0.48, 0.36), (0.94, 0.78, 0.36), (0.74, 0.54, 0.20)
    coat, coat_dark, coat_light = (0.18, 0.46, 0.32), (0.10, 0.28, 0.22), (0.30, 0.60, 0.40)
    trousers, boots, cuff = (0.56, 0.54, 0.52), (0.26, 0.15, 0.10), (0.44, 0.28, 0.16)
    scarf, pack, strap, dark, white = (0.94, 0.50, 0.14), (0.48, 0.31, 0.17), (0.34, 0.20, 0.10), (0.07, 0.06, 0.10), (0.97, 0.97, 0.99)
    metal, wood, gold = (0.84, 0.88, 0.94), (0.45, 0.28, 0.14), (0.90, 0.74, 0.26)
    z = p["bob"]
    hip, shoulder = 0.40 + z, 0.80 + z

    for sx in (-1, 1):
        leg = (b.box((0.15, 0.17, 0.24), (sx * 0.09, 0, 0.29 + z), trousers) + b.box((0.17, 0.24, 0.15), (sx * 0.09, -0.02, 0.075 + z), boots)
               + b.box((0.18, 0.20, 0.05), (sx * 0.09, -0.01, 0.17 + z), cuff))
        b.rotate(leg, (sx * 0.09, 0, hip), (sx * p["leg"], 0, 0))

    upper = []
    upper += b.box((0.40, 0.26, 0.30), (0, 0, 0.69 + z), coat)                          # chest
    upper += b.box((0.40, 0.26, 0.14), (0, 0, 0.50 + z), coat_dark)                     # waist
    upper += b.box((0.42, 0.28, 0.05), (0, 0, 0.47 + z), pack)                          # belt
    upper += b.box((0.08, 0.05, 0.05), (0, -0.14, 0.47 + z), gold)                      # buckle
    upper += b.box((0.44, 0.29, 0.09), (0, 0, 0.395 + z), coat)                         # coat hem
    upper += b.box((0.07, 0.28, 0.42), (0.02, 0, 0.66 + z), strap, rot=(0, 38, 0))      # pack strap across the chest
    upper += b.box((0.38, 0.32, 0.11), (0, 0, 0.85 + z), scarf)                         # scarf
    upper += b.box((0.10, 0.06, 0.30), (0.10, 0.17, 0.70 + z), shade(scarf, 0.8), rot=(-14, 0, 8))
    upper += b.box((0.30, 0.14, 0.30), (0, 0.20, 0.62 + z), pack)                       # backpack
    upper += b.box((0.32, 0.16, 0.07), (0, 0.20, 0.79 + z), coat_light)                 # bedroll
    upper += b.blob(0.29, (0, 0, 1.14 + z), skin, scale=(1.0, 0.95, 0.95))              # head
    upper += b.box((0.30, 0.04, 0.05), (0, -0.265, 0.98 + z), skin_dark)                # chin shadow
    upper += b.blob(0.31, (0, 0.08, 1.24 + z), hair, scale=(1.03, 0.95, 0.80))          # hair
    upper += b.box((0.46, 0.07, 0.08), (0, -0.25, 1.36 + z), hair)                      # fringe
    upper += b.box((0.12, 0.07, 0.10), (-0.14, -0.26, 1.30 + z), hair_dark)             # fringe locks
    upper += b.box((0.10, 0.07, 0.07), (0.16, -0.26, 1.31 + z), hair_dark)
    upper += b.box((0.10, 0.10, 0.14), (0, 0.10, 1.48 + z), hair, rot=(20, 0, 0))       # tuft
    for sx in (-1, 1):
        upper += b.box((0.08, 0.30, 0.22), (sx * 0.27, 0.06, 1.14 + z), hair_dark)      # side hair
        upper += b.box((0.10, 0.04, 0.12), (sx * 0.12, -0.268, 1.11 + z), white)        # eye
        upper += b.box((0.06, 0.05, 0.10), (sx * 0.105, -0.272, 1.10 + z), dark)        # pupil

    def arm(x):
        return (b.box((0.12, 0.14, 0.26), (x, 0, 0.68 + z), coat) + b.box((0.14, 0.16, 0.07), (x, 0, 0.80 + z), coat_light)
                + b.box((0.13, 0.15, 0.06), (x, 0, 0.53 + z), cuff) + b.box((0.11, 0.12, 0.08), (x, 0, 0.47 + z), skin))

    left = arm(0.27)
    b.rotate(left, (0.27, 0, shoulder), (p["arm_l"], 0, 0))

    # Right arm (at -X when facing -Y) with the weapon in the hand. The blade points up before `tilt`.
    hand = Vector((-0.27, -0.02, 0.47 + z))
    if p["weapon"] == "sword":
        weapon = (b.box((0.06, 0.06, 0.20), hand, wood) + b.box((0.26, 0.08, 0.06), hand + Vector((0, 0, 0.11)), gold)
                  + b.box((0.10, 0.04, 0.58), hand + Vector((0, 0, 0.43)), metal)
                  + b.box((0.04, 0.045, 0.52), hand + Vector((0, 0, 0.42)), (0.62, 0.68, 0.78))
                  + b.wedge(0.10, 0.04, 0.17, hand + Vector((0, 0, 0.72)), metal))
    else:
        weapon = (b.box((0.05, 0.05, 1.60), hand + Vector((0, 0, 0.35)), wood)
                  + b.box((0.10, 0.10, 0.08), hand + Vector((0, 0, 1.13)), (0.84, 0.24, 0.18))
                  + b.cone(0.10, 0.0, 0.38, hand + Vector((0, 0, 1.17)), metal, segments=4, rot=(0, 0, 45)))
    b.rotate(weapon, hand, (0, 0, p["roll"]))
    b.rotate(weapon, hand, (p["tilt"], 0, 0))
    right = arm(-0.27)
    b.rotate(right + weapon, (-0.27, 0, shoulder), (p["arm_r"], 0, 0))
    b.rotate(right + weapon, (-0.27, 0, shoulder), (0, 0, p["arm_r_yaw"]))
    b.move(weapon, (0, -p["reach"], 0))
    b.rotate(upper + left + right + weapon, (0, 0, hip), (0, 0, p["twist"]))
    b.rotate(upper + left + right + weapon, (0, 0, hip), (p["lean"], 0, 0))
    return b.build()


def make_pine(name="SM_TreePine", greens=((0.10, 0.30, 0.24), (0.13, 0.36, 0.28), (0.17, 0.43, 0.31), (0.22, 0.50, 0.34))):
    """Tall conifer: bare trunk below, then rings of drooping branch cards (pixel texture, alpha cut-out).
    Returns two models that are placed on the same spot: <name>_Trunk, which blocks movement,
    and <name>_Leaves, which nothing collides with."""
    trunk = Builder(name + "_Trunk")
    b = Builder(name + "_Leaves")
    rng = random.Random(sum(map(ord, name)))
    trunk.cone(0.22, 0.08, 5.2, (0, 0, 0), (0.30, 0.19, 0.13), segments=6)
    tiers = 9
    for t in range(tiers):
        f = t / (tiers - 1)
        height = 1.5 + 3.5 * f
        length = 2.3 - 1.6 * f
        count = 7 if f < 0.6 else 5
        for i in range(count):
            az = (i + 0.5 * (t % 2)) / count * 360 + rng.uniform(-10, 10)
            roll = rng.choice((-1, 1)) * rng.uniform(35, 70)
            b.card(length, length * 0.85, (0, 0), greens[min(3, int(f * 4))] if rng.random() < 0.7 else greens[rng.randrange(4)],
                   (0, 0, height + rng.uniform(-0.1, 0.1)), rot=(roll, rng.uniform(16, 30), az), origin=(0.04, 0.5))
    for az in (0, 90):   # the tip: two crossed upright cards
        b.card(0.9, 0.9, (0, 0), greens[3], (0, 0, 5.0), rot=(0, -78, az), origin=(0.04, 0.5))
    return [trunk.build(), b.build()]


def make_round_tree(name="SM_TreeRound", leaves=((0.24, 0.44, 0.16), (0.17, 0.34, 0.14), (0.20, 0.39, 0.15), (0.33, 0.52, 0.19))):
    """Broadleaf tree: trunk, a few boughs, and a crown of leaf-cluster cards around dark cores.
    Returns <name>_Trunk (blocks movement) and <name>_Leaves (no collision), like make_pine."""
    trunk = Builder(name + "_Trunk")
    b = Builder(name + "_Leaves")
    rng = random.Random(sum(map(ord, name)))
    bark = (0.34, 0.22, 0.14)
    trunk.cone(0.24, 0.15, 2.5, (0, 0, 0), bark, segments=6)
    crowns = [(0.0, 0.0, 3.5, 1.25), (0.85, 0.3, 3.0, 0.9), (-0.75, -0.4, 3.05, 0.95), (0.1, -0.75, 3.95, 0.8), (-0.3, 0.75, 3.75, 0.85)]
    for x, y, zc, r in crowns:
        trunk.cone(0.09, 0.05, math.dist((0, 0, 2.2), (x, y, zc)), (0, 0, 2.2), bark, segments=5,
               rot=(math.degrees(math.atan2(-y, zc - 2.2)), math.degrees(math.atan2(x, zc - 2.2)), 0))
        b.blob(r * 0.62, (x, y, zc), shade(leaves[1], 0.65), subdiv=1, smooth=False, jitter=0.12, seed=int(zc * 10))
        for i in range(8):
            # Points spread over the upper sphere; each card looks roughly outwards.
            az = rng.uniform(0, math.tau)
            el = rng.uniform(-0.35, 1.3)
            d = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
            size = rng.uniform(1.7, 2.2) * (0.75 + 0.25 * r)
            color = leaves[3] if d.z > 0.55 else leaves[rng.randrange(3)]
            b.card(size, size, (1, 0), color, Vector((x, y, zc)) + d * r * 0.7,
                   rot=(rng.uniform(-25, 25), rng.uniform(-180, 180), math.degrees(az) + 90 + rng.uniform(-30, 30)))
    return [trunk.build(), b.build()]


def make_red_tree():
    return make_round_tree("SM_TreeRed", ((0.78, 0.20, 0.14), (0.60, 0.13, 0.11), (0.70, 0.16, 0.12), (0.92, 0.34, 0.16)))


def make_orange_tree():
    return make_round_tree("SM_TreeOrange", ((0.90, 0.50, 0.14), (0.74, 0.36, 0.11), (0.84, 0.44, 0.12), (0.98, 0.68, 0.22)))


def make_mountain():
    """Distant peak, placed far away and scaled up in the level."""
    b = Builder("SM_Mountain")
    b.cone(22.0, 6.0, 14.0, (0, 0, 0), (0.30, 0.36, 0.46), segments=9)
    b.cone(6.0, 0.0, 7.0, (0, 0, 14.0), (0.62, 0.68, 0.76), segments=9)
    b.cone(13.0, 0.0, 12.0, (15.0, 6.0, 0), (0.27, 0.33, 0.43), segments=7)
    b.cone(11.0, 0.0, 9.0, (-14.0, -4.0, 0), (0.33, 0.39, 0.48), segments=7)
    return b.build()


def make_rock():
    b = Builder("SM_Rock")
    b.blob(0.6, (0, 0, 0.30), (0.42, 0.43, 0.47), scale=(1.2, 0.95, 0.65), subdiv=1, smooth=False, jitter=0.18, seed=3)
    b.blob(0.32, (0.62, 0.25, 0.16), (0.36, 0.37, 0.42), scale=(1, 1, 0.7), subdiv=1, smooth=False, jitter=0.2, seed=5)
    return b.build()


def make_bush():
    b = Builder("SM_Bush")
    rng = random.Random(4)
    greens = [(0.20, 0.42, 0.18), (0.26, 0.50, 0.20), (0.34, 0.58, 0.22)]
    b.blob(0.42, (0, 0, 0.36), shade(greens[0], 0.6), scale=(1.2, 1.1, 0.8), subdiv=1, smooth=False, jitter=0.1, seed=2)
    for i in range(7):
        az = i / 7 * 360 + rng.uniform(-15, 15)
        a = math.radians(az)
        b.card(1.25, 1.25, (1, 1), greens[i % 3], (math.cos(a) * 0.28, math.sin(a) * 0.28, 0.48 + rng.uniform(-0.05, 0.15)),
               rot=(rng.uniform(-20, 20), rng.uniform(-40, 40), az + 90))
    return b.build()


def make_grass():
    """A tuft: three crossed upright cards with a pixel grass texture."""
    b = Builder("SM_Grass")
    greens = [(0.30, 0.46, 0.17), (0.36, 0.52, 0.19), (0.25, 0.41, 0.16)]
    for i in range(3):
        b.card(0.62, 0.62, (0, 1), greens[i], (0, 0, 0), rot=(0, 0, i * 60 + 10), origin=(0.5, 0.02))
    return b.build()


def make_flowers():
    b = Builder("SM_Flowers")
    petals = [(0.95, 0.85, 0.25), (0.92, 0.92, 0.95), (0.85, 0.35, 0.65)]
    for i, (x, y, h) in enumerate([(0, 0, 0.34), (0.22, 0.12, 0.26), (-0.16, 0.2, 0.30)]):
        b.box((0.04, 0.04, h), (x, y, h / 2), (0.25, 0.50, 0.16))
        b.box((0.14, 0.14, 0.10), (x, y, h + 0.03), petals[i])
    return b.build()


def make_crystal():
    b = Builder("SM_Crystal")
    for (x, y), h, r, rot, c in [
        ((0, 0), 1.3, 0.26, (6, -4, 0), (0.25, 0.62, 0.95)),
        ((0.34, 0.1), 0.8, 0.18, (-8, 22, 0), (0.18, 0.50, 0.88)),
        ((-0.28, 0.16), 0.65, 0.16, (10, -26, 0), (0.30, 0.70, 0.98)),
        ((0.05, -0.3), 0.5, 0.14, (30, 4, 0), (0.20, 0.55, 0.92)),
    ]:
        b.cone(r * 0.75, r, h * 0.75, (x, y, 0), c, segments=6, rot=rot)
        top = b.cone(r, 0.0, h * 0.25, (0, 0, 0), (min(c[0] + 0.3, 1), min(c[1] + 0.2, 1), 1.0), segments=6)
        m = Builder._matrix((x, y, 0), rot, (1, 1, 1)) @ Matrix.Translation((0, 0, h * 0.75))
        bmesh.ops.transform(b.bm, matrix=m, verts=b.verts(top))
    b.blob(0.5, (0, 0, 0.05), (0.38, 0.39, 0.44), scale=(1.3, 1.1, 0.3), subdiv=1, smooth=False, jitter=0.15, seed=9)
    return b.build()


def make_house():
    b = Builder("SM_House")
    wall, wood, roof, roof_dark = (0.80, 0.72, 0.56), (0.36, 0.22, 0.12), (0.62, 0.20, 0.14), (0.45, 0.14, 0.11)
    stone, glass = (0.45, 0.45, 0.48), (0.35, 0.60, 0.80)
    w, d, h = 4.0, 3.2, 2.2
    b.box((w + 0.2, d + 0.2, 0.3), (0, 0, 0.15), stone)                    # foundation
    b.box((w, d, h), (0, 0, 0.3 + h / 2), wall)
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box((0.22, 0.22, h), (sx * w / 2, sy * d / 2, 0.3 + h / 2), wood)  # corner posts
    # Gabled roof: two slabs.
    slope = math.degrees(math.atan2(1.3, d / 2 + 0.3))
    length = math.hypot(1.3, d / 2 + 0.3) + 0.25
    for sy in (-1, 1):
        b.box((w + 0.7, length, 0.18), (0, sy * (d / 4 + 0.2), 0.3 + h + 0.62), roof, rot=(-sy * slope, 0, 0))
    b.box((w + 0.8, 0.3, 0.2), (0, 0, 0.3 + h + 1.3), roof_dark)            # ridge
    b.prism(w - 0.02, d / 2, 1.25, (0, 0, 0.3 + h), wall)                 # gable fill
    b.box((0.9, 0.12, 1.5), (-0.8, -d / 2 - 0.03, 0.3 + 0.75), wood)        # door
    for x in (0.6, 1.4):
        b.box((0.6, 0.1, 0.6), (x, -d / 2 - 0.03, 1.6), glass)
        b.box((0.72, 0.08, 0.1), (x, -d / 2 - 0.05, 1.25), wood)
    b.box((0.5, 0.5, 1.4), (1.3, 0.7, 0.3 + h + 0.9), stone)                # chimney
    return b.build()


def make_sword():
    """Grip at the origin, blade along +Z."""
    b = Builder("SM_Sword")
    b.box((0.07, 0.07, 0.22), (0, 0, 0.0), (0.30, 0.18, 0.10))            # grip
    b.box((0.10, 0.10, 0.08), (0, 0, -0.14), (0.85, 0.70, 0.25))          # pommel
    b.box((0.34, 0.09, 0.07), (0, 0, 0.14), (0.85, 0.70, 0.25))           # guard
    b.box((0.13, 0.05, 0.70), (0, 0, 0.52), (0.80, 0.84, 0.90))           # blade
    b.wedge(0.13, 0.05, 0.22, (0, 0, 0.87), (0.80, 0.84, 0.90))                              # point
    return b.build()


def make_spear():
    """Held at the origin, point along +Z."""
    b = Builder("SM_Spear")
    b.box((0.06, 0.06, 1.9), (0, 0, 0.45), (0.45, 0.28, 0.14))            # shaft
    b.box((0.12, 0.12, 0.10), (0, 0, 1.38), (0.75, 0.20, 0.16))           # binding
    b.cone(0.11, 0.0, 0.42, (0, 0, 1.42), (0.82, 0.86, 0.92), segments=4, rot=(0, 0, 45))
    return b.build()


SLIME_SCALE = 0.65   # size of the whole model; the proportions below are for a slime about 1 m wide

SLIME_POSES = [   # 0-1 idle, 2-3 moving (stretch, squash), 4 about to attack, 5 flying at the target, 6 hurt
    dict(), dict(tall=0.92), dict(tall=1.15, lift=0.10), dict(tall=0.82),
    dict(tall=0.66, angry=True), dict(tall=0.95, long=1.3, angry=True, lift=0.06), dict(tall=0.86, shut=True, long=0.9),
]


def make_slime(pose=None, name="SM_Slime"):
    """The enemy: a small plain jelly ball, flat on the ground, with two eyes of different size and nothing else. Faces -Y.
    `pose`: tall / long squash factors, lift, angry brows, shut eyes."""
    p = dict(tall=1.0, long=1.0, lift=0.0, angry=False, shut=False)
    p.update(pose or {})
    b = Builder(name)
    body, body_dark, body_light = (0.34, 0.47, 0.15), (0.20, 0.31, 0.09), (0.47, 0.60, 0.22)   # dark yellow-green
    dark = (0.05, 0.08, 0.03)
    t, l = p["tall"], p["long"]
    wide = 1.0 / math.sqrt(t * l)          # keep the volume: squashed means wider
    floor = p["lift"]
    rx, ry, rz = 0.46 * wide, 0.46 * l, 0.44 * t
    centre = floor + 0.30 * t              # the ball sinks into the floor and is cut flat there
    top = centre + rz
    flare = 0.24                           # how much wider the bottom is than a plain ball

    def widen(z):
        return 1.0 + flare * (1.0 - min(1.0, max(0.0, (z - floor) / (top - floor))))

    verts = b.verts(b.blob(1.0, (0, 0, centre), body, scale=(rx, ry, rz), subdiv=3))
    for v in verts:
        v.co.z = max(v.co.z, floor)
        f = widen(v.co.z)
        v.co.x *= f
        v.co.y *= f
    for face in {f for v in verts for f in v.link_faces}:      # darker towards the floor, lighter on top
        for loop in face.loops:
            h = (loop.vert.co.z - floor) / (top - floor)
            c = [body_dark[i] + (body[i] - body_dark[i]) * min(1.0, h / 0.45) for i in range(3)]
            if h > 0.7:
                c = [c[i] + (body_light[i] - c[i]) * (h - 0.7) / 0.3 * 0.6 for i in range(3)]
            loop[b.col] = (c[0], c[1], c[2], 1.0)

    def skin(x, z, sink=0.0):
        """Point on the front of the body at side offset x and height z, moved `sink` into the body."""
        f = widen(z)
        inside = max(0.0, 1.0 - (x / (rx * f)) ** 2 - ((z - centre) / rz) ** 2)
        y = -ry * f * math.sqrt(inside)
        normal = Vector((x / (rx * f) ** 2, y / (ry * f) ** 2, (z - centre) / rz ** 2)).normalized()
        return Vector((x, y, z)) - normal * sink

    eye_z = floor + 0.38 * t
    eye_x = 0.16 * wide
    for sx in (-1, 1):
        size = 1.45 if sx > 0 else 0.85     # odd eyes: its left one is much bigger
        if p["shut"]:
            b.box((0.11 * size, 0.04, 0.03), skin(sx * eye_x, eye_z, sink=-0.012), dark)
        else:
            b.blob(0.055 * size, skin(sx * eye_x, eye_z, sink=0.018 * size), dark, scale=(1.0, 1.0, 0.8 if p["angry"] else 1.1), subdiv=2)
        if p["angry"]:
            b.box((0.13, 0.04, 0.035), skin(sx * eye_x * 0.95, eye_z + 0.085, sink=-0.012), dark, rot=(0, -sx * 24, 0))

    bmesh.ops.scale(b.bm, vec=Vector((SLIME_SCALE,) * 3), verts=b.bm.verts)
    return b.build()


def make_ring():
    """Thin hoop, 1 m across, standing upright around the origin with its opening along Y.
    A row of them in the air marks the path of an enemy's charge (APixelEnemyCharacter)."""
    b = Builder("SM_Ring")
    major, minor, around, across = 0.5, 0.052, 24, 6    # a thick tube: the hoops are shown at about half this size
    grid = [[b.bm.verts.new(((major + minor * math.cos(v)) * math.cos(u), minor * math.sin(v), (major + minor * math.cos(v)) * math.sin(u)))
             for v in (math.tau * j / across for j in range(across))]
            for u in (math.tau * i / around for i in range(around))]
    for i in range(around):
        for j in range(across):
            b.bm.faces.new([grid[i][j], grid[i][(j + 1) % across], grid[(i + 1) % around][(j + 1) % across], grid[(i + 1) % around][j]])
    b._finish([v for row in grid for v in row], (0.95, 0.10, 0.08), True, Matrix.Identity(4))
    return b.build()


def make_slash():
    """Flat 120 degree fan of radius 1 centred on +X, used as the attack swoosh."""
    b = Builder("SM_SlashArc")
    steps, inner = 10, 0.45
    pts = []
    for i in range(steps + 1):
        a = math.radians(-60 + 120 * i / steps)
        pts.append((math.cos(a), math.sin(a)))
    verts = []
    for i in range(steps):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        quad = [b.bm.verts.new((x0 * inner, y0 * inner, 0)), b.bm.verts.new((x0, y0, 0)),
                b.bm.verts.new((x1, y1, 0)), b.bm.verts.new((x1 * inner, y1 * inner, 0))]
        b.bm.faces.new(quad)
        verts += quad
    b._finish(verts, (1.0, 0.97, 0.80), False, Matrix.Identity(4))
    return b.build()


COMPANION_POSES = [   # 0-1 idle, 2-3 moving, 4-5 casting, 6 hurt
    dict(), dict(bob=0.03, staff=4), dict(lean=14, staff=-10, bob=0.02), dict(lean=14, staff=8, bob=0.05),
    dict(staff=-40, raise_=0.22, glow=1.5, lean=-6), dict(staff=-75, raise_=0.16, glow=2.3, lean=10), dict(lean=-24, staff=30, bob=-0.03),
]


def make_companion(pose=None, name="SM_Companion"):
    """Small hooded mage that rides on the player's back. Faces -Y.
    `pose`: bob, lean of the body, staff tilt (negative = forward), raise_ of the staff, glow size of the orb."""
    p = dict(bob=0.0, lean=0, staff=0, raise_=0.0, glow=1.0)
    p.update(pose or {})
    b = Builder(name)
    robe, robe_dark, robe_light = (0.12, 0.50, 0.54), (0.07, 0.30, 0.36), (0.22, 0.66, 0.66)
    hat, hat_dark, band = (0.30, 0.24, 0.66), (0.20, 0.15, 0.46), (0.94, 0.78, 0.26)
    dark, glow, glow_core, wood = (0.04, 0.05, 0.08), (0.50, 0.92, 1.0), (0.90, 1.0, 1.0), (0.42, 0.27, 0.15)
    z = p["bob"]
    body = []
    body += b.cone(0.27, 0.15, 0.46, (0, 0, z), robe, segments=8, smooth=True)             # robe
    body += b.box((0.32, 0.24, 0.07), (0, 0, 0.035 + z), robe_dark)                        # hem
    body += b.box((0.07, 0.04, 0.34), (0, -0.19, 0.24 + z), robe_light)                    # front trim
    body += b.blob(0.23, (0, 0, 0.58 + z), robe_dark, scale=(1.0, 1.0, 0.95))              # hood
    body += b.box((0.26, 0.06, 0.18), (0, -0.20, 0.57 + z), dark)                          # face shadow
    for sx in (-1, 1):
        body += b.box((0.07, 0.03, 0.08), (sx * 0.07, -0.235, 0.58 + z), glow)             # eyes
        body += b.box((0.09, 0.10, 0.20), (sx * 0.24, -0.02, 0.33 + z), robe)              # sleeves
    body += b.cone(0.38, 0.36, 0.05, (0, 0, 0.72 + z), hat, segments=8)                    # hat brim
    body += b.cone(0.21, 0.0, 0.46, (0, 0.02, 0.76 + z), hat, segments=8, rot=(-16, 0, 0))
    body += b.cone(0.215, 0.195, 0.07, (0, 0, 0.76 + z), band, segments=8)
    body += b.box((0.07, 0.07, 0.07), (0, 0.13, 1.16 + z), hat_dark)                       # bent tip
    hand = (0.30, -0.08, 0.34 + z + p["raise_"])
    staff = (b.box((0.04, 0.04, 0.84), (hand[0], hand[1], hand[2] + 0.08), wood)
             + b.blob(0.09 * p["glow"], (hand[0], hand[1], hand[2] + 0.56), glow, subdiv=1, smooth=False)
             + b.blob(0.05 * p["glow"], (hand[0], hand[1] - 0.03, hand[2] + 0.58), glow_core, subdiv=1, smooth=False))
    b.rotate(staff, hand, (-p["staff"], 0, 0))
    b.rotate(body + staff, (0, 0, 0.1 + z), (p["lean"], 0, 0))
    return b.build()


def make_bolt():
    """Magic projectile: a small double pyramid."""
    b = Builder("SM_Bolt")
    b.cone(0.11, 0.0, 0.16, (0, 0, 0), (0.60, 0.95, 1.0), segments=4)
    b.cone(0.11, 0.0, 0.16, (0, 0, 0), (0.85, 1.0, 1.0), segments=4, rot=(180, 0, 0))
    return b.build()


def make_herb():
    """Gatherable plant: broad pale leaves around a glowing bud."""
    b = Builder("SM_Herb")
    leaf, leaf_light, stem, bud = (0.30, 0.62, 0.55), (0.50, 0.80, 0.70), (0.20, 0.42, 0.30), (0.95, 0.95, 0.70)
    for i in range(6):
        a = math.radians(i * 60 + 15)
        r = 0.20
        b.box((0.30, 0.13, 0.04), (math.cos(a) * r, math.sin(a) * r, 0.10 + (i % 2) * 0.05),
              leaf if i % 2 else leaf_light, rot=(0, -28, math.degrees(a)))
    b.box((0.05, 0.05, 0.46), (0, 0, 0.23), stem)
    b.blob(0.11, (0, 0, 0.52), bud, subdiv=1, smooth=False)
    b.box((0.05, 0.05, 0.08), (0, 0, 0.66), (1.0, 1.0, 0.9))
    return b.build()


def make_ore():
    """Gatherable ore: a dark boulder with copper-coloured nuggets."""
    b = Builder("SM_Ore")
    rock, rock_dark, ore, ore_light = (0.33, 0.32, 0.36), (0.24, 0.23, 0.28), (0.85, 0.48, 0.20), (1.0, 0.72, 0.35)
    b.blob(0.50, (0, 0, 0.28), rock, scale=(1.2, 1.0, 0.75), subdiv=1, smooth=False, jitter=0.18, seed=3)
    b.blob(0.30, (0.38, 0.22, 0.20), rock_dark, scale=(1.0, 1.0, 0.8), subdiv=1, smooth=False, jitter=0.2, seed=5)
    for (x, y, z), s, c in [((-0.20, -0.30, 0.42), 0.16, ore), ((0.18, -0.20, 0.56), 0.13, ore_light),
                            ((0.05, 0.25, 0.60), 0.15, ore), ((-0.42, 0.10, 0.34), 0.12, ore_light),
                            ((0.48, 0.05, 0.38), 0.11, ore)]:
        b.box((s, s, s), (x, y, z), c, rot=(20, 30, 40))
    return b.build()


VILLAGER_POSES = [   # 0-1 idle, 2-3 talking
    dict(), dict(bob=-0.02, arm_l=-6), dict(arm_l=-110, nod=8), dict(arm_l=-70, nod=-4, bob=-0.02),
]


def make_villager(pose=None, name="SM_Villager"):
    """Quest giver: an old herbalist in an apron and a straw hat, built like the hero (short, big head). Faces -Y.
    `pose`: bob, swing of the free arm, nod of the head."""
    p = dict(bob=0.0, arm_l=0, nod=0)
    p.update(pose or {})
    b = Builder(name)
    skin, skin_dark, beard = (0.86, 0.64, 0.50), (0.70, 0.48, 0.38), (0.92, 0.92, 0.90)
    shirt, shirt_dark, apron, apron_light = (0.72, 0.60, 0.38), (0.54, 0.42, 0.26), (0.26, 0.48, 0.26), (0.40, 0.62, 0.34)
    trousers, boots = (0.36, 0.28, 0.22), (0.20, 0.12, 0.08)
    straw, straw_dark, dark, white, wood = (0.94, 0.80, 0.40), (0.72, 0.54, 0.24), (0.07, 0.06, 0.10), (0.97, 0.97, 0.99), (0.42, 0.27, 0.15)
    z = p["bob"]
    shoulder = 0.82 + z
    for sx in (-1, 1):
        b.box((0.16, 0.18, 0.26), (sx * 0.10, 0, 0.28), trousers)
        b.box((0.18, 0.25, 0.15), (sx * 0.10, -0.02, 0.075), boots)
    b.box((0.46, 0.30, 0.46), (0, 0, 0.62 + z), shirt)                          # stout torso
    b.box((0.46, 0.30, 0.10), (0, 0, 0.42 + z), shirt_dark)
    b.box((0.36, 0.05, 0.50), (0, -0.16, 0.58 + z), apron)                      # apron
    b.box((0.20, 0.05, 0.12), (0, -0.18, 0.56 + z), apron_light)                # apron pocket
    b.box((0.48, 0.32, 0.05), (0, 0, 0.46 + z), wood)                           # belt
    head = []
    head += b.blob(0.28, (0, 0, 1.13 + z), skin, scale=(1.0, 0.95, 0.95))
    head += b.box((0.36, 0.10, 0.22), (0, -0.20, 0.99 + z), beard)              # beard
    head += b.box((0.20, 0.08, 0.10), (0, -0.21, 0.86 + z), beard)
    head += b.box((0.10, 0.06, 0.07), (0, -0.28, 1.10 + z), skin_dark)          # nose
    for sx in (-1, 1):
        head += b.box((0.09, 0.04, 0.09), (sx * 0.12, -0.262, 1.17 + z), white)
        head += b.box((0.05, 0.05, 0.07), (sx * 0.11, -0.266, 1.16 + z), dark)
        head += b.box((0.12, 0.05, 0.04), (sx * 0.12, -0.265, 1.25 + z), beard) # brows
    head += b.cone(0.50, 0.48, 0.04, (0, 0, 1.33 + z), straw, segments=8)       # hat brim
    head += b.cone(0.26, 0.18, 0.18, (0, 0, 1.36 + z), straw, segments=8)
    head += b.cone(0.265, 0.26, 0.05, (0, 0, 1.36 + z), straw_dark, segments=8)
    b.rotate(head, (0, 0, 0.92 + z), (p["nod"], 0, 0))

    def arm(x):
        return b.box((0.13, 0.15, 0.30), (x, 0, 0.68 + z), shirt) + b.box((0.12, 0.13, 0.09), (x, 0, 0.49 + z), skin)

    b.rotate(arm(0.30), (0.30, 0, shoulder), (p["arm_l"], 0, 0))
    arm(-0.30)
    b.box((0.05, 0.05, 1.30), (-0.34, -0.12, 0.65), wood)                       # walking stick in the right hand
    return b.build()


def export(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    for box in COLLISION.get(obj.name, []):
        box.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(
        filepath=os.path.join(OUT, obj.name + ".fbx"),
        use_selection=True,
        object_types={"MESH"},
        mesh_smooth_type="FACE",
        colors_type="SRGB",
        axis_forward="-Y",
        axis_up="Z",
        bake_space_transform=True,
        add_leaf_bones=False,
    )


def main():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    objs = [make_player(pose, f"SM_Hero_{i:02d}") for i, pose in enumerate(hero_rows())]
    objs += [make_slime(pose, f"SM_Slime_{i:02d}") for i, pose in enumerate(SLIME_POSES)]
    objs += [make_companion(pose, f"SM_Companion_{i:02d}") for i, pose in enumerate(COMPANION_POSES)]
    objs += [make_villager(pose, f"SM_Villager_{i:02d}") for i, pose in enumerate(VILLAGER_POSES)]
    for make_tree in (make_pine, make_round_tree, make_red_tree, make_orange_tree):
        objs += make_tree()
    objs += [f() for f in (make_rock, make_bush,
                           make_grass, make_flowers, make_crystal, make_house, make_sword, make_spear,
                           make_slash, make_ring, make_bolt, make_herb, make_ore, make_mountain)]
    for o in objs:
        export(o)
    print("PixelRPG: exported", len(objs), "meshes")


main()
