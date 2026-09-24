#pragma once

#include <BridgeBase.hpp>
#include <QObject>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <stdafx.h>

/**
 * @brief ModelSettingsBridge - Manages global model manipulation settings
 *
 * This class provides QML-accessible global settings for model manipulation,
 * such as snap-to-ground functionality.
 */
class ModelSettingsBridge : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY_AUTO(bool, snapToGround)
    QML_ELEMENT
    QML_SINGLETON

public:
    // Singleton pattern
    static ModelSettingsBridge* instance();

    // QML singleton provider
    static ModelSettingsBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Property accessors（由宏自动生成）
    // bool snapToGround() const;
    // void snapToGround(bool value);

    // 手动实现的 setter（带日志）- 用于外部调用
    void updateSnapToGround(bool value);

signals:
    // 信号由宏自动生成
    // void snapToGroundChanged();

private:
    ModelSettingsBridge(QObject* parent = nullptr);
    ~ModelSettingsBridge() = default;

    // Disable copy and move
    ModelSettingsBridge(const ModelSettingsBridge&) = delete;
    ModelSettingsBridge& operator=(const ModelSettingsBridge&) = delete;
    ModelSettingsBridge(ModelSettingsBridge&&) = delete;
    ModelSettingsBridge& operator=(ModelSettingsBridge&&) = delete;

    // 成员变量由宏自动生成
    // bool m_snapToGround = true;  // Default to true for safety
};