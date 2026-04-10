#ifndef GEOMETRY_HPP
#define GEOMETRY_HPP

#include "Types.hpp"
#include <vector>

namespace Geometry {

const std::vector<Vertex> triangle = {
    {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}}, {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};

}; // namespace Geometry

#endif
