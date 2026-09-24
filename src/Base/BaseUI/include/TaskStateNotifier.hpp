#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>
#include <QJSEngine>
#include <QMutex>
#include <QStack>
#include <memory>

/**
 * @brief 任务状态通知器 - 全局 UI 任务状态管理
 *
 * 作为 QML 可访问的全局状态接口，用于：
 * - 跟踪后台任务的执行状态
 * - 提供给 QML 绑定的 isBusy、currentTask 等属性
 * - 与 StandardBackgroundHandler 自动集成
 *
 * 设计：
 * - 线程安全，支持从任意线程调用
 * - 使用栈式管理，支持多个并发任务
 * - LIFO 显示最新任务
 */
class TaskStateNotifier : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // QML 可访问的属性
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY isBusyChanged)
    Q_PROPERTY(QString currentTask READ currentTask NOTIFY currentTaskChanged)
    Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY currentDescriptionChanged)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY taskCountChanged)
    Q_PROPERTY(float currentProgress READ currentProgress NOTIFY currentProgressChanged)

public:
    /**
     * @brief 获取单例实例
     */
    static TaskStateNotifier* instance();

    /**
     * @brief QML 单例工厂方法
     */
    static TaskStateNotifier* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // 属性访问器
    bool isBusy() const;
    QString currentTask() const;
    QString currentDescription() const;
    int taskCount() const;
    float currentProgress() const;

    /**
     * @brief 开始一个任务
     * @param taskId 任务唯一标识
     * @param description 任务描述（显示给用户）
     * @return 任务句柄（可用于后续更新）
     */
    Q_INVOKABLE QString beginTask(const QString& taskId, const QString& description);

    /**
     * @brief 结束一个任务
     * @param taskId 任务唯一标识
     */
    Q_INVOKABLE void endTask(const QString& taskId);

    /**
     * @brief 更新任务进度
     * @param taskId 任务唯一标识
     * @param progress 进度值 (0.0 - 1.0)
     */
    Q_INVOKABLE void updateProgress(const QString& taskId, float progress);

    /**
     * @brief 更新任务描述
     * @param taskId 任务唯一标识
     * @param description 新描述
     */
    Q_INVOKABLE void updateDescription(const QString& taskId, const QString& description);

signals:
    void isBusyChanged(bool isBusy);
    void currentTaskChanged(const QString& taskId);
    void currentDescriptionChanged(const QString& description);
    void taskCountChanged(int count);
    void currentProgressChanged(float progress);

    // 任务生命周期信号
    void taskStarted(const QString& taskId, const QString& description);
    void taskEnded(const QString& taskId);
    void taskProgressUpdated(const QString& taskId, float progress);

private:
    TaskStateNotifier(QObject* parent = nullptr);
    ~TaskStateNotifier() = default;
    Q_DISABLE_COPY(TaskStateNotifier)

    // 任务信息结构
    struct TaskInfo {
        QString taskId;
        QString description;
        float progress = 0.0f;
    };

    // 线程安全的任务栈
    mutable QMutex m_mutex;
    QStack<TaskInfo> m_taskStack;

    // 当前状态缓存（用于属性访问）
    bool m_isBusy = false;
    QString m_currentTask;
    QString m_currentDescription;
    float m_currentProgress = 0.0f;

    // 内部方法
    void updateCachedState();
    void notifyStateChange();

    // 静态实例
    static TaskStateNotifier* s_instance;
};
