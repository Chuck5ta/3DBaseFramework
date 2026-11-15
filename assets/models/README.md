# Models

| File | Description |
|---|---|
| `drone.glb` | Low-poly quadcopter, about 1.3 m across, 430 triangles, one 128×128 texture (the circling drone in the demo) |
| `gunner.glb` | Low-poly soldier, 1.9 m, 296 triangles, built from rigid parts, with five animations: `Idle` (4 s, loops), `Fire` (0.45 s), `Load` (1.5 s), `Walk` (1 s, loops; made for about 1.35 m/s) and `Run` (0.6 s, loops; about 3.4 m/s) |
| `soldier.glb` | The same soldier **skinned**: one mesh, `Body` (1128 triangles), bent by a skin whose joints are the gunner's nodes, so elbows, knees, waist and neck bend smoothly. Same node names (root `Soldier`), the same five animations and the same `Socket_Hand_R` |
| `rifle.glb` | His rifle, 104 triangles; attached in code at the soldiers' `Socket_Hand_R`. Its `Muzzle` node marks where the flash goes |
| `tree.glb` | Low-poly pine, 4.2 m tall, 148 triangles: one node, `Tree`, with two parts (`Bark` and `Needles`). The demo's forest draws 800 copies of it with instancing |

`drone.glb` was generated for this project by `tools/make_drone.py`, `gunner.glb`,
`soldier.glb` and `rifle.glb` by `tools/make_gunner.py`, and `tree.glb` by
`tools/make_tree.py` (plain Python, no installs).
They're released into the public domain (CC0): use, change and ship them freely. Import
them into Blender (File > Import > glTF 2.0) to use them as a starting point for your own.

The drone's nodes, as named in Blender's Outliner:

| Node | What it is |
|---|---|
| `Drone` | The root (an empty) |
| `Body`, `Frame`, `Lights`, `Eye` | The hull, the arms and motors, the red/green navigation lights, the glowing camera eye |
| `RotorFL`, `RotorFR`, `RotorBL`, `RotorBR` | The rotors: front/back, left/right. They share one mesh and spin about +Y |

The gunner's nodes (each turns about its joint; `_L` is his left, +X):

```
Gunner > Hips > Torso > Head > Helmet
                      > UpperArm_L > Forearm_L > Hand_L
                      > UpperArm_R > Forearm_R > Hand_R > Socket_Hand_R
              > Thigh_L > Shin_L
              > Thigh_R > Shin_R
```

In `soldier.glb` the same nodes are the skin's joints (Blender: bones) and hold no
meshes; `Body` (a child of `Soldier`) is the skinned mesh. It was made in a neutral *bind
pose* (standing straight, arms hanging 12° out from the body); the nodes' own transforms
are the rifle-ready rest pose, as in `gunner.glb`.

Units are metres, +Y is up and the front faces +Z (a model's left is +X).

Load models with `render3d::Model::load(app.gpu(), core::assetPath("models/<file>"))`.
Both `.glb` (everything in one file) and `.gltf` (with its `.bin` and images beside it) work.
