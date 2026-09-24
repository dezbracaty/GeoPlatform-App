#pragma once

#include "PickTypes.hpp"
#include <Geometry.hpp>
#include <vector>

class GeometryExtractor {
public:
    static std::vector<GeomTriangle> extractSingleCell(const PickResult& pickResult);
};
