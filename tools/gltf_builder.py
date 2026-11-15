"""A tiny glTF 2.0 (.glb) writer for low-poly models made from code. Standard library only.

Used by make_gunner.py. Build meshes from boxes and cylinders (flat shaded), arrange them
in a node hierarchy, add keyframed animations, and write one .glb:

    b = Builder()
    m = b.material('Metal', [0.1, 0.1, 0.1, 1], metallic=0.6, roughness=0.4)
    mesh = Mesh(); mesh.box((0, 0, 0), (1, 1, 1))
    root = b.node('Crate', mesh=b.mesh('Crate', [(mesh, m)]))
    b.animation('Spin', [(root, 'rotation', [0, 1], [quat_y(0), quat_y(180)])])
    b.write('crate.glb')

Skinned meshes too: give a Mesh per-vertex joints and weights, add a skin (its joints
and their bind pose) with Builder.skin, and put the mesh on a node with skin=.

Units are metres, +Y is up and the front faces +Z (the framework's conventions).
Rotations are quaternions as (x, y, z, w), as glTF stores them.
"""
import json, math, struct

# ---------------------------------------------------------------- vector and quaternion maths
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def scale(a, s): return tuple(x * s for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def length(a): return math.sqrt(dot(a, a))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = length(a) or 1.0
    return tuple(x / l for x in a)

QUAT_IDENTITY = (0.0, 0.0, 0.0, 1.0)

def quat_axis(axis, degrees):
    h = math.radians(degrees) / 2
    s = math.sin(h)
    x, y, z = norm(axis)
    return (x * s, y * s, z * s, math.cos(h))

def quat_x(d): return quat_axis((1, 0, 0), d)
def quat_y(d): return quat_axis((0, 1, 0), d)
def quat_z(d): return quat_axis((0, 0, 1), d)

def quat_mul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)

def quat_conj(q): return (-q[0], -q[1], -q[2], q[3])

def quat_rotate(q, v):
    p = quat_mul(quat_mul(q, (v[0], v[1], v[2], 0.0)), quat_conj(q))
    return p[:3]

def quat_euler(x=0.0, y=0.0, z=0.0):
    """Rotation about Y, then X, then Z (in degrees), the order a turning, nodding joint wants."""
    return quat_mul(quat_y(y), quat_mul(quat_x(x), quat_z(z)))

def quat_norm(q):
    l = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / l for c in q)

# Rigid transforms (rotation, then translation) as (quaternion, translation) pairs.
def xform_mul(a, b):
    """a after b: what b's frame is, seen from a's parent frame."""
    return quat_mul(a[0], b[0]), add(a[1], quat_rotate(a[0], b[1]))

def xform_inverse(a):
    q = quat_conj(a[0])
    return q, scale(quat_rotate(q, a[1]), -1.0)

def xform_point(a, p): return add(a[1], quat_rotate(a[0], p))

def xform_matrix(a):
    """The 4x4 matrix, column-major (16 floats), as glTF stores matrices."""
    x, y, z, w = quat_norm(a[0])
    t = a[1]
    return [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0,
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0,
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0,
            t[0], t[1], t[2], 1.0]

