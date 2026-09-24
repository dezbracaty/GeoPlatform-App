#include "KeyboardShortcutManager.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_QML_SINGLETON(KeyboardShortcutManager, "KeyboardShortcutManager")
#include "ActionHandlerRegistry.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <algorithm>

KeyboardShortcutManager::KeyboardShortcutManager(QObject* parent)
    : QObject(parent)
    , m_actionManager(ActionManager::getInstance())
{
    LOG_INFO("KeyboardShortcutManager initialized");

    // 初始化配置
    loadConfiguration();
}

KeyboardShortcutManager::~KeyboardShortcutManager() {
    LOG_INFO("KeyboardShortcutManager destroyed");
}

bool KeyboardShortcutManager::handleKeyEvent(int key, int modifiers) {
    // 将键盘事件转换为字符串形式
    QString keySequence = keyEventToString(key, modifiers);

    LOG_DEBUG("Handling key event: key={} modifiers={} sequence='{}'", key, modifiers, keySequence.toStdString());

    // 查找对应的Action
    auto it = m_shortcutMap.find(keySequence);
    if (it == m_shortcutMap.end()) {
        LOG_DEBUG("No action mapped for key sequence: {}", keySequence.toStdString());
        return false;
    }

    QString actionCode = it.value();
    LOG_INFO("Requesting action '{}' for shortcut '{}'",
             actionCode.toStdString(), keySequence.toStdString());

    // Shortcuts execute their explicitly configured registered Action.
    bool result = m_actionManager->triggerAction(actionCode);

    if (!result) {
        LOG_WARN("Failed to request action '{}' for shortcut '{}'",
                 actionCode.toStdString(), keySequence.toStdString());
    }

    return result;
}

void KeyboardShortcutManager::loadConfiguration() {
    LOG_DEBUG("Loading keyboard shortcut configuration");

    // 清空当前映射
    m_shortcutMap.clear();

    // 按优先级加载配置：应用配置 -> 用户配置
    loadApplicationConfig();
    loadUserConfig();
    removeUnregisteredActions();

    LOG_INFO("KeyboardShortcutManager: Loaded {} shortcuts", m_shortcutMap.size());
}

void KeyboardShortcutManager::saveUserConfiguration() {
    LOG_INFO("Saving user keyboard shortcut configuration");

    // 构建用户配置JSON
    QJsonObject userShortcuts;
    for (auto it = m_shortcutMap.begin(); it != m_shortcutMap.end(); ++it) {
        userShortcuts[it.value()] = it.key(); // Action -> KeySequence
    }

    QJsonObject config;
    config["shortcuts"] = userShortcuts;
    config["version"] = "1.0";

    // 保存到用户配置文件
    saveConfigToFile(config, getUserConfigPath());

    LOG_INFO("User configuration saved to: {}", getUserConfigPath().toStdString());
}

void KeyboardShortcutManager::resetToDefaults() {
    LOG_INFO("Resetting keyboard shortcuts to defaults");

    // 清空用户配置文件
    QFile::remove(getUserConfigPath());

    // 重新加载配置
    loadConfiguration();
}

QString KeyboardShortcutManager::getActionForShortcut(const QString& keySequence) {
    auto it = m_shortcutMap.find(keySequence);
    return it != m_shortcutMap.end() ? it.value() : QString();
}

bool KeyboardShortcutManager::setShortcut(const QString& actionCode, const QString& keySequence) {
    const auto registeredActions = ActionHandlerRegistry::instance().getRegisteredActions();
    if (std::find(registeredActions.begin(), registeredActions.end(), actionCode)
        == registeredActions.end()) {
        LOG_WARN("Cannot assign shortcut '{}': action '{}' is not registered",
                 keySequence.toStdString(), actionCode.toStdString());
        return false;
    }

    removeBindingsForAction(actionCode);
    bindShortcut(actionCode, keySequence, QStringLiteral("runtime"));

    LOG_INFO("Set shortcut '{}' for action '{}'",
             keySequence.toStdString(), actionCode.toStdString());

    return true;
}

