#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>
#include "SystemTypes.hpp"

/**
 * @brief 轻量级变换类，基于 Eigen3，用于 DB 层的变换管理
 *
 * 设计理念（参考 vtkTransform）：
 * - 内部使用 4x4 变换矩阵存储完整的变换状态
 * - 欧拉角仅作为接口，不是主要存储
 * - 支持增量旋转，避免万向节锁
 * - 保持与现有 Vector3 类型的兼容性
 */
class Transform {
public:
    using Matrix4 = Eigen::Matrix4f;
    using Matrix3 = Eigen::Matrix3f;
    using Vector3f = Eigen::Vector3f;
    using AngleAxisf = Eigen::AngleAxisf;

    Transform() : m_matrix(Matrix4::Identity()) {}

    /**
     * @brief 拷贝构造函数（事务系统需要）
     */
    Transform(const Transform& other) : m_matrix(other.m_matrix) {}

    /**
     * @brief 赋值运算符（事务系统需要）
     */
    Transform& operator=(const Transform& other) {
        if (this != &other) {
            m_matrix = other.m_matrix;
        }
        return *this;
    }

    /**
     * @brief 相等性比较（事务系统需要）
     * 由于浮点数精度问题，使用近似相等
     */
    bool operator==(const Transform& other) const {
        return (m_matrix - other.m_matrix).cwiseAbs().maxCoeff() < 1e-6f;
    }

    bool operator!=(const Transform& other) const {
        return !(*this == other);
    }

    // === 基础操作 ===

    /**
     * @brief 重置为单位矩阵
     */
    void identity() {
        m_matrix = Matrix4::Identity();
    }

    /**
     * @brief 获取内部变换矩阵（只读）
     */
    const Matrix4& getMatrix() const { return m_matrix; }

    /**
     * @brief 获取内部变换矩阵（可写）
     */
    Matrix4& getMatrix() { return m_matrix; }

    /**
     * @brief 设置内部变换矩阵
     */
    void setMatrix(const Matrix4& matrix) { m_matrix = matrix; }

    // === 位置操作 ===

    /**
     * @brief 获取位置
     */
    Vector3 getPosition() const {
        return Vector3(m_matrix(0, 3), m_matrix(1, 3), m_matrix(2, 3));
    }

    /**
     * @brief 设置位置
     */
    void setPosition(const Vector3& pos) {
        m_matrix(0, 3) = pos.x;
        m_matrix(1, 3) = pos.y;
        m_matrix(2, 3) = pos.z;
    }

    /**
     * @brief 平移
     */
    void translate(const Vector3& delta) {
        m_matrix(0, 3) += delta.x;
        m_matrix(1, 3) += delta.y;
        m_matrix(2, 3) += delta.z;
    }

    // === 旋转操作（核心功能）===

    /**
     * @brief 绕任意轴旋转（角度制）
     * @param angle 旋转角度（度）
     * @param axis 旋转轴（会自动归一化）
     */
    void rotate(float angle, const Vector3& axis) {
        Vector3f eigenAxis(axis.x, axis.y, axis.z);
        eigenAxis.normalize();

        // 转换为弧度
        float radians = angle * M_PI / 180.0f;

        // 创建旋转矩阵
        AngleAxisf rotation(radians, eigenAxis);
        Matrix4 rotMatrix = Matrix4::Identity();
        rotMatrix.block<3, 3>(0, 0) = rotation.toRotationMatrix();

        // 应用旋转（右乘 = 在当前坐标系下旋转）
        m_matrix = m_matrix * rotMatrix;
    }

    /**
     * @brief 绕 X 轴旋转（角度制）
     */
    void rotateX(float angle) {
        rotate(angle, Vector3(1, 0, 0));
    }

    /**
     * @brief 绕 Y 轴旋转（角度制）
     */
    void rotateY(float angle) {
        rotate(angle, Vector3(0, 1, 0));
    }

    /**
     * @brief 绕 Z 轴旋转（角度制）
     */
    void rotateZ(float angle) {
        rotate(angle, Vector3(0, 0, 1));
    }

    /**
     * @brief 在局部坐标系下旋转（类似 VTK 的 RotateWXYZ）
     * @param angle 旋转角度（度）
     * @param axis 世界坐标系下的旋转轴
     */
    void rotateWXYZ(float angle, const Vector3& worldAxis) {
        // 将世界坐标系的轴变换到局部坐标系
        Vector3f eigenAxis(worldAxis.x, worldAxis.y, worldAxis.z);
        Vector3f localAxis = getRotationMatrix().transpose() * eigenAxis;
        localAxis.normalize();

        // 在局部坐标系下旋转
        rotate(angle, Vector3(localAxis.x(), localAxis.y(), localAxis.z()));
    }

    /**
     * @brief 变换向量（类似 VTK 的 TransformDoubleVector）
     */
    Vector3 transformVector(const Vector3& vec) const {
        Vector3f eigenVec(vec.x, vec.y, vec.z);
        Vector3f transformed = getRotationMatrix() * eigenVec;
        return Vector3(transformed.x(), transformed.y(), transformed.z());
    }