# ---------------------------------------------------------------- flat-shaded geometry
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []
        # For skinned meshes: per vertex, up to four joints (indices into the skin's joint
        # list) and how much each moves the vertex (weights adding up to 1).
        self.joints, self.weights = [], []

    def skinned(self):
        return len(self.joints) == len(self.pos) and len(self.pos) > 0

    def append(self, other, xform=None, joint=None):
        """Adds another mesh's triangles, moved by a rigid transform; with `joint`, every
        added vertex follows that joint alone (weight 1)."""
        base = len(self.pos)
        for i, (p, n, t) in enumerate(zip(other.pos, other.nrm, other.uv)):
            self.pos.append(xform_point(xform, p) if xform else p)
            self.nrm.append(quat_rotate(xform[0], n) if xform else n)
            self.uv.append(t)
            if joint is not None:
                self.joints.append((joint, 0, 0, 0)); self.weights.append((1.0, 0.0, 0.0, 0.0))
            elif other.skinned():
                self.joints.append(other.joints[i]); self.weights.append(other.weights[i])
        self.idx += [base + i for i in other.idx]

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

    def box(self, centre, size, rotation=QUAT_IDENTITY, taper=1.0):
        """A box; `taper` < 1 narrows its top (+Y) face in X and Z (a tapered limb or torso)."""
        hx, hy, hz = (s / 2 for s in size)
        def P(x, y, z):
            if y > 0:
                x, z = x * taper, z * taper
            return add(centre, quat_rotate(rotation, (x, y, z)))
        v = [P(x, y, z) for x in (-hx, hx) for y in (-hy, hy) for z in (-hz, hz)]
        # v index = xi*4 + yi*2 + zi
        self.quad(v[1], v[5], v[7], v[3])  # +Z (front)
        self.quad(v[4], v[0], v[2], v[6])  # -Z
        self.quad(v[5], v[4], v[6], v[7])  # +X
        self.quad(v[0], v[1], v[3], v[2])  # -X
        self.quad(v[3], v[7], v[6], v[2])  # +Y
        self.quad(v[0], v[4], v[5], v[1])  # -Y

    def cylinder(self, centre, radius, height, segments=10, axis='y'):
        """Flat-shaded prism with caps; axis 'y' (upright) or 'z' (lying along the front axis)."""
        cx, cy, cz = centre
        def P(a, h):
            u, w = math.cos(a) * radius, math.sin(a) * radius
            return (cx + u, cy + h, cz + w) if axis == 'y' else (cx + u, cy + w, cz + h)
        h2 = height / 2
        ct = (cx, cy + h2, cz) if axis == 'y' else (cx, cy, cz + h2)
        cb = (cx, cy - h2, cz) if axis == 'y' else (cx, cy, cz - h2)
        for i in range(segments):
            a0 = 2 * math.pi * i / segments; a1 = 2 * math.pi * (i + 1) / segments
            if axis == 'y':
                self.quad(P(a1, -h2), P(a0, -h2), P(a0, h2), P(a1, h2))
                self.tri(ct, P(a1, h2), P(a0, h2)); self.tri(cb, P(a0, -h2), P(a1, -h2))
            else:
                self.quad(P(a0, -h2), P(a1, -h2), P(a1, h2), P(a0, h2))
                self.tri(ct, P(a0, h2), P(a1, h2)); self.tri(cb, P(a1, -h2), P(a0, -h2))

