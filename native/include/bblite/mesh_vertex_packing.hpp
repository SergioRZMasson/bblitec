#pragma once
#include <bblite/runtime.hpp>
#include <span>

namespace bbl::pal {

/** Native interleaved storage for the source's separate attribute buffers. */
inline std::vector<ModelVertex>
pack_mesh_vertices(std::span<const float> positions, std::span<const float> normals,
                   std::span<const float> uvs, std::span<const float> uvs2,
                   std::span<const float> tangents, std::span<const float> colors) {
    std::vector<ModelVertex> vertices(positions.size() / 3);
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        auto& vertex = vertices[index];
        vertex.position = {positions[index * 3], positions[index * 3 + 1],
                           positions[index * 3 + 2]};
        if (normals.size() >= index * 3 + 3)
            vertex.normal = {normals[index * 3], normals[index * 3 + 1], normals[index * 3 + 2]};
        if (uvs.size() >= index * 2 + 2)
            vertex.uv = {uvs[index * 2], uvs[index * 2 + 1]};
        if (uvs2.size() >= index * 2 + 2)
            vertex.uv2 = {uvs2[index * 2], uvs2[index * 2 + 1]};
        if (tangents.size() >= index * 4 + 4)
            vertex.tangent = {tangents[index * 4], tangents[index * 4 + 1], tangents[index * 4 + 2],
                              tangents[index * 4 + 3]};
        if (colors.size() >= index * 4 + 4)
            vertex.color = {colors[index * 4], colors[index * 4 + 1], colors[index * 4 + 2],
                            colors[index * 4 + 3]};
    }
    return vertices;
}

} // namespace bbl::pal
