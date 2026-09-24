#pragma once

#include <AutoRegisterDB.hpp>
#include <ChangeTypes.hpp>
#include <SystemTypes.hpp>
#include <memory>

// 前向声明
class QSize;
class QString;
class ViewportCoordinateSystem;
class CameraDB;

/**
 * @brief 窗口数据库类 - 管理渲染窗口的状态和配置
 *
 * 负责管理：
 * - 窗口位置、尺寸和显示状态
 * - 视口尺寸和DPI缩放
 * - 窗口标题和可见性
 * - 关联的渲染配置
 *
 * 设计理念：
 * - 所有属性使用 FIELD_VALUE 宏管理，支持事务和撤销/重做
 * - 对应 RenderThreadQmlItem 的窗口状态
 * - 支持多窗口场景，每个窗口有独立的状态
 * - 直接属性设置，简洁高效
 * - 支持窗口状态的持久化
 */
class WindowDB : public AutoRegisterDB {

public:
    /**
     * @brief 窗口状态枚举
     */
    enum class WindowState {
        NORMAL = 0,     // 正常状态
        MINIMIZED = 1,  // 最小化
        MAXIMIZED = 2,  // 最大化
        FULLSCREEN = 3  // 全屏
    };

    /**
     * @brief 构造函数
     */
    explicit WindowDB();

    /**
     * @brief 析构函数
     */
    virtual ~WindowDB();

    // === 窗口基本属性 ===

    // 窗口位置（屏幕坐标）
    FIELD_VALUE_SIMPLE(WindowDB, Vector2, Position)

    // 窗口尺寸（像素）
    FIELD_VALUE_SIMPLE(WindowDB, Vector2, Size)

    // 窗口状态
    FIELD_VALUE_SIMPLE(WindowDB, int, State)  // 对应 WindowState 枚举

    // 窗口标题
    FIELD_VALUE_SIMPLE(WindowDB, std::string, Title)

    // 窗口可见性
    FIELD_VALUE_SIMPLE(WindowDB, bool, IsVisible)

    // === 视口属性 ===

    // 视口尺寸（渲染区域的实际像素尺寸）
    FIELD_VALUE_SIMPLE(WindowDB, Vector2, ViewportSize)

    // DPI 缩放因子
    FIELD_VALUE_SIMPLE(WindowDB, float, DPIScale)

    // 设备像素比率
    FIELD_VALUE_SIMPLE(WindowDB, float, DevicePixelRatio)

    // === 渲染配置 ===

    // 关联的渲染配置ID（预留扩展）
    FIELD_VALUE_SIMPLE(WindowDB, std::string, RenderConfigId)

    // 是否启用 VSync
    FIELD_VALUE_SIMPLE(WindowDB, bool, EnableVSync)

    // 背景颜色（RGBA）
    FIELD_VALUE_SIMPLE(WindowDB, Vector4, BackgroundColor)

    // === 布局和行为配置 ===

    // 是否可调整大小
    FIELD_VALUE_SIMPLE(WindowDB, bool, IsResizable)

    // 最小尺寸
    FIELD_VALUE_SIMPLE(WindowDB, Vector2, MinimumSize)

    // 最大尺寸
    FIELD_VALUE_SIMPLE(WindowDB, Vector2, MaximumSize)

    // 是否置顶显示
    FIELD_VALUE_SIMPLE(WindowDB, bool, AlwaysOnTop)

    // === 公共接口 ===

    /**
     * @brief 设置窗口状态（类型安全的枚举接口）
     */
    void setWindowState(WindowState state) {
        setState(static_cast<int>(state));
    }

    /**
     * @brief 获取窗口状态（类型安全的枚举接口）
     */
    WindowState getWindowState() const {
        return static_cast<WindowState>(getState());
    }

    /**
     * @brief 批量更新窗口几何信息
     * @param position 新位置
     * @param size 新尺寸
     * @param viewportSize 新视口尺寸
     */
    void updateGeometry(const Vector2& position,
                       const Vector2& size,
                       const Vector2& viewportSize);

    /**
     * @brief 获取坐标系统管理器
     * @return ViewportCoordinateSystem 实例
     */
    std::shared_ptr<ViewportCoordinateSystem> getCoordinateSystem() const;

    // View-local association. This is transient runtime state rather than a
    // document transaction field, so switching the active view cannot pollute
    // Undo/Redo.
    bool bindCameraId(const DBInstanceID& cameraId);
    DBInstanceID getCameraId() const noexcept;
    std::shared_ptr<CameraDB> getCamera() const;

    /**
     * @brief 更新坐标系统的活动相机
     *
     * 从 DocumentManager 获取可用相机并设置到 ViewportCoordinateSystem。
     * 这是解决 Widget 拖动坐标转换的关键方法。
     */
    void updateActiveCamera();

    /**
     * @brief 获取DB类型
     */
    TypeID getTypeID() const override {
        return TypeID::WINDOW_DB;
    }

    /**
     * @brief 检查是否需要同步到 VTK 渲染系统
     *
     * WindowDB 管理的是 UI 窗口容器的属性（位置、大小、标题等），
     * 这些属性不需要同步到 VTK 3D 场景，所以返回 false。
     *
     * @return false，表示不需要同步到 VTK
     */
    bool needsVTKSync() const override {
        return false;
    }

    // === 静态工厂方法 ===

    /**
     * @brief 为 RenderThreadQmlItem 创建 WindowDB
     * @param qmlItemName QML组件的对象名称
     * @param initialSize 初始尺寸
     * @return 新创建的 WindowDB 实例
     */
    static std::shared_ptr<WindowDB> createForQmlItem(const QString& qmlItemName,
                                                     const QSize& initialSize);

protected:
    // === AutoRegisterDB 钩子方法 ===

    // WindowDB is runtime view state. It is registered explicitly after its
    // initial properties are ready and never enters document Undo/Redo.
    bool shouldAutoRegister() const override { return false; }

    /**
     * @brief 初始化属性默认值
     * 在对象完全被 shared_ptr 管理后调用
     */
    void initializeProperties() override;

    /**
     * @brief 属性初始化完成后的处理
     * 在属性设置完成后调用
     */
    void afterPropertiesInitialized() override;

private:
    /**
     * @brief 生成基于时间戳的唯一窗口ID
     */
    static std::string generateUniqueWindowId(const QString& baseName);

    // 坐标系统管理器（每个窗口独立）
    std::shared_ptr<ViewportCoordinateSystem> m_coordinateSystem;
    DBInstanceID m_cameraId{INVALID_DB_ID};
};

/**
 * @brief 窗口状态枚举的字符串转换支持
 */
namespace WindowDBHelper {
    QString windowStateToString(WindowDB::WindowState state);
    WindowDB::WindowState stringToWindowState(const QString& str);
}
