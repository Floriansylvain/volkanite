#include "MeshUtils.hpp"

void MeshUtility::generateCube(Mesh &mesh, const float &size) {
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
        mesh.vertices.push_back({face.v0, face.color, {0.0f, 0.0f}});
        mesh.vertices.push_back({face.v1, face.color, {1.0f, 0.0f}});
        mesh.vertices.push_back({face.v2, face.color, {1.0f, 1.0f}});
        mesh.vertices.push_back({face.v3, face.color, {0.0f, 1.0f}});

        mesh.indices.push_back(vertexOffset + 0);
        mesh.indices.push_back(vertexOffset + 1);
        mesh.indices.push_back(vertexOffset + 2);
        mesh.indices.push_back(vertexOffset + 2);
        mesh.indices.push_back(vertexOffset + 3);
        mesh.indices.push_back(vertexOffset + 0);

        vertexOffset += 4;
    }
}
