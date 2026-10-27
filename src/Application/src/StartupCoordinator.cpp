#include "StartupCoordinator.hpp"
#include "ActionManager.hpp"
#include "SliceSettingsBridge.hpp"
#include <DocumentManager.hpp>
#include <PrintBedDB.hpp>
#include <WindowDB.hpp>
#include <CameraDB.hpp>
#include <PrintBedTemplateDB.hpp>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QFileInfo>
#include <QUrl>
#include <QSet>
#include <QFutureWatcher>
#include <Foundation/Log.h>
#include <exception>

StartupCoordinator::StartupCoordinator(bool replayMode, QObject* parent)
    : QObject(parent), m_replayMode(replayMode) {
    m_clock.start();
    m_text = tr("Starting GPlatform…");
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &StartupCoordinator::tick);
    m_timer.start();
}

StartupCoordinator::~StartupCoordinator() {
    beginShutdown();
    // Library::open has no cancellation API. Drain pure data work while the
    // application and logger still exist; no late callback may create QObjects.
    try { if (m_presetsScheduled) m_presetTask.waitForFinished(); } catch (...) {}
}
QString StartupCoordinator::version() const { return QCoreApplication::applicationVersion(); }

void StartupCoordinator::setStage(const QString& stage, const QString& text) {
    if (stopped()) return;
    m_stage = stage;
    m_text = text;
    m_waitMs = 0;
    m_lastTick = elapsedMs();
    LOG_INFO("[STARTUP] stage={} elapsedMs={} text={}", stage.toStdString(), elapsedMs(), text.toStdString());
    emit changed();
}

void StartupCoordinator::enqueue(const QString& stage, const QString& text,
                                  std::function<void()> work) {
    const auto epoch = m_epoch;
    QTimer::singleShot(0, this, [this, epoch, stage, text, work=std::move(work)] {
        if (epoch != m_epoch || stopped()) return;
        setStage(stage, text);
        try { work(); }
        catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
        catch (...) { fail(tr("Unexpected startup error")); }
    });
}

void StartupCoordinator::preparePresets(std::function<void()> continuation) {
    if (stopped() || m_presetsScheduled) return;
    m_presetsScheduled = true;
    setStage("Presets", tr("Loading printer and filament presets…"));
    const auto epoch = m_epoch;
    auto* watcher = new QFutureWatcher<GPlatform::SlicingConfigActivationResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, epoch, continuation=std::move(continuation)] {
            watcher->deleteLater();
            if (stopped() || epoch != m_epoch) return;
            try {
                auto result = watcher->future().result();
                if (!result) {
                    const auto error = result.diagnostics.empty() ? tr("Unknown preset error")
                        : QString::fromStdString(result.diagnostics.front().message);
                    fail(error);
                    return;
                }
                SliceSettingsBridge::supplyStartupConfiguration(std::move(result));
                continuation();
            } catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
            catch (...) { fail(tr("Cannot load the slicing presets")); }
        });
    m_presetTask = SliceSettingsBridge::prepareStartupConfiguration();
    watcher->setFuture(m_presetTask);
}

void StartupCoordinator::setSplash(QQuickWindow* window) {
    m_splash = window;
    if (!window) { fail(tr("Cannot create the startup window")); return; }
    window->raise();
    window->requestActivate();
    window->update();
    connect(window, &QQuickWindow::sceneGraphError, this,
        [this](QQuickWindow::SceneGraphError, const QString& message) {
            fail(message);
            // The graphics system itself cannot draw a reliable failure page.
            QCoreApplication::exit(1);
        }, Qt::QueuedConnection);
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (m_bootPresented || stopped()) return;
        m_bootPresented = true;
        LOG_INFO("[STARTUP] splashPresented elapsedMs={}", elapsedMs());
        emit bootstrapPresented();
    }, Qt::QueuedConnection);
}

QString StartupCoordinator::admissionError(const QString& code, bool ai) const {
    if (ready()) return {};
    if (!stopped() && !ai) {
        static const QSet<QString> bootstrap{
            "scene.init", "scene.theme.set", "camera.panRotate",
            "interaction.model", "preview.inspect", "screenshot.capture"};
        if (bootstrap.contains(code)) return {};
    }
    return tr("Application is not ready (%1): %2").arg(m_stage, m_text);
}

void StartupCoordinator::attachMainWindow(QObject* object) {
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window || stopped() || (m_main && m_main != window)) return;
    m_main = window;
    if (m_splash) {
        m_splash->setTransientParent(window);
        m_splash->raise();
        m_splash->requestActivate();
    }
    connect(window, &QQuickWindow::sceneGraphError, this,
        [this](QQuickWindow::SceneGraphError, const QString& error) { fail(error); },
        Qt::QueuedConnection);
}

void StartupCoordinator::attachWorkspace(QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || m_sceneScheduled || stopped()) return;
    m_renderer = item;
    m_viewId = item->property("viewId").toULongLong();
    if (!m_viewId) { fail(tr("Prepare view has no document identity")); return; }
    m_sceneScheduled = true;
    connect(item, SIGNAL(hasPresentedFrameChanged()), this, SLOT(checkFrame()));
    connect(item, SIGNAL(renderingErrorChanged()), this, SLOT(checkFrame()));
    connect(item, &QObject::destroyed, this, [this] {
        if (!ready() && !stopped()) fail(tr("Prepare view was destroyed during startup"));
    });
    enqueue("Scene", tr("Initializing print platform…"), [this] { initializeScene(); });
}