    // === 缩放操作 ===

    /**
     * @brief 获取缩放
     */
    Vector3 getScale() const {
        float sx = Vector3f(m_matrix(0, 0), m_matrix(1, 0), m_matrix(2, 0)).norm();
        float sy = Vector3f(m_matrix(0, 1), m_matrix(1, 1), m_matrix(2, 1)).norm();
        float sz = Vector3f(m_matrix(0, 2), m_matrix(1, 2), m_matrix(2, 2)).norm();
        return Vector3(sx, sy, sz);
    }

    /**
     * @brief 设置缩放
     */
    void setScale(const Vector3& scale) {
        // 保存当前旋转
        Matrix3 rotation = getRotationMatrix();

        // 应用新缩放
        m_matrix.block<3, 3>(0, 0) = rotation * Eigen::DiagonalMatrix<float, 3>(scale.x, scale.y, scale.z);
    }

    /**
     * @brief 缩放
     */
    void scale(float sx, float sy, float sz) {
        Matrix4 scaleMatrix = Matrix4::Identity();
        scaleMatrix(0, 0) = sx;
        scaleMatrix(1, 1) = sy;
        scaleMatrix(2, 2) = sz;
        m_matrix = m_matrix * scaleMatrix;
    }

    // === 欧拉角接口（仅用于与旧系统兼容）===

    /**
     * @brief 从欧拉角设置旋转（会重置整个变换）
     * @param euler XYZ 欧拉角（度）
     */
    void setFromEuler(const Vector3& euler) {
        identity();

        // 应用位置
        Vector3 pos = getPosition();
        translate(pos);

        // 按 XYZ 顺序应用旋转
        rotateX(euler.x);
        rotateY(euler.y);
        rotateZ(euler.z);

        // 应用缩放
        Vector3 scl = getScale();
        scale(scl.x, scl.y, scl.z);
    }

    /**
     * @brief 获取欧拉角（从旋转矩阵提取）
     * @return XYZ 欧拉角（度）
     */
    Vector3 getEuler() const {
        Matrix3 rotation = getRotationMatrix();

        // 使用 Eigen 的欧拉角提取（XYZ 顺序）
        Vector3f euler = rotation.eulerAngles(0, 1, 2) * 180.0f / M_PI;

        // 正规化到 [-180, 180]
        auto normalizeAngle = [](float angle) -> float {
            while (angle > 180.0f) angle -= 360.0f;
            while (angle < -180.0f) angle += 360.0f;
            return angle;
        };

        return Vector3(
            normalizeAngle(euler.x()),
            normalizeAngle(euler.y()),
            normalizeAngle(euler.z())
        );
    }

    /**
     * @brief 从四元数设置变换
     * @param w 四元数的 w 分量
     * @param x 四元数的 x 分量
     * @param y 四元数的 y 分量
     * @param z 四元数的 z 分量
     * @param position 位置
     * @param scale 缩放
     */
    void setFromQuaternion(float w, float x, float y, float z,
                           const Vector3& position, const Vector3& scale) {
        // 四元数转旋转矩阵
        float x2 = x + x, y2 = y + y, z2 = z + z;
        float xx = x * x2, xy = x * y2, xz = x * z2;
        float yy = y * y2, yz = y * z2, zz = z * z2;
        float wx = w * x2, wy = w * y2, wz = w * z2;

        // 设置旋转和缩放
        m_matrix(0, 0) = (1 - (yy + zz)) * scale.x;
        m_matrix(0, 1) = (xy - wz) * scale.x;
        m_matrix(0, 2) = (xz + wy) * scale.x;

        m_matrix(1, 0) = (xy + wz) * scale.y;
        m_matrix(1, 1) = (1 - (xx + zz)) * scale.y;
        m_matrix(1, 2) = (yz - wx) * scale.y;

        m_matrix(2, 0) = (xz - wy) * scale.z;
        m_matrix(2, 1) = (yz + wx) * scale.z;
        m_matrix(2, 2) = (1 - (xx + yy)) * scale.z;

        // 设置位置
        m_matrix(0, 3) = position.x;
        m_matrix(1, 3) = position.y;
        m_matrix(2, 3) = position.z;

        // 确保最后一行是 [0, 0, 0, 1]
        m_matrix(3, 0) = 0;
        m_matrix(3, 1) = 0;
        m_matrix(3, 2) = 0;
        m_matrix(3, 3) = 1;
    }

private:
    /**
     * @brief 获取旋转部分的 3x3 矩阵（不包含缩放）
     */
    Matrix3 getRotationMatrix() const {
        Matrix3 rot = m_matrix.block<3, 3>(0, 0);

        // 移除缩放影响
        Vector3 scale = getScale();
        rot.col(0) /= scale.x;
        rot.col(1) /= scale.y;
        rot.col(2) /= scale.z;

        return rot;
    }

private:
    Matrix4 m_matrix;  // 4x4 变换矩阵
};