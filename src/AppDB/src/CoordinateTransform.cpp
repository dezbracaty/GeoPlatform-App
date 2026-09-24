#include "CoordinateTransform.hpp"
#include "Foundation/Log.h"
#include <algorithm>

std::vector<float> CoordinateTransform::inverseMatrix4x4(const std::vector<float>& m) {
    if (m.size() != 16) {
        LOG_ERROR("Invalid matrix size for inversion");
        return m;
    }

    // 使用 Gauss-Jordan 消元法求逆
    std::vector<float> result(16);
    std::vector<float> temp(m);

    // 初始化为单位矩阵
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result[i * 4 + j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Gauss-Jordan 消元
    for (int i = 0; i < 4; i++) {
        // 找主元
        float maxVal = std::abs(temp[i * 4 + i]);
        int maxRow = i;
        for (int k = i + 1; k < 4; k++) {
            if (std::abs(temp[k * 4 + i]) > maxVal) {
                maxVal = std::abs(temp[k * 4 + i]);
                maxRow = k;
            }
        }

        // 交换行
        if (maxRow != i) {
            for (int k = 0; k < 4; k++) {
                std::swap(temp[i * 4 + k], temp[maxRow * 4 + k]);
                std::swap(result[i * 4 + k], result[maxRow * 4 + k]);
            }
        }

        // 归一化主元所在行
        float pivot = temp[i * 4 + i];
        if (std::abs(pivot) < 1e-10f) {
            LOG_WARN("Matrix is singular, cannot invert");
            return m;
        }

        for (int k = 0; k < 4; k++) {
            temp[i * 4 + k] /= pivot;
            result[i * 4 + k] /= pivot;
        }

        // 消元
        for (int j = 0; j < 4; j++) {
            if (j != i) {
                float factor = temp[j * 4 + i];
                for (int k = 0; k < 4; k++) {
                    temp[j * 4 + k] -= factor * temp[i * 4 + k];
                    result[j * 4 + k] -= factor * result[i * 4 + k];
                }
            }
        }
    }

    return result;
}

std::vector<float> CoordinateTransform::multiplyMatrix4x4(const std::vector<float>& a,
                                                         const std::vector<float>& b) {
    std::vector<float> result(16, 0.0f);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                result[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }

    return result;
}

Vector4 CoordinateTransform::transformPoint(const Vector4& point, const std::vector<float>& matrix) {
    Vector4 result;
    result.x = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3] * point.w;
    result.y = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7] * point.w;
    result.z = matrix[8] * point.x + matrix[9] * point.y + matrix[10] * point.z + matrix[11] * point.w;
    result.w = matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z + matrix[15] * point.w;
    return result;
}

Vector3 CoordinateTransform::normalize(const Vector3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-10f) {
        return Vector3(v.x / len, v.y / len, v.z / len);
    }
    return Vector3(0, 0, 0);
}

Vector3 CoordinateTransform::screenToWorld(float screenX, float screenY, float depth,
                                          int viewportWidth, int viewportHeight,
                                          const std::vector<float>& viewMatrix,
                                          const std::vector<float>& projectionMatrix) {
    // 1. Screen → NDC (Normalized Device Coordinates)
    // 注意：屏幕Y坐标通常需要翻转（屏幕原点在左上，NDC原点在左下）
    float ndcX = (2.0f * screenX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY / viewportHeight); // Y翻转
    float ndcZ = 2.0f * depth - 1.0f;

    // LOG_DEBUG("Screen to NDC: ({}, {}) → ({}, {}, {})",
    //          screenX, screenY, ndcX, ndcY, ndcZ);

    // 2. NDC → Clip Space (齐次坐标)
    Vector4 clipCoords(ndcX, ndcY, ndcZ, 1.0f);

    // 3. Clip → View Space (通过逆投影矩阵)
    auto invProjection = inverseMatrix4x4(projectionMatrix);
    Vector4 viewCoords = transformPoint(clipCoords, invProjection);

    // 透视除法
    if (std::abs(viewCoords.w) > 1e-10f) {
        viewCoords.x /= viewCoords.w;
        viewCoords.y /= viewCoords.w;
        viewCoords.z /= viewCoords.w;
        viewCoords.w = 1.0f;
    }

    // LOG_DEBUG("View coords: ({}, {}, {})",
    //          viewCoords.x, viewCoords.y, viewCoords.z);

    // 4. View → World Space (通过逆视图矩阵)
    auto invView = inverseMatrix4x4(viewMatrix);
    Vector4 worldCoords = transformPoint(viewCoords, invView);

    return Vector3(worldCoords.x, worldCoords.y, worldCoords.z);
}

