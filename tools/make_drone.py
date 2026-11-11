#!/usr/bin/env python3
"""Writes drone.glb: an original low-poly quadcopter for Game3D's demo (CC0).

Usage: python tools/make_drone.py assets/models/drone.glb

Pure Python (standard library only). Units are metres, +Y up, the front faces +Z.
Nodes: Drone (root, empty) > Body, Frame, Eye, RotorFL, RotorFR, RotorBL, RotorBR.
The four rotor nodes share one mesh and sit at the top of the motors with an identity
rotation, so a game spins them by setting the node's rotation about +Y.
"""
import json, math, struct, sys, zlib

# ---------------------------------------------------------------- geometry helpers
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def quad(self, a, b, c, d, uvs=((0, 1), (1, 1), (1, 0), (0, 0))):
        """Counter-clockwise (seen from outside) quad a-b-c-d with a flat normal."""
        n = norm(cross(sub(b, a), sub(d, a)))
        base = len(self.pos)
        for p, t in zip((a, b, c, d), uvs):
            self.pos.append(p); self.nrm.append(n); self.uv.append(t)
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def tri(self, a, b, c, uvs=((0, 1), (1, 1), (0.5, 0))):
        n = norm(cross(sub(b, a), sub(c, a)))
        base = len(self.pos)
        for p, t in zip((a, b, c), uvs):
            self.pos.append(p); self.nrm.append(n); self.uv.append(t)
        self.idx += [base, base + 1, base + 2]

    def box(self, centre, size, yaw=0.0, uv_scale=(1, 1)):
        cx, cy, cz = centre; hx, hy, hz = (s / 2 for s in size)
        def P(x, y, z):
            c, s = math.cos(yaw), math.sin(yaw)
            return (cx + x * c + z * s, cy + y, cz - x * s + z * c)
        su, sv = uv_scale
        uv = ((0, sv), (su, sv), (su, 0), (0, 0))
        v = [P(x, y, z) for x in (-hx, hx) for y in (-hy, hy) for z in (-hz, hz)]
        # v index = xi*4 + yi*2 + zi
        self.quad(v[1], v[5], v[7], v[3], uv)  # +Z (front)
        self.quad(v[4], v[0], v[2], v[6], uv)  # -Z
        self.quad(v[5], v[4], v[6], v[7], uv)  # +X
        self.quad(v[0], v[1], v[3], v[2], uv)  # -X
        self.quad(v[3], v[7], v[6], v[2], uv)  # +Y
        self.quad(v[0], v[4], v[5], v[1], uv)  # -Y

    def cylinder(self, centre, radius, height, segments=10, axis='y', caps=True):
        """Flat-shaded prism; axis 'y' (upright) or 'z' (lying along the front axis)."""
        cx, cy, cz = centre
        def P(a, h):
            u, w = math.cos(a) * radius, math.sin(a) * radius
            return (cx + u, cy + h, cz + w) if axis == 'y' else (cx + u, cy + w, cz + h)
        h2 = height / 2
        sign = 1 if axis == 'y' else -1  # keep the winding outward for both axes
        for i in range(segments):
            a0 = 2 * math.pi * i / segments; a1 = 2 * math.pi * (i + 1) / segments
            if sign > 0:
                self.quad(P(a1, -h2), P(a0, -h2), P(a0, h2), P(a1, h2))
            else:
                self.quad(P(a0, -h2), P(a1, -h2), P(a1, h2), P(a0, h2))
            if caps:
                top, bot = P(0, h2), P(0, -h2)
                ct = (cx, cy + h2, cz) if axis == 'y' else (cx, cy, cz + h2)
                cb = (cx, cy - h2, cz) if axis == 'y' else (cx, cy, cz - h2)
                if sign > 0:
                    self.tri(ct, P(a1, h2), P(a0, h2)); self.tri(cb, P(a0, -h2), P(a1, -h2))
                else:
                    self.tri(ct, P(a0, h2), P(a1, h2)); self.tri(cb, P(a1, -h2), P(a0, -h2))

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = math.sqrt(sum(x * x for x in a)) or 1.0
    return tuple(x / l for x in a)