QStringList KeyboardShortcutManager::getAllShortcuts() {
    QStringList shortcuts;
    shortcuts.reserve(m_shortcutMap.size());
    for (auto it = m_shortcutMap.begin(); it != m_shortcutMap.end(); ++it) {
        shortcuts << it.key() + " -> " + it.value();
    }
    return shortcuts;
}

QString KeyboardShortcutManager::keyEventToString(int key, int modifiers) {
    QStringList parts;

    // 处理修饰键
    if (modifiers & Qt::ControlModifier) {
        parts << "Ctrl";
    }
    if (modifiers & Qt::AltModifier) {
        parts << "Alt";
    }
    if (modifiers & Qt::ShiftModifier) {
        parts << "Shift";
    }
    if (modifiers & Qt::MetaModifier) {
        parts << "Meta";
    }

    // 处理主键
    QString keyName;
    switch (key) {
        case Qt::Key_Space:
            keyName = "Space";
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            keyName = "Enter";
            break;
        case Qt::Key_Escape:
            keyName = "Escape";
            break;
        case Qt::Key_Tab:
            keyName = "Tab";
            break;
        case Qt::Key_Backspace:
            keyName = "Backspace";
            break;
        case Qt::Key_Delete:
            keyName = "Delete";
            break;
        case Qt::Key_F1: case Qt::Key_F2: case Qt::Key_F3: case Qt::Key_F4:
        case Qt::Key_F5: case Qt::Key_F6: case Qt::Key_F7: case Qt::Key_F8:
        case Qt::Key_F9: case Qt::Key_F10: case Qt::Key_F11: case Qt::Key_F12:
            keyName = QString("F%1").arg(key - Qt::Key_F1 + 1);
            break;
        default:
            if (key >= Qt::Key_A && key <= Qt::Key_Z) {
                keyName = QChar(key).toLower();
            } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
                keyName = QChar(key);
            } else {
                keyName = QString::number(key);
            }
            break;
    }

    parts << keyName;
    return parts.join("+");
}

QPair<int, int> KeyboardShortcutManager::stringToKeyEvent(const QString& keySequence) {
    QStringList parts = keySequence.split("+");
    int modifiers = Qt::NoModifier;
    int key = 0;

    for (const QString& part : parts) {
        if (part == "Ctrl") {
            modifiers |= Qt::ControlModifier;
        } else if (part == "Alt") {
            modifiers |= Qt::AltModifier;
        } else if (part == "Shift") {
            modifiers |= Qt::ShiftModifier;
        } else if (part == "Meta") {
            modifiers |= Qt::MetaModifier;
        } else {
            // 这是主键
            if (part == "Space") {
                key = Qt::Key_Space;
            } else if (part == "Enter") {
                key = Qt::Key_Return;
            } else if (part == "Escape") {
                key = Qt::Key_Escape;
            } else if (part == "Tab") {
                key = Qt::Key_Tab;
            } else if (part == "Backspace") {
                key = Qt::Key_Backspace;
            } else if (part == "Delete") {
                key = Qt::Key_Delete;
            } else if (part.startsWith("F") && part.length() <= 3) {
                bool ok;
                int fNum = part.mid(1).toInt(&ok);
                if (ok && fNum >= 1 && fNum <= 12) {
                    key = Qt::Key_F1 + fNum - 1;
                }
            } else if (part.length() == 1) {
                QChar c = part[0];
                if (c.isLetter()) {
                    key = c.toUpper().unicode();
                } else if (c.isDigit()) {
                    key = c.unicode();
                }
            }
        }
    }

    return qMakePair(key, modifiers);
}

QString KeyboardShortcutManager::getUserConfigPath() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + "/keyboard_shortcuts.json";
}

QString KeyboardShortcutManager::getAppConfigPath() {
    // 应用配置文件位于Qt资源系统中
    return ":/qt/qml/GPlatform/config/default_shortcuts.json";
}

void KeyboardShortcutManager::loadApplicationConfig() {
    QJsonObject shortcuts = loadConfigFromFile(getAppConfigPath(), "application");
    loadShortcutConfig(shortcuts, QStringLiteral("application"), false);

    LOG_DEBUG("Loaded {} application config shortcuts", shortcuts.size());
}

