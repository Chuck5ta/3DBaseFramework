#!/usr/bin/env python3
"""Writes gunner.glb (a low-poly soldier with Idle, Fire, Load, Walk and Run animations) and
rifle.glb (the rifle he holds, attached in code at his Socket_Hand_R). CC0.

Usage: python tools/make_gunner.py assets/models

Plain Python (standard library only, plus gltf_builder.py beside this file). Units are
metres, +Y up, the front faces +Z. The soldier is built from separate rigid parts, each
a node turning about its joint, the way you'd build one in Blender from separate objects
(or objects parented to an armature's bones):

  Gunner (root, at the feet)
    Hips > Torso > Head > Helmet
                 > UpperArm_L > Forearm_L > Hand_L
                 > UpperArm_R > Forearm_R > Hand_R > Socket_Hand_R (an empty: the rifle goes here)
         > Thigh_L > Shin_L
         > Thigh_R > Shin_R

The arm angles that put the hands on the rifle are worked out by a small solver
(inverse kinematics), so the rifle's shape can change without redoing the poses by hand.
"""
import math, os, sys
sys.dont_write_bytecode = True  # no __pycache__ folder in tools/
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gltf_builder import (Builder, Mesh, QUAT_IDENTITY, add, sub, length, quat_euler, quat_mul, quat_conj,
                          quat_rotate, quat_x, quat_y)

# ---------------------------------------------------------------- proportions (metres)
HIPS_Y = 0.95
TORSO_Y = 0.08                      # waist joint above the hips
SHOULDER = (0.23, 0.47, 0.0)        # left shoulder in the torso's frame (right is mirrored)
UPPER_ARM, FOREARM, PALM = 0.32, 0.30, 0.06
TORSO_LEAN = 5.0                    # degrees forward in the ready stance

# Where the rifle sits, in the torso's frame: its grip, and level in the world.
RIFLE_GRIP = (-0.06, 0.27, 0.30)
RIFLE_ROT = quat_x(-TORSO_LEAN)     # cancels the torso's lean, so the barrel is level
FOREGRIP = (0.0, -0.01, 0.22)       # on the rifle, in its own frame
MAGAZINE = (0.0, -0.10, 0.10)

def rifle_point(p):
    """A point on the rifle, in the torso's frame."""
    return add(RIFLE_GRIP, quat_rotate(RIFLE_ROT, p))

# ---------------------------------------------------------------- inverse kinematics
def arm_palm(shoulder, angles):
    """Palm position (torso frame) for shoulder angles (x, y, z) and elbow bend, in degrees."""
    sx, sy, sz, e = angles
    qs = quat_euler(sx, sy, sz)
    qe = quat_x(e)
    elbow = add(shoulder, quat_rotate(qs, (0, -UPPER_ARM, 0)))
    q = quat_mul(qs, qe)
    return add(elbow, quat_rotate(q, (0, -(FOREARM + PALM), 0))), elbow

def solve_arm(shoulder, target, start, outward):
    """Finds shoulder angles and elbow bend that put the palm on `target` (coordinate descent)."""
    def cost(a):
        palm, elbow = arm_palm(shoulder, a)
        c = length(sub(palm, target)) ** 2
        c += 0.0004 * max(0.0, elbow[1] - shoulder[1] + 0.05) ** 2 * 100   # keep the elbow low
        c += 0.0004 * max(0.0, (shoulder[0] - elbow[0]) * outward) ** 2 * 50  # and not tucked in
        c += 1e-6 * (a[2] ** 2)                                             # little twist
        if not -150.0 <= a[3] <= 0.0:                                       # elbows bend one way
            c += 1.0
        return c
    a = list(start)
    step = 20.0
    while step > 0.01:
        improved = False
        for i in range(4):
            for d in (step, -step):
                b = a[:]; b[i] += d
                if cost(b) < cost(a):
                    a, improved = b, True
        if not improved:
            step /= 2
    palm, _ = arm_palm(shoulder, a)
    miss = length(sub(palm, target))
    if miss > 0.01:
        print('warning: a hand misses its target by %.0f mm' % (miss * 1000))
    return a

