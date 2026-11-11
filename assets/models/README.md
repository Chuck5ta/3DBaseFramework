# Models

| File | Description |
|---|---|
| `drone.glb` | Low-poly quadcopter, about 1.3 m across, 430 triangles, one 128×128 texture (the circling drone in the demo) |

`drone.glb` was generated for this project by `tools/make_drone.py` (plain Python, no
installs). It's released into the public domain (CC0): use, change and ship it freely.
Import it into Blender (File > Import > glTF 2.0) to use it as a starting point for your own.

Its nodes, as named in Blender's Outliner:

| Node | What it is |
|---|---|
| `Drone` | The root (an empty) |
| `Body`, `Frame`, `Lights`, `Eye` | The hull, the arms and motors, the red/green navigation lights, the glowing camera eye |
| `RotorFL`, `RotorFR`, `RotorBL`, `RotorBR` | The rotors: front/back, left/right. They share one mesh and spin about +Y |

Units are metres, +Y is up and the front faces +Z (the drone's left is +X).

Load models with `render3d::Model::load(app.gpu(), core::assetPath("models/<file>"))`.
Both `.glb` (everything in one file) and `.gltf` (with its `.bin` and images beside it) work.