void KeyboardShortcutManager::loadUserConfig() {
    QJsonObject shortcuts = loadConfigFromFile(getUserConfigPath(), "user");
    loadShortcutConfig(shortcuts, QStringLiteral("user"), true);

    LOG_DEBUG("Loaded {} user config shortcuts", shortcuts.size());
}

void KeyboardShortcutManager::loadShortcutConfig(const QJsonObject& shortcuts,
                                                 const QString& source,
                                                 bool overrideActionBindings) {
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it) {
        const QString actionCode = it.key();
        const QJsonValue value = it.value();

        if (overrideActionBindings) {
            removeBindingsForAction(actionCode);
        }

        if (value.isString()) {
            bindShortcut(actionCode, value.toString(), source);
        } else if (value.isArray()) {
            const QJsonArray keyArray = value.toArray();
            for (const QJsonValue& keyValue : keyArray) {
                if (keyValue.isString()) {
                    bindShortcut(actionCode, keyValue.toString(), source);
                }
            }
        } else {
            LOG_WARN("Ignoring invalid {} shortcut value for action '{}'",
                     source.toStdString(), actionCode.toStdString());
        }
    }
}

void KeyboardShortcutManager::bindShortcut(const QString& actionCode,
                                           const QString& keySequence,
                                           const QString& source) {
    const QString normalizedKey = keySequence.trimmed();
    if (normalizedKey.isEmpty()) {
        LOG_WARN("Ignoring empty {} shortcut for action '{}'",
                 source.toStdString(), actionCode.toStdString());
        return;
    }

    const auto existing = m_shortcutMap.constFind(normalizedKey);
    if (existing != m_shortcutMap.cend() && existing.value() != actionCode) {
        LOG_WARN("Shortcut conflict in {} config: '{}' reassigned from '{}' to '{}'",
                 source.toStdString(),
                 normalizedKey.toStdString(),
                 existing.value().toStdString(),
                 actionCode.toStdString());
    }
    m_shortcutMap[normalizedKey] = actionCode;
}

void KeyboardShortcutManager::removeBindingsForAction(const QString& actionCode) {
    for (auto it = m_shortcutMap.begin(); it != m_shortcutMap.end();) {
        if (it.value() == actionCode) {
            it = m_shortcutMap.erase(it);
        } else {
            ++it;
        }
    }
}

void KeyboardShortcutManager::removeUnregisteredActions() {
    const auto registeredActions = ActionHandlerRegistry::instance().getRegisteredActions();
    for (auto it = m_shortcutMap.begin(); it != m_shortcutMap.end();) {
        if (std::find(registeredActions.begin(), registeredActions.end(), it.value())
            == registeredActions.end()) {
            LOG_WARN("Ignoring shortcut '{}': action '{}' is not registered",
                     it.key().toStdString(), it.value().toStdString());
            it = m_shortcutMap.erase(it);
        } else {
            ++it;
        }
    }
}

QJsonObject KeyboardShortcutManager::loadConfigFromFile(const QString& filePath, const QString& configType) {
    QFile configFile(filePath);
    if (!configFile.exists()) {
        LOG_DEBUG("{} config file not found: {}", configType.toStdString(), filePath.toStdString());
        return QJsonObject();
    }

    if (!configFile.open(QIODevice::ReadOnly)) {
        LOG_WARN("Cannot open {} config file: {}", configType.toStdString(), filePath.toStdString());
        return QJsonObject();
    }

    QByteArray data = configFile.readAll();
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR("Failed to parse {} config JSON: {}", configType.toStdString(), error.errorString().toStdString());
        return QJsonObject();
    }

    return doc.object()["shortcuts"].toObject();
}

void KeyboardShortcutManager::saveConfigToFile(const QJsonObject& config, const QString& filePath) {
    QFile configFile(filePath);
    if (!configFile.open(QIODevice::WriteOnly)) {
        LOG_ERROR("Cannot open config file for writing: {}", filePath.toStdString());
        return;
    }

    QJsonDocument doc(config);
    configFile.write(doc.toJson());
    configFile.close();
}
