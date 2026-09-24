#pragma once

#include "BaseID.hpp"
#include <Geometry.hpp>
#include <QString>
#include <QVariant>
#include <chrono>

/**
 * @brief 系统中的DB类型枚举
 */
enum class TypeID : uint32_t {
    UNKNOWN = 0,
    // 1 was the removed GeometryDB. Keep the numeric slot unused so stored
    // type identifiers are never silently reinterpreted.
    MATERIAL_DB = 2,
    TEXTURE_DB = 3,
    CAMERA_DB = 4,
    LIGHT_DB = 5,
    SCENE_DB = 6,

    // Actor DB types
    ACTOR_DB = 10,
    CUBE_DB = 11,
    SPHERE_DB = 12,
    CONE_DB = 13,
    CYLINDER_DB = 14,
    // 15 was the removed legacy printable-mesh type. Keep the slot unused so stored type
    // identifiers are never silently reinterpreted.
    // 16 is reserved after removal of the legacy triangle-highlight mesh type.
    // Keep the numeric slot unused so stored type identifiers are never
    // silently reinterpreted.
    PRINT_BED_DB = 17,
    TOOLPATH_PREVIEW_DB = 18,
    SLICING_CONFIG_DB = 19,
    DEBUG_ACTOR_DB = 20,  // 调试可视化 Actor
    // 21 was another removed legacy mesh type. Keep the numeric slot unused so stored
    // type identifiers are never silently reinterpreted.
    MANUAL_SUPPORT_DB = 22, // 模型拥有的手动支撑状态与派生结果
    // 23 was the removed ManualSupportStateDB. State now belongs to the
    // ManualSupportDB aggregate; keep the numeric slot unused.
    // 24 was the removed PartPlateDB. Keep the numeric slot unused so stored
    // type identifiers are never silently reinterpreted.
    MODEL_SURFACE_COLOR_DB = 25, // 模型拥有的正式表面颜色数据
    TRANSIENT_POLY_DATA_ACTOR_DB = 26, // 通用临时 PolyData 渲染对象
    PRINT_BED_TEMPLATE_DB = 27, // 不可渲染、可复用的打印床定义
    GLB_DB = 28, // 保留完整 GLB 资产的可打印模型
    CUT_PREVIEW_DB = 29, // 切割工具会话期间的临时渲染投影
    MODEL_OBJECT_DB = 30, // 逻辑模型定义，不直接渲染
    MODEL_PART_DB = 31, // Object 内独立局部网格
    MODEL_INSTANCE_DB = 32, // Object 在场景中的一次摆放
    MODEL_GEOMETRY_DB = 33, // Part 拥有的正式几何资源

    SNAP_PREVIEW_DB = 34, // 视口内临时吸附高亮，使用后端无关点线数据

    // GPlatform specific types
    POINT_DATA_DB = 100,
    BATCH_DATA_DB = 101,

    // Window and UI management types
    WINDOW_DB = 150,

    // Scene environment types
    SKYBOX_DB = 180,

    // Widget DB types
    WIDGET_DB = 200,
    // 201 and 202 are retired viewport widget IDs. Do not reuse.
    MEASUREMENT_DB = 203,
    ANNOTATION_DB = 204,
    GRID_DB = 205,
    MODEL_ORIENTATION_WIDGET_DB = 206,
    MODEL_SCALE_WIDGET_DB = 207,
    MODEL_TRANSLATE_WIDGET_DB = 208,
    CUT_PLANE_WIDGET_DB = 209,
    SELECTION_BOX_WIDGET_DB = 210
};

/**
 * @brief 渲染目标类型
 */
enum class RenderTargets : uint32_t {
    NONE = 0,
    GEOMETRY = 1 << 0, // 几何体渲染
    MATERIAL = 1 << 1, // 材质渲染
    LIGHTING = 1 << 2, // 光照渲染
    CAMERA = 1 << 3,   // 相机更新
    ALL = 0xFFFFFFFF   // 全部重新渲染
};

