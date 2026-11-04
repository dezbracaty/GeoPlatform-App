#pragma once

#include <array>
#include <cmath>

namespace GPlatform::Rendering {

// Per-view presentation state. Never persisted to the document or material.
class PrintBedViewPolicy {
public:
    static constexpr float BottomGridOpacity = 0.4f;

    template<class Matrix>
    bool update(const std::array<double, 3>& forward, const Matrix& transform) {
        // Inverse-transpose local +Z normal, including non-uniform scaling.
        const double ax = transform(0, 0), ay = transform(1, 0), az = transform(2, 0);
        const double bx = transform(0, 1), by = transform(1, 1), bz = transform(2, 1);
        double nx = ay * bz - az * by;
        double ny = az * bx - ax * bz;
        double nz = ax * by - ay * bx;
        const double determinant = nx * transform(0, 2) + ny * transform(1, 2) + nz * transform(2, 2);
        if (determinant < 0.0) { nx = -nx; ny = -ny; nz = -nz; }
        const double length = std::sqrt((nx * nx + ny * ny + nz * nz) *
            (forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]));
        if (!std::isfinite(length) || length <= 1e-12 || std::abs(determinant) <= 1e-12)
            return false;
        const double facing = (nx * forward[0] + ny * forward[1] + nz * forward[2]) / length;
        if (!std::isfinite(facing)) return false;
        const bool next = !m_initialized ? facing >= 0.0
            : (facing > 0.01 ? true : (facing < -0.01 ? false : m_bottom));
        const bool changed = !m_initialized || next != m_bottom;
        m_initialized = true;
        m_bottom = next;
        return changed;
    }

    bool bottom() const noexcept { return m_bottom; }
    float gridOpacity() const noexcept { return m_bottom ? BottomGridOpacity : 1.0f; }

private:
    bool m_initialized{false};
    bool m_bottom{false};
};

} // namespace GPlatform::Rendering
