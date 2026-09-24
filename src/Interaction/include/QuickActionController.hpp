#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QQmlEngine>
#include <memory>

// 需要完整定义，因为 Q_INVOKABLE 方法返回 TMenuGroup*
#include "MenuGroup.hpp"

// Forward declarations
class SelectionBridge;
class MenuData;

class QuickActionController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString currentContext READ currentContext NOTIFY contextChanged)

public:
    explicit QuickActionController(QObject* parent = nullptr);
    ~QuickActionController() = default;

    // Singleton instance access
    static QuickActionController* getInstance();

    // QML registration
    static QuickActionController* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Initialization
    void initialize();

    // Property accessors
    QString currentContext() const;

    // 直接返回 TMenuGroup*，让 QML 使用 menuGroup.items 获取 TMenuItem 列表
    Q_INVOKABLE TMenuGroup* getMenuGroupForContext(const QString& contextType);
    Q_INVOKABLE QString getCurrentContext();

public slots:
    // Called from C++ side when right-click is detected
    void handleRightClick(const QPointF& position);

    // Called from C++ side when keyboard input is detected
    void handleKeyboardInput(const QString& text, const QPointF& position = QPointF());

    // Selection change handler
    void onSelectionChanged();

signals:
    // Signal to QML to show quick action menu
    void showQuickActionMenu(qreal x, qreal y, const QString& initialText = "");

    // Context change signal
    void contextChanged();

private:
    // Context determination logic
    QString determineCurrentContext() const;
    bool hasSelection() const;
    int getSelectionCount() const;

    static QuickActionController* s_instance;

    // Dependencies
    SelectionBridge* m_selectionBridge = nullptr;
    MenuData* m_menuData = nullptr;

    // Current state
    QString m_currentContext = "NoSelection";
};