# ---------------------------------------------------------------- the texture
def hull_texture(n=128):
    """Orange panels with dark seams, rivets and a hazard stripe: RGBA rows."""
    base, seam, dark, yellow = (222, 112, 38), (70, 40, 25), (35, 32, 30), (240, 190, 40)
    rows = []
    for y in range(n):
        row = bytearray()
        for x in range(n):
            c = base
            shade = 1.0 - 0.10 * (y / n)  # a little darker towards the bottom
            if x < 3 or y < 3 or x >= n - 3 or y >= n - 3 or x in (n // 2 - 1, n // 2):
                c = seam
            elif 3 <= y - 3 < 10 and (x // 2 + y // 2) % 2 == 0:
                c = seam
            if n * 0.68 < y < n * 0.80 and 3 <= x < n - 3:
                c = yellow if ((x + y) // 8) % 2 == 0 else dark
            for rx in (8, n // 2 - 9, n // 2 + 8, n - 9):
                for ry in (8, n - 9):
                    if (x - rx) ** 2 + (y - ry) ** 2 <= 4:
                        c = (110, 70, 40)
            row += bytes((int(c[0] * shade), int(c[1] * shade), int(c[2] * shade), 255))
        rows.append(bytes(row))
    return n, rows

def png(width, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, len(rows), 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

# ---------------------------------------------------------------- the model
ARM = (0.50, 0.42)  # motor positions: x = +-0.50, z = +-0.42
MOTOR_TOP = 0.11

def build():
    meshes = {}

    body = Mesh()  # textured hull
    body.box((0, 0, 0), (0.46, 0.16, 0.72))
    body.box((0, 0.11, -0.04), (0.32, 0.06, 0.48), uv_scale=(0.5, 0.5))
    meshes['Body'] = [(body, 'Hull')]

    frame = Mesh()  # arms and motors: dark metal
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * ARM[0], sz * ARM[1]
            mx, mz = x * 0.5, z * 0.5
            length = math.hypot(x, z) * 0.95
            frame.box((mx, 0.0, mz), (0.07, 0.05, length), yaw=math.atan2(x, z))
            frame.cylinder((x, 0.03, z), 0.075, 0.12, segments=10)
    eye_ring = Mesh()
    eye_ring.cylinder((0, -0.01, 0.37), 0.085, 0.04, segments=12, axis='z')
    meshes['Frame'] = [(frame, 'Metal'), (eye_ring, 'Metal')]

    leds_red, leds_green = Mesh(), Mesh()  # navigation lights under the motors
    for sz in (-1, 1):
        leds_red.box((ARM[0], -0.05, sz * ARM[1]), (0.05, 0.03, 0.05))     # left (+X) = red
        leds_green.box((-ARM[0], -0.05, sz * ARM[1]), (0.05, 0.03, 0.05))  # right (-X) = green
    meshes['Lights'] = [(leds_red, 'LightRed'), (leds_green, 'LightGreen')]

    eye = Mesh()  # the glowing camera eye on the front
    eye.cylinder((0, -0.01, 0.395), 0.06, 0.02, segments=12, axis='z')
    meshes['Eye'] = [(eye, 'Eye')]

    rotor = Mesh()  # hub + two blades, centred on the node
    rotor.cylinder((0, 0.015, 0), 0.03, 0.03, segments=8)
    rotor.box((0, 0.02, 0), (0.05, 0.008, 0.52))
    rotor.box((0, 0.02, 0), (0.52, 0.008, 0.05))
    meshes['Rotor'] = [(rotor, 'Rotor')]

    materials = {
        'Hull': dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0.0, roughnessFactor=0.55)),
        'Metal': dict(pbrMetallicRoughness=dict(baseColorFactor=[0.06, 0.065, 0.075, 1], metallicFactor=0.6, roughnessFactor=0.4)),
        'Rotor': dict(pbrMetallicRoughness=dict(baseColorFactor=[0.035, 0.035, 0.04, 1], metallicFactor=0.0, roughnessFactor=0.7)),
        'LightRed': dict(pbrMetallicRoughness=dict(baseColorFactor=[1, 0.05, 0.03, 1], metallicFactor=0.0, roughnessFactor=0.3),
                         emissiveFactor=[1.0, 0.06, 0.03]),
        'LightGreen': dict(pbrMetallicRoughness=dict(baseColorFactor=[0.05, 1, 0.2, 1], metallicFactor=0.0, roughnessFactor=0.3),
                           emissiveFactor=[0.08, 1.0, 0.25]),
        'Eye': dict(pbrMetallicRoughness=dict(baseColorFactor=[0.25, 0.02, 0.01, 1], metallicFactor=0.0, roughnessFactor=0.2),
                    emissiveFactor=[1.0, 0.15, 0.05]),
    }
    nodes = [('Body', 'Body', (0, 0, 0)), ('Frame', 'Frame', (0, 0, 0)), ('Lights', 'Lights', (0, 0, 0)),
             ('Eye', 'Eye', (0, 0, 0))]
    for name, sx, sz in (('RotorFL', 1, 1), ('RotorFR', -1, 1), ('RotorBL', 1, -1), ('RotorBR', -1, -1)):
        nodes.append((name, 'Rotor', (sx * ARM[0], MOTOR_TOP - 0.02, sz * ARM[1])))
    return meshes, materials, nodes

def write_glb(path):
    meshes, materials, nodes = build()
    blob = bytearray()
    views, accessors = [], []

    def add_view(data, target=None):
        while len(blob) % 4: blob.append(0)
        v = dict(buffer=0, byteOffset=len(blob), byteLength=len(data))
        if target: v['target'] = target
        blob.extend(data); views.append(v)
        return len(views) - 1

    def add_accessor(values, kind):
        if kind == 'SCALAR':
            data = struct.pack('<%dI' % len(values), *values)
            acc = dict(componentType=5125, count=len(values), type='SCALAR')
            acc['bufferView'] = add_view(data, 34963)
        else:
            comps = len(values[0])
            data = b''.join(struct.pack('<%df' % comps, *v) for v in values)
            acc = dict(componentType=5126, count=len(values), type='VEC%d' % comps)
            acc['bufferView'] = add_view(data, 34962)
            if kind == 'POSITION':
                acc['min'] = [min(v[i] for v in values) for i in range(3)]
                acc['max'] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(acc)
        return len(accessors) - 1

    mat_names = list(materials)
    gl_meshes, mesh_index = [], {}
    for name, prims in meshes.items():
        out = []
        for m, mat in prims:
            out.append(dict(attributes=dict(POSITION=add_accessor(m.pos, 'POSITION'),
                                             NORMAL=add_accessor(m.nrm, 'NORMAL'),
                                             TEXCOORD_0=add_accessor(m.uv, 'TEXCOORD_0')),
                            indices=add_accessor(m.idx, 'SCALAR'), material=mat_names.index(mat)))
        mesh_index[name] = len(gl_meshes)
        gl_meshes.append(dict(name=name, primitives=out))

    size, rows = hull_texture()
    image_view = add_view(png(size, rows))

    gl_nodes = [dict(name='Drone', children=list(range(1, len(nodes) + 1)))]
    for name, mesh, t in nodes:
        n = dict(name=name, mesh=mesh_index[mesh])
        if any(t): n['translation'] = list(t)
        gl_nodes.append(n)

    doc = dict(
        asset=dict(version='2.0', generator='make_drone.py (Game3D)', copyright='CC0 1.0: public domain'),
        scene=0, scenes=[dict(name='Scene', nodes=[0])], nodes=gl_nodes, meshes=gl_meshes,
        materials=[dict(name=k, **v) for k, v in materials.items()],
        textures=[dict(source=0, sampler=0)],
        samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
        images=[dict(name='DroneHull', bufferView=image_view, mimeType='image/png')],
        accessors=accessors, bufferViews=views, buffers=[dict(byteLength=0)])
    while len(blob) % 4: blob.append(0)
    doc['buffers'][0]['byteLength'] = len(blob)
    js = json.dumps(doc, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
    glb += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(blob), 0x004E4942) + bytes(blob)
    with open(path, 'wb') as f:
        f.write(glb)
    tris = sum(len(m.idx) // 3 for prims in meshes.values() for m, _ in prims)
    print('wrote %s: %d bytes, %d triangles (one rotor counted once)' % (path, len(glb), tris))

if __name__ == '__main__':
    write_glb(sys.argv[1] if len(sys.argv) > 1 else 'drone.glb')
