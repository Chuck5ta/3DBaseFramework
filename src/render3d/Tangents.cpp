#include "render3d/Tangents.h"

#include <mikktspace.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <unordered_map>

namespace render3d {

namespace {

struct Job {
    std::vector<Vertex>* vertices;
    const std::vector<uint32_t>* indices;
    size_t firstIndex;
    uint32_t base;
    std::vector<glm::vec4> corners; // the tangent MikkTSpace gives each corner of each triangle

    const Vertex& vertex(int face, int corner) const
    {
        return (*vertices)[base + (*indices)[firstIndex + size_t(face) * 3 + size_t(corner)]];
    }
};

Job& job(const SMikkTSpaceContext* c)
{
    return *static_cast<Job*>(c->m_pUserData);
}

int numFaces(const SMikkTSpaceContext* c)
{
    return int(job(c).corners.size() / 3);
}

int numVerticesOfFace(const SMikkTSpaceContext*, const int)
{
    return 3;
}

void getPosition(const SMikkTSpaceContext* c, float out[], const int face, const int corner)
{
    const glm::vec3& p = job(c).vertex(face, corner).position;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void getNormal(const SMikkTSpaceContext* c, float out[], const int face, const int corner)
{
    const glm::vec3& n = job(c).vertex(face, corner).normal;
    out[0] = n.x;
    out[1] = n.y;
    out[2] = n.z;
}

void getTexCoord(const SMikkTSpaceContext* c, float out[], const int face, const int corner)
{
    // Our UVs, like glTF's, start at the top of the image; MikkTSpace (like Blender) counts
    // v upwards. Flipped, the bitangent points up the image: a normal map's green channel
    // is "up" (the OpenGL / glTF / Blender convention).
    const glm::vec2& uv = job(c).vertex(face, corner).uv;
    out[0] = uv.x;
    out[1] = 1.0f - uv.y;
}

void setTSpaceBasic(const SMikkTSpaceContext* c, const float tangent[], const float sign, const int face,
                    const int corner)
{
    job(c).corners[size_t(face) * 3 + size_t(corner)] = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

bool sameTangent(const glm::vec4& a, const glm::vec4& b)
{
    // MikkTSpace gives corners that share a tangent the very same numbers; a little slack
    // for rounding all the same.
    return a.w == b.w && glm::all(glm::lessThanEqual(glm::abs(glm::vec3(a) - glm::vec3(b)), glm::vec3(1e-5f)));
}

} // namespace

bool generateTangents(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, size_t firstIndex,
                      size_t indexCount, uint32_t base, const std::function<void(uint32_t)>& onCopy)
{
    const size_t triangles = indexCount / 3;
    if (triangles == 0)
        return false;

    Job j{&vertices, &indices, firstIndex, base, std::vector<glm::vec4>(triangles * 3, glm::vec4(0.0f))};
    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = numFaces;
    callbacks.m_getNumVerticesOfFace = numVerticesOfFace;
    callbacks.m_getPosition = getPosition;
    callbacks.m_getNormal = getNormal;
    callbacks.m_getTexCoord = getTexCoord;
    callbacks.m_setTSpaceBasic = setTSpaceBasic;
    SMikkTSpaceContext context{&callbacks, &j};
    if (!genTangSpaceDefault(&context))
        return false;

    // MikkTSpace answers per corner. Give each vertex the tangent of its first corner; a
    // corner that needs a different one gets its own copy of the vertex (one per distinct
    // tangent), as MikkTSpace asks: averaging them would be wrong.
    std::vector<bool> done(vertices.size(), false);
    std::unordered_map<uint32_t, std::vector<uint32_t>> copies; // vertex -> its copies
    for (size_t k = 0; k < triangles * 3; ++k) {
        uint32_t& index = indices[firstIndex + k];
        const uint32_t v = base + index;
        const glm::vec4& t = j.corners[k];
        if (!done[v]) {
            vertices[v].tangent = t;
            done[v] = true;
            continue;
        }
        if (sameTangent(vertices[v].tangent, t))
            continue;
        uint32_t found = UINT32_MAX;
        for (uint32_t c : copies[v])
            if (sameTangent(vertices[c].tangent, t)) {
                found = c;
                break;
            }
        if (found == UINT32_MAX) {
            found = uint32_t(vertices.size());
            Vertex copy = vertices[v];
            copy.tangent = t;
            vertices.push_back(copy);
            done.push_back(true);
            copies[v].push_back(found);
            if (onCopy)
                onCopy(v);
        }
        index = found - base;
    }
    return true;
}

void generateTangents(MeshData& mesh)
{
    generateTangents(mesh.vertices, mesh.indices, 0, mesh.indices.size());
}

} // namespace render3d