// 位运算支持
inline RenderTargets operator|(RenderTargets a, RenderTargets b) {
    return static_cast<RenderTargets>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RenderTargets operator&(RenderTargets a, RenderTargets b) {
    return static_cast<RenderTargets>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * @brief 时间戳类型
 */
using Timestamp = std::chrono::steady_clock::time_point;

inline Timestamp getCurrentTimestamp() {
    return std::chrono::steady_clock::now();
}

/**
 * @brief 属性值类型（支持多种数据类型）
 */
using PropertyValue = QVariant;

/**
 * @brief 属性映射表
 */
using PropertyMap = QVariantMap;

/**
 * @brief TypeID 辅助函数
 */
inline QString typeIdToString(TypeID typeId) {
    switch (typeId) {
        case TypeID::UNKNOWN:
            return "UNKNOWN";
        case TypeID::MATERIAL_DB:
            return "MATERIAL_DB";
        case TypeID::TEXTURE_DB:
            return "TEXTURE_DB";
        case TypeID::CAMERA_DB:
            return "CAMERA_DB";
        case TypeID::LIGHT_DB:
            return "LIGHT_DB";
        case TypeID::SCENE_DB:
            return "SCENE_DB";
        case TypeID::ACTOR_DB:
            return "ACTOR_DB";
        case TypeID::CUBE_DB:
            return "CUBE_DB";
        case TypeID::SPHERE_DB:
            return "SPHERE_DB";
        case TypeID::CONE_DB:
            return "CONE_DB";
        case TypeID::CYLINDER_DB:
            return "CYLINDER_DB";
        case TypeID::GLB_DB:
            return "GLB_DB";
        case TypeID::CUT_PREVIEW_DB:
            return "CUT_PREVIEW_DB";
        case TypeID::MODEL_OBJECT_DB:
            return "MODEL_OBJECT_DB";
        case TypeID::MODEL_PART_DB:
            return "MODEL_PART_DB";
        case TypeID::MODEL_INSTANCE_DB:
            return "MODEL_INSTANCE_DB";
        case TypeID::MODEL_GEOMETRY_DB:
            return "MODEL_GEOMETRY_DB";
        case TypeID::PRINT_BED_DB:
            return "PRINT_BED_DB";
        case TypeID::PRINT_BED_TEMPLATE_DB:
            return "PRINT_BED_TEMPLATE_DB";
        case TypeID::TOOLPATH_PREVIEW_DB:
            return "TOOLPATH_PREVIEW_DB";
        case TypeID::SLICING_CONFIG_DB:
            return "SLICING_CONFIG_DB";
        case TypeID::DEBUG_ACTOR_DB:
            return "DEBUG_ACTOR_DB";
        case TypeID::MANUAL_SUPPORT_DB:
            return "MANUAL_SUPPORT_DB";
        case TypeID::MODEL_SURFACE_COLOR_DB:
            return "MODEL_SURFACE_COLOR_DB";
        case TypeID::SNAP_PREVIEW_DB:
            return "SNAP_PREVIEW_DB";
        case TypeID::TRANSIENT_POLY_DATA_ACTOR_DB:
            return "TRANSIENT_POLY_DATA_ACTOR_DB";
        case TypeID::POINT_DATA_DB:
            return "POINT_DATA_DB";
        case TypeID::BATCH_DATA_DB:
            return "BATCH_DATA_DB";
        case TypeID::WINDOW_DB:
            return "WINDOW_DB";
        case TypeID::SKYBOX_DB:
            return "SKYBOX_DB";
        case TypeID::WIDGET_DB:
            return "WIDGET_DB";
        case TypeID::MEASUREMENT_DB:
            return "MEASUREMENT_DB";
        case TypeID::ANNOTATION_DB:
            return "ANNOTATION_DB";
        case TypeID::GRID_DB:
            return "GRID_DB";
        case TypeID::MODEL_ORIENTATION_WIDGET_DB:
            return "MODEL_ORIENTATION_WIDGET_DB";
        case TypeID::MODEL_SCALE_WIDGET_DB:
            return "MODEL_SCALE_WIDGET_DB";
        case TypeID::MODEL_TRANSLATE_WIDGET_DB:
            return "MODEL_TRANSLATE_WIDGET_DB";
        case TypeID::CUT_PLANE_WIDGET_DB:
            return "CUT_PLANE_WIDGET_DB";
        case TypeID::SELECTION_BOX_WIDGET_DB:
            return "SELECTION_BOX_WIDGET_DB";
        default:
            return "UNKNOWN";
    }
}

/** Actor/model family helpers. Keep type-category decisions centralized. */
constexpr bool isModelInstanceType(TypeID type) noexcept {
    return type == TypeID::MODEL_INSTANCE_DB;
}

constexpr bool isPrintableModelType(TypeID type) noexcept {
    return isModelInstanceType(type);
}
