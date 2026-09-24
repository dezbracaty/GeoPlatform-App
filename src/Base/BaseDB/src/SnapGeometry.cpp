#include "SnapGeometry.hpp"
#include <algorithm>
#include <functional>
#include <limits>

void SnapGeometry::buildIndex() {
    nodes.clear();
    indices.clear();
    infiniteLines.clear();
    for (std::uint32_t i = 0; i < features.size(); ++i)
        (features[i].kind == SnapFeatureKind::Line ? infiniteLines : indices).push_back(i);
    if (indices.empty())
        return;
    const auto coordinate = [](const Vector3& p, int axis) {
        return axis == 0 ? p.x : axis == 1 ? p.y
                                           : p.z;
    };
    std::function<std::uint32_t(std::uint32_t, std::uint32_t)> build =
        [&](std::uint32_t begin, std::uint32_t end) {
            Node node;
            const float inf = std::numeric_limits<float>::infinity();
            node.min = {inf, inf, inf};
            node.max = {-inf, -inf, -inf};
            for (auto i = begin; i < end; ++i) {
                const auto& f = features[indices[i]];
                for (const auto p : {f.first, f.kind == SnapFeatureKind::Point ? f.first : f.second}) {
                    node.min.x = std::min(node.min.x, p.x - f.radiusMm);
                    node.min.y = std::min(node.min.y, p.y - f.radiusMm);
                    node.min.z = std::min(node.min.z, p.z - f.radiusMm);
                    node.max.x = std::max(node.max.x, p.x + f.radiusMm);
                    node.max.y = std::max(node.max.y, p.y + f.radiusMm);
                    node.max.z = std::max(node.max.z, p.z + f.radiusMm);
                }
            }
            const auto id = static_cast<std::uint32_t>(nodes.size());
            nodes.push_back(node);
            if (end - begin <= 16) {
                nodes[id].begin = begin;
                nodes[id].count = end - begin;
            } else {
                const Vector3 size = node.max - node.min;
                const int axis = size.x >= size.y && size.x >= size.z ? 0 : size.y >= size.z ? 1
                                                                                             : 2;
                const auto middle = begin + (end - begin) / 2;
                std::nth_element(indices.begin() + begin, indices.begin() + middle, indices.begin() + end,
                                 [&](auto a, auto b) {
                                     const auto center = [&](auto i) {
                                         const auto& f = features[i];
                                         return coordinate(f.first, axis) * 0.5f + coordinate(
                                                                                       f.kind == SnapFeatureKind::Point ? f.first : f.second, axis) *
                                                                                       0.5f;
                                     };
                                     return center(a) < center(b);
                                 });
                const auto left = build(begin, middle), right = build(middle, end);
                nodes[id].left = left;
                nodes[id].right = right;
            }
            return id;
        };
    build(0, static_cast<std::uint32_t>(indices.size()));
}
