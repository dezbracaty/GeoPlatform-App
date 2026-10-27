#pragma once

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <QFuture>
#include <SlicingBackend.hpp>

class QQuickWindow;
class QQuickItem;

// Application-owned startup state. Both QML engines observe this C++ object;
// neither engine owns it and neither can set ready.
class StartupCoordinator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QString stageText READ stageText NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool sceneReady READ sceneReady NOTIFY changed)
    Q_PROPERTY(bool replayMode READ replayMode CONSTANT)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY changed)
public:
    explicit StartupCoordinator(bool replayMode, QObject* parent = nullptr);
    ~StartupCoordinator() override;
    QString stage() const { return m_stage; }
    QString stageText() const { return m_text; }
    QString errorMessage() const { return m_error; }
    QString version() const;
    bool ready() const { return m_stage == "Ready"; }
    bool sceneReady() const { return m_sceneReady; }
    bool replayMode() const { return m_replayMode; }
    qint64 elapsedMs() const { return m_clock.elapsed(); }
    bool stopped() const { return m_stage == "Failed" || m_stage == "ShuttingDown"; }
    void setStage(const QString& stage, const QString& text);
    void enqueue(const QString& stage, const QString& text, std::function<void()> work);
    void setSplash(QQuickWindow* window);
    void preparePresets(std::function<void()> continuation);
    QString admissionError(const QString& code, bool ai) const;
    void fail(const QString& message);
    void beginShutdown();
    Q_INVOKABLE void attachMainWindow(QObject* window);
    Q_INVOKABLE void attachWorkspace(QObject* renderer);
    Q_INVOKABLE void requestExit();
    Q_INVOKABLE void requestRestart();
    Q_INVOKABLE void showLogs();
signals:
    void changed();
    void bootstrapPresented();
    void startupReady();
    void restartRequested();
private slots:
    void checkFrame();
private:
    void initializeScene();
    void tick();
    QString m_stage{"Bootstrap"};
    QString m_text;
    QString m_error;
    bool m_sceneReady{false};
    bool m_replayMode{false};
    bool m_bootPresented{false};
    bool m_sceneScheduled{false};
    bool m_presetsScheduled{false};
    quint64 m_epoch{1};
    qulonglong m_viewId{0};
    qint64 m_waitMs{0};
    qint64 m_lastTick{0};
    QFuture<GPlatform::SlicingConfigActivationResult> m_presetTask;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QPointer<QQuickWindow> m_splash;
    QPointer<QQuickWindow> m_main;
    QPointer<QQuickItem> m_renderer;
};