LEFT = SHOULDER
RIGHT = (-SHOULDER[0], SHOULDER[1], SHOULDER[2])

def left_arm(target):
    return solve_arm(LEFT, target, (-60, 0, 0, -40), outward=-1)

def arm_rotations(a):
    return quat_euler(a[0], a[1], a[2]), quat_x(a[3])

# ---------------------------------------------------------------- the soldier
def build_gunner(path):
    b = Builder('make_gunner.py (Game3D)')
    uniform = b.material('Uniform', [0.20, 0.24, 0.12, 1], roughness=0.85)
    webbing = b.material('Webbing', [0.09, 0.07, 0.05, 1], roughness=0.8)
    skin = b.material('Skin', [0.62, 0.40, 0.28, 1], roughness=0.7)
    helmet = b.material('Helmet', [0.13, 0.16, 0.08, 1], roughness=0.6)
    boots = b.material('Boots', [0.04, 0.035, 0.03, 1], roughness=0.5)

    def part(name, parts):
        return b.mesh(name, parts)

    root = b.node('Gunner')

    m = Mesh(); m.box((0, 0, 0), (0.34, 0.17, 0.22))
    w = Mesh(); w.box((0, 0.06, 0), (0.36, 0.05, 0.24))
    for x in (-0.12, 0.12):
        w.box((x, 0.03, 0.13), (0.09, 0.09, 0.05))      # pouches on the belt
    hips_rest = (0, HIPS_Y, 0)
    hips = b.node('Hips', root, part('Hips', [(m, uniform), (w, webbing)]), hips_rest)

    m = Mesh(); m.box((0, 0.26, 0), (0.38, 0.52, 0.23), taper=1.08)
    w = Mesh(); w.box((0, 0.26, 0.118), (0.30, 0.34, 0.02))  # chest rig
    torso_rest = quat_x(TORSO_LEAN)
    torso = b.node('Torso', hips, part('Torso', [(m, uniform), (w, webbing)]), (0, TORSO_Y, 0), torso_rest)

    m = Mesh(); m.cylinder((0, 0.04, 0), 0.055, 0.08, segments=8)
    m.box((0, 0.17, 0.01), (0.19, 0.21, 0.21))
    e = Mesh(); e.box((0, 0.19, 0.117), (0.13, 0.03, 0.01))                  # eyes: one dark band
    head = b.node('Head', torso, part('Head', [(m, skin), (e, boots)]), (0, 0.55, 0))
    m = Mesh(); m.box((0, 0.03, 0), (0.25, 0.10, 0.27), taper=0.8)
    m.box((0, -0.02, 0.005), (0.28, 0.025, 0.30))
    b.node('Helmet', head, part('Helmet', [(m, helmet)]), (0, 0.25, 0))

    # Arms: rest pose = holding the rifle.
    grip_r = rifle_point((0, 0, 0))
    right = solve_arm(RIGHT, grip_r, (-40, 0, 0, -70), outward=1)
    left = left_arm(rifle_point(FOREGRIP))
    arms = {}
    for side, shoulder, angles in (('L', LEFT, left), ('R', RIGHT, right)):
        qs, qe = arm_rotations(angles)
        m = Mesh(); m.box((0, -UPPER_ARM / 2, 0), (0.11, UPPER_ARM, 0.11), taper=1.15)
        upper = b.node('UpperArm_' + side, torso, part('UpperArm_' + side, [(m, uniform)]), shoulder, qs)
        m = Mesh(); m.box((0, -FOREARM / 2, 0), (0.095, FOREARM, 0.095), taper=1.1)
        fore = b.node('Forearm_' + side, upper, part('Forearm_' + side, [(m, uniform)]), (0, -UPPER_ARM, 0), qe)
        m = Mesh(); m.box((0, -0.05, 0), (0.08, 0.10, 0.09))
        hand = b.node('Hand_' + side, fore, part('Hand_' + side, [(m, skin)]), (0, -FOREARM, 0))
        arms[side] = (upper, fore, hand, qs, qe)

    # The socket: at the right palm, turned so an attached rifle (front = +Z) points along
    # RIFLE_ROT in the torso's frame whatever angles the arm ended up with.
    _, _, hand_r, qs, qe = arms['R']
    socket_rot = quat_mul(quat_conj(quat_mul(qs, qe)), RIFLE_ROT)
    b.node('Socket_Hand_R', hand_r, None, (0, -PALM, 0), socket_rot)

    legs = {}
    for side, x, thigh_a, shin_a in (('L', 0.1, -12.0, 12.0), ('R', -0.1, 8.0, 0.0)):
        m = Mesh(); m.box((0, -0.22, 0), (0.15, 0.44, 0.16), taper=1.1)
        thigh = b.node('Thigh_' + side, hips, part('Thigh_' + side, [(m, uniform)]), (x, -0.06, 0), quat_x(thigh_a))
        m = Mesh(); m.box((0, -0.20, 0), (0.13, 0.40, 0.14), taper=1.1)
        bt = Mesh(); bt.box((0, -0.41, 0.035), (0.14, 0.09, 0.25))
        shin = b.node('Shin_' + side, thigh, part('Shin_' + side, [(m, uniform), (bt, boots)]), (0, -0.44, 0), quat_x(shin_a))
        legs[side] = (thigh, shin)

    # ------------------------------------------------------------ animations
    upper_l, fore_l, _, qs_l, qe_l = arms['L']

    # Idle (4 s, loops): breathing, and a look left and right.
    t = [0.0, 1.0, 1.6, 2.6, 3.2, 4.0]
    b.animation('Idle', [
        (torso, 'rotation', [0.0, 2.0, 4.0], [torso_rest, quat_x(TORSO_LEAN + 1.5), torso_rest]),
        (hips, 'translation', [0.0, 2.0, 4.0], [hips_rest, (0, HIPS_Y - 0.008, 0), hips_rest]),
        (head, 'rotation', t, [QUAT_IDENTITY, quat_y(25), quat_y(25), quat_euler(-3, -20), quat_euler(-3, -20),
                               QUAT_IDENTITY]),
    ])

    # Fire (0.45 s, once): the recoil jolts the upper body back; the rifle climbs with it.
    b.animation('Fire', [
        (torso, 'rotation', [0.0, 0.05, 0.18, 0.45],
         [torso_rest, quat_x(TORSO_LEAN - 8), quat_x(TORSO_LEAN - 2), torso_rest]),
        (hips, 'translation', [0.0, 0.05, 0.18, 0.45],
         [hips_rest, (0, HIPS_Y, -0.035), (0, HIPS_Y, -0.012), hips_rest]),
        (head, 'rotation', [0.0, 0.05, 0.18, 0.45], [QUAT_IDENTITY, quat_x(-6), quat_x(-2), QUAT_IDENTITY]),
    ])

    # Load (1.5 s, once): the left hand pulls the magazine, fetches one from the belt,
    # pushes it in, slaps it home and goes back to the foregrip. He looks down at the rifle.
    hips_in_torso = lambda p: quat_rotate(quat_x(-TORSO_LEAN), sub(p, (0, TORSO_Y, 0)))
    pouch = hips_in_torso((0.12, 0.03, 0.17))
    keys = [(0.0, rifle_point(FOREGRIP)),
            (0.30, rifle_point(MAGAZINE)),
            (0.55, pouch),
            (0.85, rifle_point(add(MAGAZINE, (0, -0.06, 0)))),
            (1.00, rifle_point(MAGAZINE)),
            (1.10, rifle_point(add(MAGAZINE, (0, -0.04, 0)))),
            (1.15, rifle_point(MAGAZINE)),
            (1.50, rifle_point(FOREGRIP))]
    times = [k[0] for k in keys]
    solved, guess = [], left
    for _, target in keys:
        guess = solve_arm(LEFT, target, guess, outward=-1)
        solved.append(guess)
    tl = [0.0, 0.3, 1.15, 1.5]
    b.animation('Load', [
        (upper_l, 'rotation', times, [arm_rotations(a)[0] for a in solved]),
        (fore_l, 'rotation', times, [arm_rotations(a)[1] for a in solved]),
        (head, 'rotation', tl, [QUAT_IDENTITY, quat_euler(16, 8), quat_euler(16, 8), QUAT_IDENTITY]),
        (torso, 'rotation', tl, [torso_rest, quat_x(TORSO_LEAN + 4), quat_x(TORSO_LEAN + 4), torso_rest]),
    ])

    # Walk (1.0 s) and Run (0.6 s), both loops: the legs swing, the hips bob, the arms
    # keep the rifle ready. Thigh angles are degrees about X (negative = forward), shins
    # bend the knee (positive). Each leg's cycle: contact in front, passing under the
    # body, pushing off behind, swinging forward with the knee bent; the right leg is
    # half a cycle behind the left.
    def gait(name, length, thigh_fwd, thigh_back, knee_lift, bob, lean):
        times = [length * i / 4 for i in range(5)]
        thigh = [-thigh_fwd, 0.0, thigh_back, -thigh_fwd * 0.4, -thigh_fwd]
        shin = [5.0, 8.0, 15.0, knee_lift, 5.0]
        half = lambda keys: keys[2:4] + keys[0:3]       # the same cycle, half a cycle later
        (thigh_l, shin_l), (thigh_r, shin_r) = legs['L'], legs['R']
        low, high = (0, HIPS_Y - bob, 0), (0, HIPS_Y, 0)
        b.animation(name, [
            (thigh_l, 'rotation', times, [quat_x(a) for a in thigh]),
            (shin_l, 'rotation', times, [quat_x(a) for a in shin]),
            (thigh_r, 'rotation', times, [quat_x(a) for a in half(thigh)]),
            (shin_r, 'rotation', times, [quat_x(a) for a in half(shin)]),
            (hips, 'translation', times, [low, high, low, high, low]),
            (torso, 'rotation', [0.0, length], [quat_x(TORSO_LEAN + lean)] * 2),
        ])

    gait('Walk', 1.0, thigh_fwd=25.0, thigh_back=20.0, knee_lift=55.0, bob=0.03, lean=3.0)
    gait('Run', 0.6, thigh_fwd=40.0, thigh_back=30.0, knee_lift=85.0, bob=0.05, lean=12.0)
    b.write(path)

