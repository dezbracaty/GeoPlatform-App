#pragma once

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <deque>
#include <chrono>

/**
 * @brief FPS监测器
 *
 * 负责监测实际的渲染帧率和性能统计
 */
class FPSMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(double currentFPS READ currentFPS NOTIFY fpsChanged)
    Q_PROPERTY(double averageFPS READ averageFPS NOTIFY fpsChanged)
    Q_PROPERTY(double minFPS READ minFPS NOTIFY fpsChanged)
    Q_PROPERTY(double maxFPS READ maxFPS NOTIFY fpsChanged)
    Q_PROPERTY(double frameTime READ frameTime NOTIFY frameTimeChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY fpsChanged)
    Q_PROPERTY(bool presentationActive READ presentationActive NOTIFY presentationActiveChanged)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit FPSMonitor(QObject* parent = nullptr)
        : QObject(parent), m_targetFPS(60), m_currentFPS(0), m_averageFPS(0), 
          m_minFPS(999), m_maxFPS(0), m_averageFrameTime(0), m_droppedFrames(0), 
          m_totalFrames(0) {
        // Sample on a stable UI cadence rather than emitting one property
        // update for every presented frame.
        auto* sampleTimer = new QTimer(this);
        sampleTimer->setInterval(250);
        connect(sampleTimer, &QTimer::timeout, this, [this]() { updateFPS(); });
        sampleTimer->start();
    }

    /**
     * @brief 记录一帧
     */
    void recordFrame() {
        auto now = std::chrono::high_resolution_clock::now();
        QMutexLocker locker(&m_statsMutex);

        if (!m_presentationActive) {
            return;
        }

        const double timestampMs = std::chrono::duration<double, std::milli>(
                                       now.time_since_epoch())
                                       .count();

        if (m_lastFrameTime.time_since_epoch().count() > 0) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime);
            double frameTimeMs = duration.count() / 1000.0;

            const double targetFrameTime = 1000.0 / m_targetFPS;
            const bool startsNewBurst = frameTimeMs > targetFrameTime * 4.0;
            if (startsNewBurst) {
                m_frameTimes.clear();
            }

            // 检测掉帧
            if (!startsNewBurst && frameTimeMs > targetFrameTime * 1.5) {
                m_recordedDroppedFrames++;
            }
        }

        // 记录每一帧，包括每次按需渲染 burst 的第一帧。
        const size_t maxSize = 4000;
        if (m_frameTimes.size() >= maxSize) {
            m_frameTimes.pop_front();
        }
        m_frameTimes.push_back(timestampMs);

        m_lastFrameTime = now;
        m_totalFrames++;
    }

    /**
     * @brief 设置目标FPS（用于掉帧检测）
     */
    void setTargetFPS(int fps) {
        QMutexLocker locker(&m_statsMutex);
        m_targetFPS = fps;
    }

    /**
     * @brief Mark whether this view is eligible to present frames.
     *
     * Inactive views report zero immediately. Resuming starts a fresh cadence
     * window so the suspended interval is never counted as a dropped frame.
     */
    void setPresentationActive(bool active) {
        QMutexLocker locker(&m_statsMutex);
        if (m_presentationActive == active) {
            return;
        }

        m_presentationActive = active;
        m_frameTimes.clear();
        m_lastFrameTime = {};
        m_currentFPS = 0;
        m_averageFrameTime = 0;
        locker.unlock();

        emit presentationActiveChanged();
        emit fpsChanged();
        emit frameTimeChanged();
    }

    /**
     * @brief 重置统计
     */
    void reset() {
        QMutexLocker locker(&m_statsMutex);
        m_frameTimes.clear();
        m_lastFrameTime = {};
        m_recordedDroppedFrames = 0;
        m_droppedFrames = 0;
        m_totalFrames = 0;
        m_currentFPS = 0;
        m_averageFPS = 0;
        m_minFPS = 999;
        m_maxFPS = 0;
        m_averageFrameTime = 0;
        m_fpsSampleSum = 0;
        m_fpsSampleCount = 0;
        locker.unlock();
        emit fpsChanged();
        emit frameTimeChanged();
        emit droppedFramesChanged();
    }

    // Getters
    double currentFPS() const {
        return m_currentFPS;
    }
    double averageFPS() const {
        return m_averageFPS;
    }
    double minFPS() const {
        return m_minFPS;
    }
    double maxFPS() const {
        return m_maxFPS;
    }
    double frameTime() const {
        return m_averageFrameTime;
    }
    int droppedFrames() const {
        return m_droppedFrames;
    }
    qint64 totalFrames() const {
        QMutexLocker locker(&m_statsMutex);
        return m_totalFrames;
    }
    bool presentationActive() const {
        QMutexLocker locker(&m_statsMutex);
        return m_presentationActive;
    }

    /**
     * @brief 获取性能报告
     */
    Q_INVOKABLE QString getPerformanceReport() const {
        return QString("FPS: %1 (avg: %2) | Frame Time: %3ms | Dropped: %4")
            .arg(m_currentFPS, 0, 'f', 1)
            .arg(m_averageFPS, 0, 'f', 1)
            .arg(frameTime(), 0, 'f', 2)
            .arg(m_droppedFrames);
    }

