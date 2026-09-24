#include "GeometryExtractor.hpp"

std::vector<GeomTriangle> GeometryExtractor::extractSingleCell(const PickResult& pickResult) {
    if (!pickResult.primitiveTriangle) {
        return {};
    }

    const auto& triangle = *pickResult.primitiveTriangle;
    return {GeomTriangle(triangle.v0, triangle.v1, triangle.v2)};
}