Vector3 CoordinateTransform::worldToScreen(const Vector3& worldPos,
                                          int viewportWidth, int viewportHeight,
                                          const std::vector<float>& viewMatrix,
                                          const std::vector<float>& projectionMatrix) {
    // 1. World → View Space
    Vector4 worldCoords(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    Vector4 viewCoords = transformPoint(worldCoords, viewMatrix);

    // 2. View → Clip Space
    Vector4 clipCoords = transformPoint(viewCoords, projectionMatrix);

    // 3. 透视除法 Clip → NDC
    if (std::abs(clipCoords.w) > 1e-10f) {
        clipCoords.x /= clipCoords.w;
        clipCoords.y /= clipCoords.w;
        clipCoords.z /= clipCoords.w;
    }

    // 4. NDC → Screen
    float screenX = (clipCoords.x + 1.0f) * 0.5f * viewportWidth;
    float screenY = (1.0f - clipCoords.y) * 0.5f * viewportHeight; // Y翻转
    float depth = (clipCoords.z + 1.0f) * 0.5f;

    return Vector3(screenX, screenY, depth);
}

Vector3 CoordinateTransform::screenDeltaToWorldDelta(const Vector2& screenDelta,
                                                    const Vector3& objectWorldPos,
                                                    int viewportWidth, int viewportHeight,
                                                    const std::vector<float>& viewMatrix,
                                                    const std::vector<float>& projectionMatrix) {
    // 1. 将物体世界坐标转换到屏幕坐标
    Vector3 screenPos = worldToScreen(objectWorldPos, viewportWidth, viewportHeight,
                                     viewMatrix, projectionMatrix);

    // LOG_DEBUG("Object world pos: ({}, {}, {}) → screen: ({}, {}, {}) [viewport: {}x{}]",
    //          objectWorldPos.x, objectWorldPos.y, objectWorldPos.z,
    //          screenPos.x, screenPos.y, screenPos.z, viewportWidth, viewportHeight);

    // 2. 计算新的屏幕坐标
    float newScreenX = screenPos.x + screenDelta.x;
    float newScreenY = screenPos.y + screenDelta.y;

    // 3. 使用相同的深度值，将新屏幕坐标转换回世界坐标
    Vector3 newWorldPos = screenToWorld(newScreenX, newScreenY, screenPos.z,
                                       viewportWidth, viewportHeight,
                                       viewMatrix, projectionMatrix);

    // LOG_DEBUG("New screen pos: ({}, {}) → world: ({}, {}, {})",
    //          newScreenX, newScreenY,
    //          newWorldPos.x, newWorldPos.y, newWorldPos.z);

    // 4. 计算世界空间的增量
    return Vector3(newWorldPos.x - objectWorldPos.x,
                  newWorldPos.y - objectWorldPos.y,
                  newWorldPos.z - objectWorldPos.z);
}

void CoordinateTransform::screenToRay(float screenX, float screenY,
                                     int viewportWidth, int viewportHeight,
                                     const std::vector<float>& viewMatrix,
                                     const std::vector<float>& projectionMatrix,
                                     Vector3& rayOrigin, Vector3& rayDirection) {
    // 近裁剪面上的点（depth = 0）
    Vector3 nearPoint = screenToWorld(screenX, screenY, 0.0f,
                                     viewportWidth, viewportHeight,
                                     viewMatrix, projectionMatrix);

    // 远裁剪面上的点（depth = 1）
    Vector3 farPoint = screenToWorld(screenX, screenY, 1.0f,
                                    viewportWidth, viewportHeight,
                                    viewMatrix, projectionMatrix);

    // 射线起点为近裁剪面上的点
    rayOrigin = nearPoint;

    // 射线方向从近点指向远点
    Vector3 direction(farPoint.x - nearPoint.x,
                     farPoint.y - nearPoint.y,
                     farPoint.z - nearPoint.z);
    rayDirection = normalize(direction);

    LOG_DEBUG("Ray from screen ({}, {}): origin=({}, {}, {}), dir=({}, {}, {})",
             screenX, screenY,
             rayOrigin.x, rayOrigin.y, rayOrigin.z,
             rayDirection.x, rayDirection.y, rayDirection.z);
}

float CoordinateTransform::closestPointOnAxisFromRay(const Vector3& axisOrigin,
                                                     const Vector3& axisDirection,
                                                     const Vector3& rayOrigin,
                                                     const Vector3& rayDirection) {
    // 计算轴上离射线最近的点
    // 这是一个经典的3D几何问题：两条不相交直线之间的最近点
    //
    // 轴的参数方程：P_axis(t) = axisOrigin + t * axisDirection
    // 射线的参数方程：P_ray(s) = rayOrigin + s * rayDirection
    //
    // 我们要找到使得 |P_axis(t) - P_ray(s)| 最小的 t 和 s
    // 最优解的条件是：(P_axis(t) - P_ray(s)) 垂直于两条线的方向向量

    Vector3 w0 = rayOrigin - axisOrigin;  // 从轴原点到射线原点的向量

    float a = axisDirection.x * axisDirection.x + axisDirection.y * axisDirection.y + axisDirection.z * axisDirection.z;  // |axisDirection|^2
    float b = axisDirection.x * rayDirection.x + axisDirection.y * rayDirection.y + axisDirection.z * rayDirection.z;    // axisDirection · rayDirection
    float c = rayDirection.x * rayDirection.x + rayDirection.y * rayDirection.y + rayDirection.z * rayDirection.z;        // |rayDirection|^2
    float d = axisDirection.x * w0.x + axisDirection.y * w0.y + axisDirection.z * w0.z;                                   // axisDirection · w0
    float e = rayDirection.x * w0.x + rayDirection.y * w0.y + rayDirection.z * w0.z;                                     // rayDirection · w0

    float denom = a * c - b * b;

    // 检查两条线是否平行
    if (std::abs(denom) < 1e-6f) {
        // 两条线平行，投影 rayOrigin 到轴上
        return d / a;
    }

    // 计算最优参数 t
    float t = (b * e - c * d) / denom;

    LOG_DEBUG("Closest point on axis: t={:.3f}, axis=({:.2f},{:.2f},{:.2f}), ray=({:.2f},{:.2f},{:.2f})",
              t, axisDirection.x, axisDirection.y, axisDirection.z,
              rayDirection.x, rayDirection.y, rayDirection.z);

    return t;
}