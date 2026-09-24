#pragma once

#include <SystemTypes.hpp>  // for Vector3
#include <optional>

/**
 * @brief 统一的视觉样式类
 *
 * Handler 构造样式，DBSync 直接应用
 * 所有属性都是可选的，只有设置的属性才会被应用
 */
struct VisualStyle {
    // === 基础颜色和透明度 ===
    std::optional<Vector3> color;           // RGB颜色 (0.0-1.0)
    std::optional<double> opacity;          // 透明度 (0.0-1.0)

    // === 线条和轮廓 ===
    std::optional<double> lineWidth;        // 线条宽度
    std::optional<bool> showEdges;          // 是否显示边缘
    std::optional<Vector3> edgeColor;       // 边缘颜色

    // === 材质光照 ===
    std::optional<double> ambient;          // 环境光系数 (0.0-1.0)
    std::optional<double> diffuse;          // 漫反射系数 (0.0-1.0)
    std::optional<double> specular;         // 镜面反射系数 (0.0-1.0)

    // === 表示模式 ===
    std::optional<int> representation;      // VTK表示模式: 0=points, 1=wireframe, 2=surface

    // === 变换 ===
    std::optional<double> scaleFactor;      // 缩放因子

    // === 常用预设工厂方法 ===

    static VisualStyle createHover() {
        VisualStyle style;
        style.color = Vector3(0.5, 0.7, 1.0);  // 淡蓝色
        style.opacity = 0.8;
        return style;
    }

    static VisualStyle createSelection() {
        VisualStyle style;
        style.color = Vector3(1.0, 0.8, 0.0);  // 橙色
        style.showEdges = true;
        style.edgeColor = Vector3(1.0, 1.0, 0.0);  // 黄色边缘
        style.lineWidth = 2.0;
        return style;
    }

    static VisualStyle createDimmed() {
        VisualStyle style;
        style.color = Vector3(0.48f, 0.50f, 0.52f);
        style.opacity = 1.0;
        style.ambient = 0.28;
        style.diffuse = 0.55;
        return style;
    }

    static VisualStyle createWarning() {
        VisualStyle style;
        style.color = Vector3(1.0, 0.5, 0.0);  // 橙红色
        style.ambient = 0.4;  // 增强环境光
        return style;
    }

    static VisualStyle createError() {
        VisualStyle style;
        style.color = Vector3(1.0, 0.0, 0.0);  // 红色
        style.ambient = 0.5;
        style.showEdges = true;
        style.edgeColor = Vector3(1.0, 0.0, 0.0);
        return style;
    }

    static VisualStyle createDisabled() {
        VisualStyle style;
        style.color = Vector3(0.5, 0.5, 0.5);  // 灰色
        style.opacity = 0.5;  // 半透明
        return style;
    }

    static VisualStyle createWireframe() {
        VisualStyle style;
        style.representation = 1;  // VTK_WIREFRAME
        style.lineWidth = 1.5;
        style.color = Vector3(0.0, 1.0, 0.0);  // 绿色线框
        return style;
    }

    static VisualStyle createGlow() {
        VisualStyle style;
        style.color = Vector3(0.8, 0.8, 1.0);  // 淡紫色
        style.ambient = 0.6;   // 高环境光
        style.specular = 0.8;  // 高镜面反射
        return style;
    }
};