# ---------------------------------------------------------------- the rifle
def build_rifle(path):
    b = Builder('make_gunner.py (Game3D)')
    metal = b.material('GunMetal', [0.035, 0.035, 0.04, 1], metallic=0.7, roughness=0.35)
    furniture = b.material('Furniture', [0.16, 0.10, 0.05, 1], roughness=0.7)
    m, f = Mesh(), Mesh()
    f.box((0, -0.045, -0.01), (0.045, 0.11, 0.055), rotation=quat_x(18))    # pistol grip
    m.box((0, 0.04, 0.07), (0.065, 0.08, 0.30))                              # receiver
    m.box((0, -0.065, 0.10), (0.042, 0.15, 0.07), rotation=quat_x(-12))     # magazine
    f.box((0, 0.035, 0.30), (0.075, 0.07, 0.20))                             # handguard
    m.cylinder((0, 0.05, 0.48), 0.018, 0.24, segments=8, axis='z')          # barrel
    m.box((0, 0.095, 0.04), (0.025, 0.025, 0.08))                            # sight
    f.box((0, 0.02, -0.15), (0.055, 0.10, 0.20))                             # stock
    rifle = b.node('Rifle', None, b.mesh('Rifle', [(m, metal), (f, furniture)]))
    b.node('Muzzle', rifle, None, (0, 0.05, 0.60))                          # for the flash
    b.write(path)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    build_gunner(os.path.join(out, 'gunner.glb'))
    build_rifle(os.path.join(out, 'rifle.glb'))