signals:
    void fpsChanged();
    void frameTimeChanged();
    void droppedFramesChanged();
    void presentationActiveChanged();
    void performanceWarning(const QString& message);

private:
    void updateFPS() {
        auto now = std::chrono::high_resolution_clock::now();
        const double currentTimestampMs = std::chrono::duration<double, std::milli>(
                                              now.time_since_epoch())
                                              .count();

        QMutexLocker locker(&m_statsMutex);

        // 移除1秒窗口之外的帧时间记录
        while (!m_frameTimes.empty()) {
            if (currentTimestampMs - m_frameTimes.front() > 1000) { // 超过1秒
                m_frameTimes.pop_front();
            } else {
                break;
            }
        }

        // A resumed presentation starts a fresh cadence window and must not
        // treat the suspended interval as one very long dropped frame.
        if (m_frameTimes.empty()) {
            m_lastFrameTime = {};
        }

        // Treat a producer/scene-graph stall as zero current FPS while keeping
        // the historical samples available for average/min/max reporting.
        constexpr double activeIdleThresholdMs = 150.0;
        const bool isActive = m_presentationActive && !m_frameTimes.empty()
            && currentTimestampMs - m_frameTimes.back() <= activeIdleThresholdMs;

        if (m_frameTimes.size() > 1) {
            const double elapsedMs = m_frameTimes.back() - m_frameTimes.front();
            m_averageFrameTime = elapsedMs > 0.0
                ? elapsedMs / static_cast<double>(m_frameTimes.size() - 1)
                : 0.0;
        } else {
            m_averageFrameTime = 0;
        }

        m_currentFPS = isActive && m_averageFrameTime > 0.0
            ? 1000.0 / m_averageFrameTime
            : 0.0;

        // 更新统计
        if (m_currentFPS > 0.0) {
            if (m_currentFPS < m_minFPS)
                m_minFPS = m_currentFPS;
            if (m_currentFPS > m_maxFPS)
                m_maxFPS = m_currentFPS;
        }

        // 计算总体平均
        if (m_currentFPS > 0) {
            m_fpsSampleSum += m_currentFPS;
            m_fpsSampleCount++;
        }
        m_averageFPS = m_fpsSampleCount > 0
            ? m_fpsSampleSum / static_cast<double>(m_fpsSampleCount)
            : 0;
        m_droppedFrames = m_recordedDroppedFrames;
        const double currentFPS = m_currentFPS;
        const int targetFPS = m_targetFPS;
        locker.unlock();

        emit fpsChanged();
        emit frameTimeChanged();
        emit droppedFramesChanged();

        // 性能警告
        if (currentFPS > 0 && currentFPS < targetFPS * 0.8) {
            emit performanceWarning(QString("Low FPS: %1 (target: %2)")
                                        .arg(currentFPS, 0, 'f', 1)
                                        .arg(targetFPS));
        }
    }

    mutable QMutex m_statsMutex;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    std::deque<double> m_frameTimes; // 存储最近的帧时间戳（毫秒）

    int m_targetFPS;
    double m_currentFPS;
    double m_averageFPS;
    double m_minFPS;
    double m_maxFPS;
    double m_averageFrameTime; // 平均帧时间（毫秒）
    int m_droppedFrames;
    int m_recordedDroppedFrames{0};
    qint64 m_totalFrames;
    double m_fpsSampleSum{0};
    qint64 m_fpsSampleCount{0};
    bool m_presentationActive{true};
};
