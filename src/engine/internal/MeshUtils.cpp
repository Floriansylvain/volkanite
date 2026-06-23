#include "MeshUtils.hpp"
#include "Exceptions.hpp"
#include <filesystem>
#include <ufbx.h>

void MeshUtils::generateCube(Mesh &mesh, const float &size) {
    float half = size / 2.0f;

    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    struct FaceDef {
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec3 v0;
        glm::vec3 v1;
        glm::vec3 v2;
        glm::vec3 v3;
    };

    const std::array<FaceDef, 6> faces = {
        {{{0, 0, 1}, {1, 0, 0}, {-half, -half, half}, {half, -half, half}, {half, half, half}, {-half, half, half}},
         {{0, 0, -1}, {0, 1, 0}, {half, -half, -half}, {-half, -half, -half}, {-half, half, -half}, {half, half, -half}},
         {{0, 1, 0}, {0, 0, 1}, {-half, half, half}, {half, half, half}, {half, half, -half}, {-half, half, -half}},
         {{0, -1, 0}, {1, 1, 0}, {-half, -half, -half}, {half, -half, -half}, {half, -half, half}, {-half, -half, half}},
         {{1, 0, 0}, {0, 1, 1}, {half, -half, half}, {half, -half, -half}, {half, half, -half}, {half, half, half}},
         {{-1, 0, 0}, {1, 0, 1}, {-half, -half, -half}, {-half, -half, half}, {-half, half, half}, {-half, half, -half}}}};

    uint16_t vertexOffset = 0;

    for (const auto &face : faces) {
        mesh.vertices.push_back({face.v0, face.color, {0.0f, 0.0f}, face.normal});
        mesh.vertices.push_back({face.v1, face.color, {1.0f, 0.0f}, face.normal});
        mesh.vertices.push_back({face.v2, face.color, {1.0f, 1.0f}, face.normal});
        mesh.vertices.push_back({face.v3, face.color, {0.0f, 1.0f}, face.normal});

        mesh.indices.push_back(vertexOffset + 0);
        mesh.indices.push_back(vertexOffset + 1);
        mesh.indices.push_back(vertexOffset + 2);
        mesh.indices.push_back(vertexOffset + 2);
        mesh.indices.push_back(vertexOffset + 3);
        mesh.indices.push_back(vertexOffset + 0);

        vertexOffset += 4;
    }

    computeTangents(mesh.vertices, mesh.indices);
}

void MeshUtils::computeTangents(std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices) {
    std::vector<glm::vec3> accum(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        const glm::vec3 edge1 = vertices[i1].pos - vertices[i0].pos;
        const glm::vec3 edge2 = vertices[i2].pos - vertices[i0].pos;
        const glm::vec2 deltaUV1 = vertices[i1].texCoord - vertices[i0].texCoord;
        const glm::vec2 deltaUV2 = vertices[i2].texCoord - vertices[i0].texCoord;

        const float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(denom) < 1e-8f)
            continue;

        const float f = 1.0f / denom;
        const glm::vec3 tangent = f * (deltaUV2.y * edge1 - deltaUV1.y * edge2);

        accum[i0] += tangent;
        accum[i1] += tangent;
        accum[i2] += tangent;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3 &n = vertices[i].normal;
        glm::vec3 t = accum[i] - n * glm::dot(n, accum[i]); // Gram-Schmidt against the normal

        if (glm::length(t) < 1e-6f) {
            const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            t = glm::cross(up, n);
        }

        vertices[i].tangent = glm::normalize(t);
    }
}

namespace {
std::string bareFilename(const ufbx_texture *texture) {
    if (!texture)
        return "";
    const std::string raw = texture->relative_filename.length > 0 ? std::string(texture->relative_filename.data)
                                                                  : std::string(texture->filename.data);
    if (raw.empty())
        return "";
    const size_t lastSep = raw.find_last_of("/\\");
    return lastSep == std::string::npos ? raw : raw.substr(lastSep + 1);
}
} // namespace

void MeshUtils::extractMaterial(const ufbx_mesh *mesh, const ufbx_mesh_part &part, SubMesh &sub) {
    if (part.index >= mesh->materials.count)
        return;

    const ufbx_material *material = mesh->materials.data[part.index];
    if (!material)
        return;

    const ufbx_material_map &baseColor = material->pbr.base_color;
    if (baseColor.has_value) {
        sub.baseColor = {static_cast<float>(baseColor.value_vec3.x), static_cast<float>(baseColor.value_vec3.y),
                         static_cast<float>(baseColor.value_vec3.z)};
    }
    if (baseColor.texture && baseColor.texture_enabled) {
        sub.albedoFilename = bareFilename(baseColor.texture);
    }

    const ufbx_material_map &roughness = material->pbr.roughness;
    if (roughness.has_value) {
        sub.roughness = static_cast<float>(roughness.value_real);
    }
    if (roughness.texture && roughness.texture_enabled) {
        sub.roughnessMapFilename = bareFilename(roughness.texture);
    }

    if (const ufbx_material_map &normalMap = material->pbr.normal_map; normalMap.texture && normalMap.texture_enabled) {
        sub.normalMapFilename = bareFilename(normalMap.texture);
    }

    const ufbx_material_map &metalness = material->pbr.metalness;
    if (metalness.has_value) {
        sub.metallic = static_cast<float>(metalness.value_real);
    }
    if (metalness.texture && metalness.texture_enabled) {
        sub.metallicMapFilename = bareFilename(metalness.texture);
    }

    const ufbx_material_map &displacement = material->pbr.displacement_map;
    if (displacement.texture && displacement.texture_enabled) {
        sub.heightMapFilename = bareFilename(displacement.texture);
    }
}

