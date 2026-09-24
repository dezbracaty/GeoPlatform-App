#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>

/** Backend- and database-independent 2D vector. */
struct Vector2 {
    float x{0.0f};
    float y{0.0f};

    Vector2() = default;
    Vector2(float x_, float y_)
        : x(x_), y(y_) {
    }

    bool operator==(const Vector2& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Vector2& other) const {
        return !(*this == other);
    }
};

using Polygon2 = std::vector<Vector2>;

/** Backend- and database-independent 3D vector. */
struct Vector3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    Vector3() = default;
    Vector3(float x_, float y_, float z_)
        : x(x_), y(y_), z(z_) {
    }

    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }

    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float scale) const {
        return Vector3(x * scale, y * scale, z * scale);
    }
};

/** Backend- and database-independent 4D vector. */
struct Vector4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};

    Vector4() = default;
    Vector4(float x_, float y_, float z_, float w_)
        : x(x_), y(y_), z(z_), w(w_) {
    }

    bool operator==(const Vector4& other) const {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const Vector4& other) const {
        return !(*this == other);
    }
};

/** RGBA color with normalized channels. */
struct Color {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};

    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {
    }

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const Color& other) const {
        return !(*this == other);
    }
};

/** Triangle value shared by mesh import, processing, and renderer projection. */
struct GeomTriangle {
    Vector3 vertex1;
    Vector3 vertex2;
    Vector3 vertex3;
    Vector3 normal;
    Color color;

    GeomTriangle() = default;

    GeomTriangle(const Vector3& v1, const Vector3& v2, const Vector3& v3)
        : vertex1(v1), vertex2(v2), vertex3(v3) {
        const Vector3 edge1 = v2 - v1;
        const Vector3 edge2 = v3 - v1;
        normal = Vector3(
            edge1.y * edge2.z - edge1.z * edge2.y,
            edge1.z * edge2.x - edge1.x * edge2.z,
            edge1.x * edge2.y - edge1.y * edge2.x);

        const float length = std::sqrt(
            normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0f) {
            normal = normal * (1.0f / length);
        }
    }

    bool operator==(const GeomTriangle& other) const {
        return vertex1 == other.vertex1 &&
               vertex2 == other.vertex2 &&
               vertex3 == other.vertex3 &&
               color == other.color;
    }

    bool operator!=(const GeomTriangle& other) const {
        return !(*this == other);
    }
};

struct GeometryBounds {
    Vector3 min;
    Vector3 max;
    bool valid{false};
};

/** Backend-independent indexed triangle surface. */
struct IndexedTriangleMesh {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<std::uint32_t> indices;

    bool hasValidTopology() const {
        if (positions.empty() || indices.empty() || indices.size() % 3 != 0 ||
            (!normals.empty() && normals.size() != positions.size())) {
            return false;
        }
        return std::all_of(indices.begin(), indices.end(), [this](std::uint32_t index) {
            return index < positions.size();
        });
    }

    std::size_t triangleCount() const { return indices.size() / 3; }

    GeometryBounds bounds() const {
        GeometryBounds result;
        if (positions.empty()) return result;
        const float maximum = (std::numeric_limits<float>::max)();
        result.min = Vector3(maximum, maximum, maximum);
        result.max = Vector3(-maximum, -maximum, -maximum);
        for (const Vector3& position : positions) {
            result.min.x = (std::min)(result.min.x, position.x);
            result.min.y = (std::min)(result.min.y, position.y);
            result.min.z = (std::min)(result.min.z, position.z);
            result.max.x = (std::max)(result.max.x, position.x);
            result.max.y = (std::max)(result.max.y, position.y);
            result.max.z = (std::max)(result.max.z, position.z);
        }
        result.valid = true;
        return result;
    }

    void rebuildVertexNormals() {
        normals.clear();
        if (positions.empty() || indices.empty() || indices.size() % 3 != 0 ||
            !std::all_of(indices.begin(), indices.end(), [this](std::uint32_t index) {
                return index < positions.size();
            })) {
            return;
        }
        normals.assign(positions.size(), Vector3());
        for (std::size_t offset = 0; offset < indices.size(); offset += 3) {
            const auto i0 = indices[offset];
            const auto i1 = indices[offset + 1];
            const auto i2 = indices[offset + 2];
            const Vector3 edge1 = positions[i1] - positions[i0];
            const Vector3 edge2 = positions[i2] - positions[i0];
            const Vector3 faceNormal(
                edge1.y * edge2.z - edge1.z * edge2.y,
                edge1.z * edge2.x - edge1.x * edge2.z,
                edge1.x * edge2.y - edge1.y * edge2.x);
            normals[i0] = normals[i0] + faceNormal;
            normals[i1] = normals[i1] + faceNormal;
            normals[i2] = normals[i2] + faceNormal;
        }
        for (Vector3& normal : normals) {
            const float length = std::sqrt(
                normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (length > 0.0f) normal = normal * (1.0f / length);
        }
    }
};
