#include "TaskStateNotifier.hpp"
#include "BaseUIRegistration.hpp"
#include "Foundation/Log.h"

// 静态实例
TaskStateNotifier* TaskStateNotifier::s_instance = nullptr;

TaskStateNotifier* TaskStateNotifier::instance() {
    if (!s_instance) {
        s_instance = new TaskStateNotifier();
    }
    return s_instance;
}

TaskStateNotifier* TaskStateNotifier::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    // 返回单例，QML 引擎不会销毁它
    return instance();
}

TaskStateNotifier::TaskStateNotifier(QObject* parent)
    : QObject(parent) {
    LOG_DEBUG("TaskStateNotifier initialized");
}

bool TaskStateNotifier::isBusy() const {
    QMutexLocker locker(&m_mutex);
    return m_isBusy;
}

QString TaskStateNotifier::currentTask() const {
    QMutexLocker locker(&m_mutex);
    return m_currentTask;
}

QString TaskStateNotifier::currentDescription() const {
    QMutexLocker locker(&m_mutex);
    return m_currentDescription;
}

int TaskStateNotifier::taskCount() const {
    QMutexLocker locker(&m_mutex);
    return m_taskStack.size();
}

float TaskStateNotifier::currentProgress() const {
    QMutexLocker locker(&m_mutex);
    return m_currentProgress;
}

QString TaskStateNotifier::beginTask(const QString& taskId, const QString& description) {
    QMutexLocker locker(&m_mutex);

    // 创建任务信息
    TaskInfo info;
    info.taskId = taskId;
    info.description = description;
    info.progress = 0.0f;

    // 压入栈
    m_taskStack.push(info);

    LOG_DEBUG("TaskStateNotifier: Task started '{}' - '{}' (total: {})",
              taskId.toStdString(), description.toStdString(), m_taskStack.size());

    // 更新缓存状态
    updateCachedState();

    // 发送信号（在锁外，避免死锁）
    locker.unlock();
    notifyStateChange();
    emit taskStarted(taskId, description);

    return taskId;
}

void TaskStateNotifier::endTask(const QString& taskId) {
    QMutexLocker locker(&m_mutex);

    // 查找并移除任务
    bool found = false;
    QStack<TaskInfo> newStack;

    while (!m_taskStack.isEmpty()) {
        TaskInfo info = m_taskStack.pop();
        if (info.taskId == taskId) {
            found = true;
            LOG_DEBUG("TaskStateNotifier: Task ended '{}' (remaining: {})",
                      taskId.toStdString(), m_taskStack.size());
        } else {
            newStack.push(info);
        }
    }

    if (!found) {
        LOG_WARN("TaskStateNotifier: Task not found for end: {}", taskId.toStdString());
    }

    m_taskStack = newStack;

    // 更新缓存状态
    updateCachedState();

    // 发送信号
    locker.unlock();
    notifyStateChange();
    emit taskEnded(taskId);
}

void TaskStateNotifier::updateProgress(const QString& taskId, float progress) {
    QMutexLocker locker(&m_mutex);

    // 查找任务并更新进度
    bool found = false;
    for (int i = 0; i < m_taskStack.size(); ++i) {
        if (m_taskStack[i].taskId == taskId) {
            m_taskStack[i].progress = progress;
            found = true;

            // 如果是栈顶任务，更新当前进度
            if (i == m_taskStack.size() - 1) {
                m_currentProgress = progress;
            }
            break;
        }
    }

    if (!found) {
        LOG_WARN("TaskStateNotifier: Task not found for progress update: {}", taskId.toStdString());
        return;
    }

    // 发送信号
    locker.unlock();
    emit currentProgressChanged(progress);
    emit taskProgressUpdated(taskId, progress);
}

void TaskStateNotifier::updateDescription(const QString& taskId, const QString& description) {
    QMutexLocker locker(&m_mutex);

    // 查找任务并更新描述
    bool found = false;
    for (int i = 0; i < m_taskStack.size(); ++i) {
        if (m_taskStack[i].taskId == taskId) {
            m_taskStack[i].description = description;
            found = true;

            // 如果是栈顶任务，更新当前描述
            if (i == m_taskStack.size() - 1) {
                m_currentDescription = description;
            }
            break;
        }
    }

    if (!found) {
        LOG_WARN("TaskStateNotifier: Task not found for description update: {}", taskId.toStdString());
        return;
    }

    LOG_DEBUG("TaskStateNotifier: Task '{}' description updated to '{}'",
              taskId.toStdString(), description.toStdString());

    // 发送信号
    locker.unlock();
    emit currentDescriptionChanged(description);
}

void TaskStateNotifier::updateCachedState() {
    // 注意：调用此方法时已经持有锁
    bool wasBusy = m_isBusy;
    QString oldTask = m_currentTask;
    QString oldDescription = m_currentDescription;
    int oldCount = m_taskStack.size();  // 用于比较的数量

    // 更新缓存
    m_isBusy = !m_taskStack.isEmpty();

    if (!m_taskStack.isEmpty()) {
        // 栈顶任务是当前任务
        const TaskInfo& top = m_taskStack.top();
        m_currentTask = top.taskId;
        m_currentDescription = top.description;
        m_currentProgress = top.progress;
    } else {
        m_currentTask.clear();
        m_currentDescription.clear();
        m_currentProgress = 0.0f;
    }
}

void TaskStateNotifier::notifyStateChange() {
    // 注意：调用此方法时不持有锁
    // 因为状态可能在 notifyStateChange 调用期间被其他线程修改
    // 我们使用 updateCachedState 中更新的值

    bool isBusyValue;
    QString taskValue;
    QString descValue;
    int countValue;
    float progressValue;

    {
        QMutexLocker locker(&m_mutex);
        isBusyValue = m_isBusy;
        taskValue = m_currentTask;
        descValue = m_currentDescription;
        countValue = m_taskStack.size();
        progressValue = m_currentProgress;
    }

    emit isBusyChanged(isBusyValue);
    emit currentTaskChanged(taskValue);
    emit currentDescriptionChanged(descValue);
    emit taskCountChanged(countValue);
    emit currentProgressChanged(progressValue);
}

REGISTER_BASE_UI_QML_SINGLETON_CUSTOM(
    TaskStateNotifier, "TaskStateNotifier", &TaskStateNotifier::create)
