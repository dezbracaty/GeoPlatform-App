#include "WindowDB.hpp"
#include <DocumentManager.hpp>
#include "TransactionManager.hpp"
#include "CameraDB.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "Foundation/Log.h"
#include <QUuid>
#include <QDateTime>
#include <QSize>
#include <QRegularExpression>

// === 构造函数和初始化 ===

WindowDB::WindowDB()
    : AutoRegisterDB()
    , m_coordinateSystem(std::make_shared<ViewportCoordinateSystem>())
{
    LOG_DEBUG("WindowDB created with coordinate system");
}

WindowDB::~WindowDB() {
    LOG_DEBUG("WindowDB destroyed");
}

// === AutoRegisterDB 钩子方法实现 ===

void WindowDB::initializeProperties() {
    // 设置默认属性值
    setPosition(Vector2(100.0f, 100.0f));           // 默认窗口位置
    setSize(Vector2(800.0f, 600.0f));               // 默认窗口尺寸
    setViewportSize(Vector2(800.0f, 600.0f));       // 默认视口尺寸
    setState(static_cast<int>(WindowState::NORMAL)); // 正常状态
    setTitle("GPlatform Window");                    // 默认标题
    setIsVisible(true);                              // 默认可见

    // 视口属性默认值
    setDPIScale(1.0f);                              // 默认DPI缩放
    setDevicePixelRatio(1.0f);                      // 默认设备像素比

    // 渲染配置默认值
    setRenderConfigId("");                          // 无特定渲染配置
    setEnableVSync(true);                           // 默认启用垂直同步
    setBackgroundColor(Vector4(0.2f, 0.2f, 0.2f, 1.0f)); // 深灰色背景

    // 布局配置默认值
    setIsResizable(true);                           // 默认可调整大小
    setMinimumSize(Vector2(320.0f, 240.0f));        // 最小尺寸
    setMaximumSize(Vector2(4096.0f, 2160.0f));      // 最大尺寸（4K）
    setAlwaysOnTop(false);                          // 默认不置顶

    LOG_DEBUG("WindowDB properties initialized with default values - Size: {}x{}, Position: {}x{}",
             getSize().x, getSize().y, getPosition().x, getPosition().y);
}

void WindowDB::afterPropertiesInitialized() {
    // 属性初始化完成，无需额外处理
    LOG_DEBUG("WindowDB properties initialization completed");
}

// === 几何更新方法 ===

void WindowDB::updateGeometry(const Vector2& position,
                             const Vector2& size,
                             const Vector2& viewportSize) {
    // 直接设置属性，简洁高效
    setPosition(position);
    setSize(size);
    setViewportSize(viewportSize);

    LOG_DEBUG("WindowDB geometry updated - Position: ({:.1f}, {:.1f}), "
              "Size: ({:.1f}, {:.1f}), Viewport: ({:.1f}, {:.1f})",
              position.x, position.y, size.x, size.y, viewportSize.x, viewportSize.y);
}

std::shared_ptr<ViewportCoordinateSystem> WindowDB::getCoordinateSystem() const {
    return m_coordinateSystem;
}

bool WindowDB::bindCameraId(const DBInstanceID& cameraId) {
    if (cameraId == INVALID_DB_ID) {
        LOG_ERROR("WindowDB::bindCameraId - invalid CameraDB ID");
        return false;
    }
    auto* docManager = DocumentManager::instance();
    if (!docManager ||
        docManager->getDBInstanceType(cameraId) != TypeID::CAMERA_DB) {
        LOG_ERROR("WindowDB::bindCameraId - ID {} is not a registered CameraDB",
                  cameraId.getValue());
        return false;
    }
    if (m_cameraId != INVALID_DB_ID && m_cameraId != cameraId) {
        LOG_ERROR("WindowDB::bindCameraId - camera binding is immutable (current={}, requested={})",
                  m_cameraId.getValue(), cameraId.getValue());
        return false;
    }

    m_cameraId = cameraId;
    updateActiveCamera();
    return true;
}

DBInstanceID WindowDB::getCameraId() const noexcept {
    return m_cameraId;
}

std::shared_ptr<CameraDB> WindowDB::getCamera() const {
    if (m_cameraId == INVALID_DB_ID) {
        return nullptr;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return nullptr;
    }

    return std::dynamic_pointer_cast<CameraDB>(
        docManager->getDBInstance(m_cameraId));
}

void WindowDB::updateActiveCamera() {
    if (!m_coordinateSystem) {
        return;
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_ERROR("WindowDB::updateActiveCamera - DocumentManager not available");
        return;
    }

    auto cameraDB = getCamera();
    if (cameraDB) {
        m_coordinateSystem->setActiveCamera(cameraDB);
        LOG_DEBUG("WindowDB::updateActiveCamera - Active camera '{}' set for coordinate system",
                  cameraDB->getName());
    } else {
        LOG_DEBUG("WindowDB::updateActiveCamera - Window has no valid associated camera");
    }
}

// === 静态工厂方法 ===

std::shared_ptr<WindowDB> WindowDB::createForQmlItem(const QString& qmlItemName,
                                                     const QSize& initialSize) {
    try {
        // View state is transient and must not enter document Undo/Redo.
        TransientUpdateGuard transientUpdate;
        auto windowDB = trans::TransDB::create<WindowDB>();

        // 设置初始尺寸
        Vector2 size(static_cast<float>(initialSize.width()),
                    static_cast<float>(initialSize.height()));
        windowDB->setSize(size);
        windowDB->setViewportSize(size);

        // 设置标题（使用QML组件名称加时间戳）
        std::string windowTitle = generateUniqueWindowId(qmlItemName);
        windowDB->setTitle(windowTitle);

        LOG_DEBUG("WindowDB created for QML item '{}' with title '{}' and size {}x{}",
                 qmlItemName.toStdString(), windowTitle, initialSize.width(), initialSize.height());

        return windowDB;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to create WindowDB for QML item '{}': {}",
                  qmlItemName.toStdString(), e.what());
        return nullptr;
    }
}

std::string WindowDB::generateUniqueWindowId(const QString& baseName) {
    QString cleanBaseName = baseName.isEmpty() ? "Window" : baseName;

    // 移除特殊字符，只保留字母数字和下划线
    cleanBaseName = cleanBaseName.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_");

    // 添加时间戳确保唯一性
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QString uniqueId = QString("%1_%2").arg(cleanBaseName, timestamp);

    return uniqueId.toStdString();
}

// === 辅助函数实现 ===

namespace WindowDBHelper {
    QString windowStateToString(WindowDB::WindowState state) {
        switch (state) {
            case WindowDB::WindowState::NORMAL:     return "Normal";
            case WindowDB::WindowState::MINIMIZED:  return "Minimized";
            case WindowDB::WindowState::MAXIMIZED:  return "Maximized";
            case WindowDB::WindowState::FULLSCREEN: return "Fullscreen";
            default:                                return "Unknown";
        }
    }

    WindowDB::WindowState stringToWindowState(const QString& str) {
        if (str == "Minimized") return WindowDB::WindowState::MINIMIZED;
        if (str == "Maximized") return WindowDB::WindowState::MAXIMIZED;
        if (str == "Fullscreen") return WindowDB::WindowState::FULLSCREEN;
        return WindowDB::WindowState::NORMAL;
    }
}
