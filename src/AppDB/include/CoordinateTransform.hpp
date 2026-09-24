#pragma once

#include <SystemTypes.hpp>
#include <vector>
#include <cmath>

/**
 * @brief 坐标变换工具类
 *
 * 提供屏幕坐标与世界坐标之间的转换功能
 * 实现 VTK DisplayToWorld/WorldToDisplay 等效算法
 */
class CoordinateTransform {
public:
    /**
     * @brief 矩阵求逆（4x4矩阵）
     */
    static std::vector<float> inverseMatrix4x4(const std::vector<float>& matrix);

    /**
     * @brief 矩阵乘法（4x4矩阵）
     */
    static std::vector<float> multiplyMatrix4x4(const std::vector<float>& a,
                                                const std::vector<float>& b);

    /**
     * @brief 屏幕坐标转世界坐标
     *
     * @param screenX 屏幕X坐标（像素）
     * @param screenY 屏幕Y坐标（像素）
     * @param depth 深度值 [0,1]，0表示近裁剪面，1表示远裁剪面
     * @param viewportWidth 视口宽度（像素）
     * @param viewportHeight 视口高度（像素）
     * @param viewMatrix 视图矩阵（行主序，16个元素）
     * @param projectionMatrix 投影矩阵（行主序，16个元素）
     * @return 世界坐标
     */
    static Vector3 screenToWorld(float screenX, float screenY, float depth,
                                 int viewportWidth, int viewportHeight,
                                 const std::vector<float>& viewMatrix,
                                 const std::vector<float>& projectionMatrix);

    /**
     * @brief 世界坐标转屏幕坐标
     *
     * @param worldPos 世界坐标
     * @param viewportWidth 视口宽度（像素）
     * @param viewportHeight 视口高度（像素）
     * @param viewMatrix 视图矩阵（行主序，16个元素）
     * @param projectionMatrix 投影矩阵（行主序，16个元素）
     * @return 屏幕坐标（x,y）和深度值（z）
     */
    static Vector3 worldToScreen(const Vector3& worldPos,
                                 int viewportWidth, int viewportHeight,
                                 const std::vector<float>& viewMatrix,
                                 const std::vector<float>& projectionMatrix);

    /**
     * @brief 计算屏幕空间移动对应的世界空间移动量
     *
     * 用于拖拽操作时，根据鼠标移动计算物体在世界空间的移动量
     *
     * @param screenDelta 屏幕空间的移动量（像素）
     * @param objectWorldPos 物体的世界坐标
     * @param viewportWidth 视口宽度
     * @param viewportHeight 视口高度
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @return 世界空间的移动量
     */
    static Vector3 screenDeltaToWorldDelta(const Vector2& screenDelta,
                                           const Vector3& objectWorldPos,
                                           int viewportWidth, int viewportHeight,
                                           const std::vector<float>& viewMatrix,
                                           const std::vector<float>& projectionMatrix);

    /**
     * @brief 从屏幕坐标生成射线
     *
     * 用于拾取操作，从相机位置通过屏幕点生成一条射线
     *
     * @param screenX 屏幕X坐标
     * @param screenY 屏幕Y坐标
     * @param viewportWidth 视口宽度
     * @param viewportHeight 视口高度
     * @param viewMatrix 视图矩阵
     * @param projectionMatrix 投影矩阵
     * @param rayOrigin [输出] 射线起点（世界坐标）
     * @param rayDirection [输出] 射线方向（归一化向量）
     */
    static void screenToRay(float screenX, float screenY,
                           int viewportWidth, int viewportHeight,
                           const std::vector<float>& viewMatrix,
                           const std::vector<float>& projectionMatrix,
                           Vector3& rayOrigin, Vector3& rayDirection);

    /**
     * @brief 计算轴上离射线最近的点
     *
     * 用于缩放操作，将鼠标射线投影到指定轴上
     *
     * @param axisOrigin 轴的原点
     * @param axisDirection 轴的方向（应该是归一化向量）
     * @param rayOrigin 射线起点
     * @param rayDirection 射线方向（应该是归一化向量）
     * @return 沿轴方向的参数 t，使得 axisOrigin + t * axisDirection 是轴上离射线最近的点
     */
    static float closestPointOnAxisFromRay(const Vector3& axisOrigin,
                                           const Vector3& axisDirection,
                                           const Vector3& rayOrigin,
                                           const Vector3& rayDirection);

private:
    /**
     * @brief 向量与矩阵相乘
     */
    static Vector4 transformPoint(const Vector4& point, const std::vector<float>& matrix);

    /**
     * @brief 归一化向量
     */
    static Vector3 normalize(const Vector3& v);
};