# ---------------------------------------------------------------- the file
class Builder:
    def __init__(self, generator='gltf_builder.py (Game3D)'):
        self.generator = generator
        self.materials, self.meshes, self.nodes, self.animations, self.skins = [], [], [], [], []

    def material(self, name, color, metallic=0.0, roughness=0.6, emissive=None):
        """Returns the material's index. Colours are linear RGBA."""
        m = dict(name=name, pbrMetallicRoughness=dict(baseColorFactor=list(color),
                 metallicFactor=metallic, roughnessFactor=roughness))
        if emissive:
            m['emissiveFactor'] = list(emissive)
        self.materials.append(m)
        return len(self.materials) - 1

    def mesh(self, name, parts):
        """parts: [(Mesh, material index), ...]. Returns the mesh's index."""
        self.meshes.append((name, parts))
        return len(self.meshes) - 1

    def node(self, name, parent=None, mesh=None, translation=(0, 0, 0), rotation=QUAT_IDENTITY, skin=None):
        """Returns the node's index. Its transform is relative to its parent. A node with a
        skin draws its (skinned) mesh deformed by that skin's joints."""
        self.nodes.append(dict(name=name, parent=parent, mesh=mesh, skin=skin,
                               translation=tuple(translation), rotation=tuple(rotation)))
        return len(self.nodes) - 1

    def skin(self, name, joints, bind_poses):
        """A skin (Blender: an armature) moving a mesh by `joints` (node indices). bind_poses
        are each joint's (rotation, translation) in model space in the pose the mesh was made
        in: the bind pose. Returns the skin's index, for node(..., skin=)."""
        self.skins.append((name, list(joints), [xform_matrix(xform_inverse(p)) for p in bind_poses]))
        return len(self.skins) - 1

    def animation(self, name, channels):
        """channels: [(node, 'translation' | 'rotation', times, values), ...] (linear keys)."""
        self.animations.append((name, channels))

    def write(self, path, copyright='CC0 1.0: public domain'):
        blob = bytearray()
        views, accessors = [], []

        def add_view(data, target=None):
            while len(blob) % 4: blob.append(0)
            v = dict(buffer=0, byteOffset=len(blob), byteLength=len(data))
            if target: v['target'] = target
            blob.extend(data); views.append(v)
            return len(views) - 1

        def add_floats(values, comps, target=None, bounds=False):
            flat = [c for v in values for c in (v if comps > 1 else (v,))]
            acc = dict(bufferView=add_view(struct.pack('<%df' % len(flat), *flat), target),
                       componentType=5126, count=len(values), type='SCALAR' if comps == 1 else 'VEC%d' % comps)
            if bounds:
                cols = [[v] if comps == 1 else list(v) for v in values]
                acc['min'] = [min(c[i] for c in cols) for i in range(comps)]
                acc['max'] = [max(c[i] for c in cols) for i in range(comps)]
            accessors.append(acc)
            return len(accessors) - 1

        def add_joints(values):
            flat = [c for v in values for c in v]
            accessors.append(dict(bufferView=add_view(struct.pack('<%dB' % len(flat), *flat), 34962),
                                  componentType=5121, count=len(values), type='VEC4'))
            return len(accessors) - 1

        def add_indices(values):
            accessors.append(dict(bufferView=add_view(struct.pack('<%dI' % len(values), *values), 34963),
                                  componentType=5125, count=len(values), type='SCALAR'))
            return len(accessors) - 1

        gl_meshes = []
        for name, parts in self.meshes:
            prims = []
            for m, mat in parts:
                attributes = dict(POSITION=add_floats(m.pos, 3, 34962, bounds=True),
                                  NORMAL=add_floats(m.nrm, 3, 34962),
                                  TEXCOORD_0=add_floats(m.uv, 2, 34962))
                if m.skinned():
                    attributes['JOINTS_0'] = add_joints(m.joints)
                    attributes['WEIGHTS_0'] = add_floats(m.weights, 4, 34962)
                prims.append(dict(attributes=attributes, indices=add_indices(m.idx), material=mat))
            gl_meshes.append(dict(name=name, primitives=prims))

        gl_nodes = []
        for i, n in enumerate(self.nodes):
            g = dict(name=n['name'])
            if n['mesh'] is not None: g['mesh'] = n['mesh']
            if n['skin'] is not None: g['skin'] = n['skin']
            if any(abs(c) > 1e-9 for c in n['translation']): g['translation'] = [round(c, 6) for c in n['translation']]
            if n['rotation'] != QUAT_IDENTITY: g['rotation'] = [round(c, 6) for c in quat_norm(n['rotation'])]
            children = [j for j, c in enumerate(self.nodes) if c['parent'] == i]
            if children: g['children'] = children
            gl_nodes.append(g)

        gl_anims = []
        for name, channels in self.animations:
            samplers, chans = [], []
            for node, prop, times, values in channels:
                comps = 4 if prop == "rotation" else 3
                if prop == 'rotation':
                    values = [quat_norm(v) for v in values]
                samplers.append(dict(input=add_floats(times, 1, bounds=True),
                                     output=add_floats(values, comps), interpolation='LINEAR'))
                chans.append(dict(sampler=len(samplers) - 1, target=dict(node=node, path=prop)))
            gl_anims.append(dict(name=name, samplers=samplers, channels=chans))

        doc = dict(asset=dict(version='2.0', generator=self.generator, copyright=copyright),
                   scene=0, scenes=[dict(name='Scene', nodes=[i for i, n in enumerate(self.nodes) if n['parent'] is None])],
                   nodes=gl_nodes, meshes=gl_meshes, materials=self.materials,
                   accessors=accessors, bufferViews=views, buffers=[dict(byteLength=0)])
        if gl_anims:
            doc['animations'] = gl_anims
        if self.skins:
            doc['skins'] = []
            for name, joints, inverse_bind in self.skins:
                acc = add_floats([tuple(m) for m in inverse_bind], 16)
                accessors[acc]['type'] = 'MAT4'
                doc['skins'].append(dict(name=name, joints=joints, inverseBindMatrices=acc))
        while len(blob) % 4: blob.append(0)
        doc['buffers'][0]['byteLength'] = len(blob)
        js = json.dumps(doc, separators=(',', ':')).encode()
        js += b' ' * (-len(js) % 4)
        glb = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
        glb += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(blob), 0x004E4942) + bytes(blob)
        with open(path, 'wb') as f:
            f.write(glb)
        tris = sum(len(m.idx) // 3 for _, parts in self.meshes for m, _ in parts)
        print('wrote %s: %d bytes, %d triangles, %d nodes, %d animations%s'
              % (path, len(glb), tris, len(self.nodes), len(self.animations),
                 ', %d joints' % sum(len(j) for _, j, _ in self.skins) if self.skins else ''))
