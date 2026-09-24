#pragma once

#include <QObject>
#include <QKeyEvent>
#include <QMap>
#include <QString>
#include <qqmlregistration.h>
#include "stdafx.h"
#include "ActionManager.hpp"
#include "Foundation/Log.h"

/**
 * @brief 键盘快捷键管理器
 *
 * 职责：
 * 1. 处理键盘事件并映射到对应的Action
 * 2. 管理快捷键配置（JSON格式）
 * 3. 支持两层配置优先级：用户配置 > 应用配置
 */
class KeyboardShortcutManager : public QObject {
    Q_OBJECT
    QML_SINGLETON

public:
    // 单例模式
    SINGLETON(KeyboardShortcutManager)

    // 析构函数
    ~KeyboardShortcutManager();

    // QML接口 - 处理键盘事件
    Q_INVOKABLE bool handleKeyEvent(int key, int modifiers);

    // 配置管理
    Q_INVOKABLE void loadConfiguration();
    Q_INVOKABLE void saveUserConfiguration();
    Q_INVOKABLE void resetToDefaults();

    // 快捷键查询和设置
    Q_INVOKABLE QString getActionForShortcut(const QString& keySequence);
    Q_INVOKABLE bool setShortcut(const QString& actionCode, const QString& keySequence);
    Q_INVOKABLE QStringList getAllShortcuts();


private:
    // 私有构造函数（单例模式）
    KeyboardShortcutManager(QObject* parent = nullptr);

    // 键序列转换
    QString keyEventToString(int key, int modifiers);
    QPair<int, int> stringToKeyEvent(const QString& keySequence);

    // 配置文件路径
    QString getUserConfigPath();
    QString getAppConfigPath();

    // 配置加载和保存
    void loadApplicationConfig();
    void loadUserConfig();
    void loadShortcutConfig(const QJsonObject& shortcuts, const QString& source,
                            bool overrideActionBindings);
    void bindShortcut(const QString& actionCode, const QString& keySequence,
                      const QString& source);
    void removeBindingsForAction(const QString& actionCode);
    void removeUnregisteredActions();
    QJsonObject loadConfigFromFile(const QString& filePath, const QString& configType);
    void saveConfigToFile(const QJsonObject& config, const QString& filePath);

    // 快捷键映射（键序列 -> Action代码）
    QMap<QString, QString> m_shortcutMap;


    // ActionManager引用
    ActionManager* m_actionManager;
};