void StartupCoordinator::initializeScene() {
    const auto view = std::dynamic_pointer_cast<WindowDB>(
        DocumentManager::instance()->getDBInstance(DBInstanceID(m_viewId)));
    if (!view || !view->getCamera()) {
        fail(tr("Prepare view has no valid camera binding"));
        return;
    }
    auto* settings = SliceSettingsBridge::instance();
    if (!settings->schemaLoaded() || !settings->configurationError().isEmpty() ||
        settings->selectedMachineModelId().isEmpty() || settings->selectedProcessPresetId().isEmpty() ||
        settings->filamentSlots().isEmpty()) {
        fail(tr("Printer, process or filament configuration is unavailable: %1")
             .arg(settings->configurationError()));
        return;
    }
    const auto machine = settings->selectedMachine();
    const auto epoch = m_epoch;
    const auto receipt = ActionManager::getInstance()->invokeActionWithReceipt(
        "scene.init", {{"printBed", machine}}, {{"startupEpoch", epoch}});
    const auto result = receipt.value("result").toMap();
    const auto bedId = result.value("printBedId").toString().toLongLong();
    const auto bed = std::dynamic_pointer_cast<PrintBedDB>(
        DocumentManager::instance()->getDBInstance(DBInstanceID(bedId)));
    const auto definition = bed ? bed->getTemplateDB() : nullptr;
    if (epoch != m_epoch || stopped()) return;
    if (receipt.value("actionCode").toString() != "scene.init" ||
        receipt.value("seq").toULongLong() == 0 ||
        receipt.value("metadata").toMap().value("startupEpoch").toULongLong() != epoch ||
        settings->selectedMachine() != machine ||
        !receipt.value("accepted").toBool() || !receipt.value("success").toBool() ||
        receipt.value("pending").toBool() || !result.value("sceneReady").toBool() ||
        !definition || definition->getModelId() != machine.value("modelId").toString().toStdString() ||
        definition->getVariantId() != machine.value("variantId").toString().toStdString()) {
        fail(tr("Print platform initialization failed: %1").arg(receipt.value("error").toString()));
        return;
    }
    m_sceneReady = true;
    setStage("FirstFrame", tr("Preparing 3D view…"));
    if (m_main) {
        if (!m_main->isVisible()) m_main->show();
        m_main->update();
    }
    checkFrame();
}

void StartupCoordinator::checkFrame() {
    if (stopped() || ready() || !m_sceneReady || !m_renderer) return;
    const QString error = m_renderer->property("renderingError").toString();
    if (!error.isEmpty()) { fail(error); return; }
    if (!m_main || !m_main->isVisible() ||
        m_main->windowState() == Qt::WindowMinimized || !m_renderer->isVisible() ||
        m_renderer->width() <= 0 || m_renderer->height() <= 0 ||
        m_renderer->property("viewId").toULongLong() != m_viewId ||
        !m_renderer->property("renderActive").toBool() ||
        !m_renderer->property("hasPresentedFrame").toBool()) return;
    setStage("Ready", tr("Ready"));
    m_timer.stop();
    // The main window is already visible; hiding the Tool window cannot leave
    // a gap with no application windows or retain its input/focus ownership.
    if (m_splash) m_splash->hide();
    qGuiApp->setQuitOnLastWindowClosed(true);
    m_main->raise();
    m_main->requestActivate();
    emit startupReady();
}

void StartupCoordinator::tick() {
    const auto now = elapsedMs();
    const auto delta = now - m_lastTick;
    m_lastTick = now;
    if (ready() || stopped()) return;
    if (m_stage == "FirstFrame") checkFrame();
    if (ready() || stopped()) return;
    const bool waitingForShell = m_stage == "UiShell";
    auto* window = m_stage == "Bootstrap" || (waitingForShell && !m_main)
        ? m_splash.data() : m_main.data();
    if ((m_stage == "Bootstrap" || m_stage == "FirstFrame" || waitingForShell) && window &&
        window->isExposed() && window->windowState() != Qt::WindowMinimized &&
        qGuiApp->applicationState() == Qt::ApplicationActive) {
        m_waitMs += delta;
        window->update();
        if (m_waitMs >= 30000)
            fail(waitingForShell ? tr("Timed out while creating the print workspace")
                                 : tr("Timed out while waiting for the first visible frame"));
    }
    emit changed();
}

void StartupCoordinator::fail(const QString& message) {
    if (stopped() || ready()) return;
    ++m_epoch;
    m_sceneReady = false;
    m_stage = "Failed";
    m_error = message;
    m_text = tr("Startup failed");
    LOG_ERROR("[STARTUP] failed elapsedMs={} error={}", elapsedMs(), message.toStdString());
    emit changed();
    if (m_replayMode) QCoreApplication::exit(1);
}

void StartupCoordinator::beginShutdown() {
    if (m_stage == "ShuttingDown") return;
    ++m_epoch;
    m_sceneReady = false;
    m_stage = "ShuttingDown";
    m_timer.stop();
    emit changed();
}
void StartupCoordinator::requestExit() { beginShutdown(); QCoreApplication::exit(0); }
void StartupCoordinator::requestRestart() { beginShutdown(); emit restartRequested(); }
void StartupCoordinator::showLogs() {
    const QString directory = Log::g_file_path.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs"
        : QFileInfo(Log::g_file_path).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}
