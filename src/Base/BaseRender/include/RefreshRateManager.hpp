#pragma once

#include <QObject>
#include <QScreen>
#include <QGuiApplication>
#include <memory>
#include "Foundation/Log.h"

/**
 * @brief 刷新率管理器
 *
 * 负责管理应用程序的刷新率设置，支持：
 * - 自动检测屏幕刷新率
 * - 手动设置刷新率
 * - 提供不同用途的定时器间隔
 */
class RefreshRateManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 刷新率模式
     */
    enum class RefreshMode {
        Auto,     // 自动检测屏幕刷新率
        Fixed60,  // 固定60Hz
        Fixed120, // 固定120Hz
        Fixed144, // 固定144Hz
        Custom    // 自定义刷新率
    };

    /**
     * @brief 获取单例实例
     */
    static RefreshRateManager& instance() {
        static RefreshRateManager instance;
        return instance;
    }

    /**
     * @brief 获取当前刷新率（Hz）
     */
    int getRefreshRate() const {
        return m_currentRefreshRate;
    }

    /**
     * @brief 获取帧间隔（毫秒）
     */
    int getFrameIntervalMs() const {
        return 1000 / m_currentRefreshRate;
    }

    /**
     * @brief 获取事件处理定时器间隔（毫秒）
     * 事件处理可以比渲染更频繁，确保响应性
     */
    int getEventTimerIntervalMs() const {
        // 事件处理频率可以是刷新率的2倍，确保低延迟
        return std::max(8, 500 / m_currentRefreshRate);
    }

    /**
     * @brief 获取渲染定时器间隔（毫秒）
     */
    int getRenderTimerIntervalMs() const {
        return getFrameIntervalMs();
    }

    /**
     * @brief 设置刷新率模式
     */
    void setRefreshMode(RefreshMode mode, int customRate = 60) {
        m_mode = mode;

        switch (mode) {
            case RefreshMode::Auto:
                detectScreenRefreshRate();
                break;
            case RefreshMode::Fixed60:
                m_currentRefreshRate = 60;
                break;
            case RefreshMode::Fixed120:
                m_currentRefreshRate = 120;
                break;
            case RefreshMode::Fixed144:
                m_currentRefreshRate = 144;
                break;
            case RefreshMode::Custom:
                m_currentRefreshRate = std::clamp(customRate, 30, 240);
                break;
        }

        emit refreshRateChanged(m_currentRefreshRate);
    }

    /**
     * @brief 检测屏幕刷新率
     */
    void detectScreenRefreshRate() {
        if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
            if (auto* screen = app->primaryScreen()) {
                // QScreen::refreshRate() 返回的是 qreal (double)
                qreal rate = screen->refreshRate();
                m_currentRefreshRate = static_cast<int>(std::round(rate));

                // 验证合理范围
                if (m_currentRefreshRate < 30 || m_currentRefreshRate > 240) {
                    // 如果检测到不合理的值，使用默认60Hz
                    m_currentRefreshRate = 60;
                }

                LOG_DEBUG("Detected screen refresh rate: {} Hz", m_currentRefreshRate);
            }
        }
    }

    /**
     * @brief 获取当前模式
     */
    RefreshMode getMode() const {
        return m_mode;
    }

signals:
    /**
     * @brief 刷新率变化信号
     */
    void refreshRateChanged(int newRate);

private:
    RefreshRateManager(QObject* parent = nullptr)
        : QObject(parent), m_currentRefreshRate(60), m_mode(RefreshMode::Auto) {
        // 初始化时自动检测
        detectScreenRefreshRate();

        // 监听屏幕变化
        if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
            connect(app, &QGuiApplication::primaryScreenChanged,
                    this, [this](QScreen* screen) {
                        if (m_mode == RefreshMode::Auto && screen) {
                            detectScreenRefreshRate();
                        }
                    });

            // 监听主屏幕的刷新率变化
            if (auto* screen = app->primaryScreen()) {
                connect(screen, &QScreen::refreshRateChanged,
                        this, [this](qreal) {
                            if (m_mode == RefreshMode::Auto) {
                                detectScreenRefreshRate();
                            }
                        });
            }
        }
    }

    ~RefreshRateManager() = default;

    // 禁用拷贝
    RefreshRateManager(const RefreshRateManager&) = delete;
    RefreshRateManager& operator=(const RefreshRateManager&) = delete;

    int m_currentRefreshRate;
    RefreshMode m_mode;
};
