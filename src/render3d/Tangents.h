#pragma once

// Tangents for normal maps.
//
// A normal map stores, per texel, which way the surface faces, relative to the surface
// itself: x along the texture's u (its tangent), y along v (its bitangent), z straight out
// (the normal). To use it, each vertex needs that frame: its normal, and a tangent that
// says which way u runs across the surface (Vertex::tangent, w = +1 or -1 for which way
// v runs, so mirrored UVs work).
//
// generateTangents works them out with MikkTSpace, the method Blender, glTF and most
// bakers use: a normal map baked in Blender (or by CharacterCreator) lines up exactly.
// primitives::cube/plane/sphere already have them; glTF models get them at load for every
// part with a normal map (unless the file has its own). Call it yourself for your own
// MeshData before building the Mesh, if it gets a normal map:
//
//     render3d::MeshData rock = makeRock();
//     render3d::generateTangents(rock);
//     rockMesh_ = render3d::Mesh(gpu, rock, "rock");

#include "render3d/Mesh.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace render3d {

// Fills in the tangents of every triangle (needs normals and UVs).
void generateTangents(MeshData& mesh);

// The same for some of the triangles of shared arrays (a model's part): indices
// [firstIndex, firstIndex + indexCount), each counted from vertex `base`. Where two
// triangles share a vertex but need different tangents (where UVs are mirrored or cut),
// the vertex is copied to the end of `vertices` and that corner's index changed; onCopy(i)
// is told which vertex was copied, so the caller can copy data it keeps alongside (a
// model's joints and weights). Returns false if there was nothing to do.
bool generateTangents(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, size_t firstIndex,
                      size_t indexCount, uint32_t base = 0, const std::function<void(uint32_t)>& onCopy = {});

} // namespace render3d
