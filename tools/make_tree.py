#!/usr/bin/env python3
"""Writes tree.glb: an original low-poly pine tree for Game3D's demo (CC0).

Usage: python tools/make_tree.py assets/models/tree.glb

Pure Python (standard library only; uses gltf_builder.py beside it). Units are metres,
+Y up, the trunk's foot at the origin. One node, Tree, with one mesh of two parts: the
trunk (Bark) and three stacked cones of branches (Needles). The demo draws hundreds of
copies of it with MeshRenderer::drawInstanced, each turned and sized a little differently.
"""
import math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gltf_builder import Builder, Mesh


def add_cone(m, base_y, height, radius, segments=10, jag=0.78, twist=0.0):
    """A flat-shaded cone of branches: its rim zigzags in and out (every other point at
    `jag` times the radius), so it reads as a pine's layer of branches, not a party hat.
    The underside is closed, for views from below."""
    tip = (0.0, base_y + height, 0.0)
    rim = []
    for i in range(segments * 2):
        a = twist + math.pi * i / segments
        r = radius * (1.0 if i % 2 == 0 else jag)
        rim.append((math.cos(a) * r, base_y + (0.0 if i % 2 == 0 else 0.12 * height), math.sin(a) * r))
    centre = (0.0, base_y + 0.06 * height, 0.0)
    for i in range(len(rim)):
        a, b = rim[i], rim[(i + 1) % len(rim)]
        m.tri(b, a, tip)     # outside, counter-clockwise seen from outside
        m.tri(a, b, centre)  # the underside


def build_tree(path):
    b = Builder('make_tree.py (Game3D)')
    bark = b.material('Bark', [0.13, 0.075, 0.04, 1], roughness=0.9)
    needles = b.material('Needles', [0.035, 0.12, 0.045, 1], roughness=0.85)

    trunk = Mesh()
    trunk.cylinder((0, 0.7, 0), 0.14, 1.4, segments=7)

    crown = Mesh()
    add_cone(crown, 0.9, 1.9, 1.35, twist=0.0)
    add_cone(crown, 1.85, 1.6, 1.0, twist=0.35)
    add_cone(crown, 2.75, 1.45, 0.65, twist=0.7)

    b.node('Tree', mesh=b.mesh('Tree', [(trunk, bark), (crown, needles)]))
    b.write(path)
    return len(trunk.idx) // 3 + len(crown.idx) // 3


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'tree.glb'
    print(out, build_tree(out), 'triangles')
