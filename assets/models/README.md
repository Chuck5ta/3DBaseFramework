# Models

| File | Description |
|---|---|
| `drone.glb` | Low-poly quadcopter, about 1.3 m across, 430 triangles, one 128×128 texture (the circling drone in the demo) |
| `gunner.glb` | Low-poly soldier, 1.9 m, 296 triangles, built from rigid parts, with three animations: `Idle` (4 s, loops), `Fire` (0.45 s), `Load` (1.5 s) |
| `rifle.glb` | His rifle, 104 triangles; attached in code at the gunner's `Socket_Hand_R`. Its `Muzzle` node marks where the flash goes |

`drone.glb` was generated for this project by `tools/make_drone.py`, and `gunner.glb` and
`rifle.glb` by `tools/make_gunner.py` (plain Python, no installs). They're released into
the public domain (CC0): use, change and ship them freely. Import them into Blender
(File > Import > glTF 2.0) to use them as a starting point for your own.

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

Units are metres, +Y is up and the front faces +Z (a model's left is +X).

Load models with `render3d::Model::load(app.gpu(), core::assetPath("models/<file>"))`.
Both `.glb` (everything in one file) and `.gltf` (with its `.bin` and images beside it) work.
