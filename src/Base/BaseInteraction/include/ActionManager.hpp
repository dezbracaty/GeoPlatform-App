#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QMouseEvent>
#include <QKeyEvent>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <QString>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>
#include <QHash>
#include "InteractionInputEvent.hpp"

// 前向声明
class IActionHandlerBase;
class ActionContext;

class ActionManager : public QObject {
    Q_OBJECT

public:
    using HandlerPtr = std::shared_ptr<IActionHandlerBase>;

    static ActionManager* getInstance();


    // 析构函数 - 负责清理所有 handler
    ~ActionManager();

    // QML接口 - 直接触发action
    Q_INVOKABLE bool triggerAction(const QString& actionCode,
                                   const QVariantMap& params = QVariantMap());

    Q_INVOKABLE QVariantMap lastActionOutcome() const { return m_lastActionOutcome; }

    // AI-facing APIs
    Q_INVOKABLE QVariantList listActionsForAI();
    Q_INVOKABLE QVariantList listActionSummariesForAI(); // lightweight: actionCode + title only
    Q_INVOKABLE QVariantMap describeActionForAI(const QString& actionCode);
    Q_INVOKABLE bool triggerActionByAI(const QString& actionCode,
                                       const QVariantMap& params = QVariantMap(),
                                       const QVariantMap& meta = QVariantMap());
    Q_INVOKABLE QVariantMap invokeActionByAI(const QString& actionCode,
                                             const QVariantMap& params = QVariantMap(),
                                             const QVariantMap& meta = QVariantMap());
    Q_INVOKABLE QVariantMap triggerActionSequenceByAI(const QVariantList& steps,
                                                      const QVariantMap& meta = QVariantMap());

    // View-qualified input entry points used by Interaction input routing.
    bool onMousePressEvent(const MouseInputEvent& input);
    bool onMouseMoveEvent(const MouseInputEvent& input);
    bool onMouseReleaseEvent(const MouseInputEvent& input);
    bool onWheelEvent(const WheelInputEvent& input);
    bool onKeyPressEvent(const KeyInputEvent& input);
    bool onKeyReleaseEvent(const KeyInputEvent& input);

    // Compatibility entry points for direct tests and non-viewport callers.
    bool onMousePressEvent(QMouseEvent* event);
    bool onMouseMoveEvent(QMouseEvent* event);
    bool onMouseReleaseEvent(QMouseEvent* event);
    bool onWheelEvent(QWheelEvent* event);
    bool onKeyPressEvent(QKeyEvent* event);
    bool onKeyReleaseEvent(QKeyEvent* event);

    // Unified exit path for the current exclusive handler.
    void deactivateActiveHandler();

    // Environment is supplied by the Interaction layer so BaseInteraction
    // does not depend on the UI environment implementation.
    void setEnvironment(const QString& environment);
    const QString& currentEnvironment() const { return m_currentEnvironment; }

    // 关闭管理
    void stopAcceptingActions();
    bool isAcceptingActions() const { return m_acceptingActions; }

    // 安全的关闭前清理方法 - 无日志记录，避免崩溃
    void cleanupBeforeShutdown();

signals:
    void actionTriggered(const QString& actionCode);
    void actionCompleted(const QString& actionCode, bool success);
    void invocationCompleted(const QString& invocationId, const QVariantMap& outcome);

private slots:
    // 处理Handler的后台请求
    void onHandlerRequestBackground();
    void onHandlerRequestResume();
    void onHandlerRequestExit();
    void onHandlerInvocationCompleted(const QString& invocationId,
                                      const QVariantMap& outcome);

    // 检查活跃Handler是否完成
    void checkActiveHandlerCompletion();

private:
    // 私有构造函数（单例）
    explicit ActionManager(QObject* parent = nullptr);

    bool triggerActionInternal(const QString& actionCode,
                               const QVariantMap& params,
                               bool aiInvoke,
                               const QString& invocationId = QString(),
                               const QVariantMap& invocationMetadata = QVariantMap());
    // 注册Handler（由Registry调用）
    void registerHandler(const QString& actionCode, HandlerPtr handler);

    // Handler管理（内部使用）
    void addBackgroundHandler(HandlerPtr handler);
    void addMiddlewareHandler(HandlerPtr handler);
    void setActiveHandler(HandlerPtr handler);
    HandlerPtr getActiveHandler() const { return m_activeHandler; }
    bool canRunHandler(const HandlerPtr& handler) const;

    // Handler存储
    std::unordered_map<QString, HandlerPtr> m_handlers;

    // 分层Handler管理
    std::vector<HandlerPtr> m_backgroundHandlers; // 后台层（始终活跃）
    std::vector<HandlerPtr> m_middlewareHandlers; // 中间层
    HandlerPtr m_activeHandler;                   // 活跃层（独占）
    HandlerPtr m_suspendedHandler;                // 暂停的Handler（后台执行中）

    QString m_currentEnvironment{QStringLiteral("normal")};
    std::unordered_set<IActionHandlerBase*> m_environmentSuspendedHandlers;

    // 是否接受新的 action
    bool m_acceptingActions = true;

    // Last action outcome snapshot (used by AI sequence receipt).
    QVariantMap m_lastActionOutcome;
    qulonglong m_lastActionOutcomeSeq = 0;
    QHash<QString, QVariantMap> m_pendingInvocations;
};