Mesh::Vertex MeshUtils::processVertex(const ufbx_mesh *mesh, const ufbx_node *node, uint32_t corner) {
    Mesh::Vertex vertex{};

    const ufbx_vec3 localPos = ufbx_get_vertex_vec3(&mesh->vertex_position, corner);
    const ufbx_vec3 worldPos = ufbx_transform_position(&node->geometry_to_world, localPos);
    vertex.pos = {static_cast<float>(worldPos.x), static_cast<float>(worldPos.y), static_cast<float>(worldPos.z)};

    if (mesh->vertex_uv.exists) {
        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
        vertex.texCoord = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
    } else {
        vertex.texCoord = {0.0f, 0.0f};
    }

    if (mesh->vertex_color.exists) {
        const ufbx_vec4 col = ufbx_get_vertex_vec4(&mesh->vertex_color, corner);
        vertex.color = {static_cast<float>(col.x), static_cast<float>(col.y), static_cast<float>(col.z)};
    } else {
        vertex.color = {1.0f, 1.0f, 1.0f};
    }

    if (mesh->vertex_normal.exists) {
        const ufbx_vec3 localNormal = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
        const ufbx_vec3 worldNormal = ufbx_transform_direction(&node->geometry_to_world, localNormal);
        vertex.normal = glm::normalize(
            glm::vec3{static_cast<float>(worldNormal.x), static_cast<float>(worldNormal.y), static_cast<float>(worldNormal.z)});
    } else {
        vertex.normal = {0.0f, 1.0f, 0.0f};
    }

    return vertex;
}

SubMesh MeshUtils::processMeshPart(const ufbx_mesh *mesh, const ufbx_node *node, const ufbx_mesh_part &part) {
    SubMesh sub;
    sub.vertices.reserve(part.num_triangles * 3);
    sub.indices.reserve(part.num_triangles * 3);

    std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);

    for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
        const ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
        if (face.num_indices < 3) {
            continue;
        }

        const uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);

        for (uint32_t c = 0; c < numTris * 3; ++c) {
            sub.indices.push_back(static_cast<uint32_t>(sub.vertices.size()));
            sub.vertices.push_back(processVertex(mesh, node, triIndices[c]));
        }
    }

    extractMaterial(mesh, part, sub);
    return sub;
}

void MeshUtils::processMesh(const ufbx_mesh *mesh, std::vector<SubMesh> &subMeshes) {
    if (mesh->instances.count == 0) {
        return;
    }
    const ufbx_node *node = mesh->instances.data[0];

    for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
        const ufbx_mesh_part &part = mesh->material_parts.data[pi];
        if (part.num_triangles == 0) {
            continue;
        }

        subMeshes.push_back(processMeshPart(mesh, node, part));
    }
}

std::vector<SubMesh> MeshUtils::loadFBXModel(const std::string &path) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_z_up;
    ufbx_error error;

    ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        throw EngineExceptions::Compatibility("Failed to load FBX model \"" + path +
                                              "\": " + std::string(error.description.data));
    }

    std::vector<SubMesh> subMeshes;
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        processMesh(scene->meshes.data[mi], subMeshes);
    }

    ufbx_free_scene(scene);
    return subMeshes;
}

void MeshUtils::deduplicateVertices(std::vector<Mesh::Vertex> &vertices, std::vector<uint32_t> &indices) {
    std::unordered_map<Mesh::Vertex, uint32_t> uniqueVertices;
    std::vector<Mesh::Vertex> dedupedVertices;
    std::vector<uint32_t> remappedIndices;
    dedupedVertices.reserve(vertices.size());
    remappedIndices.reserve(indices.size());

    for (const uint32_t idx : indices) {
        const Mesh::Vertex &v = vertices[idx];
        if (const auto it = uniqueVertices.find(v); it == uniqueVertices.end()) {
            const auto newIndex = static_cast<uint32_t>(dedupedVertices.size());
            uniqueVertices.try_emplace(v, newIndex);
            dedupedVertices.push_back(v);
            remappedIndices.push_back(newIndex);
        } else {
            remappedIndices.push_back(it->second);
        }
    }

    vertices = std::move(dedupedVertices);
    indices = std::move(remappedIndices);
}
