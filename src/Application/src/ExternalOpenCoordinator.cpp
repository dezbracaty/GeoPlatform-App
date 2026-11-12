#include "ExternalOpenCoordinator.hpp"
#include "LocalInstanceEndpoint.hpp"
#include "StartupCoordinator.hpp"
#include <ActionManager.hpp>
#include <DownloadManager.h>
#include <NotificationManager.h>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileOpenEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QUuid>

ExternalOpenCoordinator::ExternalOpenCoordinator(bool dedicatedJournal, QObject* parent)
    : QObject(parent), m_dedicated(dedicatedJournal) {
    qApp->installEventFilter(this);
}
ExternalOpenCoordinator::~ExternalOpenCoordinator() { stop(); }

void ExternalOpenCoordinator::initialize(StartupCoordinator* startup, QQmlApplicationEngine* engine,
                                         LocalInstanceEndpoint* endpoint) {
    m_startup = startup; m_engine = engine; m_endpoint = endpoint;
    connect(startup, &StartupCoordinator::startupReady, this, [this] { activate(); pump(); });
    connect(startup, &StartupCoordinator::changed, this, [this] { if (m_startup->stopped()) stop(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &ExternalOpenCoordinator::stop, Qt::DirectConnection);
    if (m_dedicated) return;
    connect(ActionManager::getInstance(), &ActionManager::invocationCompleted, this,
        [this](const QString& id, const QVariantMap& outcome) {
            if (m_stopped || !m_current || id != m_invocation) return;
            if (outcome.contains("released") && !outcome.value("released").toBool()) {
                const auto error = outcome.value("error").toString();
                finish(error); stop(); return;
            }
            LOG_INFO("[EXTERNAL OPEN] completed request={} success={} status={}",
                m_current->id.toStdString(), outcome.value("success").toBool(), outcome.value("status").toString().toStdString());
            finish(outcome.value("success").toBool() || outcome.value("status") == "cancelled"
                ? QString() : outcome.value("error").toString());
        }, Qt::QueuedConnection);
}

void ExternalOpenCoordinator::ensureDownloads() {
    if (m_downloads) return;
    m_downloads = DownloadManager::getInstance();
    connect(m_downloads, &DownloadManager::downloadCompleted, this, [this](const QString& id, const QString& file) {
        if (m_stopped || !m_current || !m_downloading || m_current->id != id) return;
        m_downloading = false; closeDialog(); importFile(file);
    }, Qt::QueuedConnection);
    connect(m_downloads, &DownloadManager::downloadFailed, this, [this](const QString& id, const QString& error) {
        if (!m_stopped && m_current && m_downloading && m_current->id == id) finish(error);
    }, Qt::QueuedConnection);
    connect(m_downloads, &DownloadManager::downloadCancelled, this, [this](const QString& id) {
        if (!m_stopped && m_current && m_downloading && m_current->id == id) finish();
    }, Qt::QueuedConnection);
}

bool ExternalOpenCoordinator::enqueue(const QVector<ExternalOpenRequest>& requests, QString& error) {
    if (m_stopped || m_dedicated) { error = tr("This application instance does not accept external files."); return false; }
    if (m_queue.size() + (m_current ? 1 : 0) + requests.size() > 32) {
        error = tr("The open-file queue is full. Try again after the current files finish."); return false;
    }
    for (const auto& r : requests) m_queue.enqueue(r);
    QTimer::singleShot(0, this, &ExternalOpenCoordinator::pump);
    return true;
}

void ExternalOpenCoordinator::activate() {
    if (m_startup && m_startup->ready()) m_startup->activateMainWindow();
}

bool ExternalOpenCoordinator::eventFilter(QObject* object, QEvent* event) {
    if (object != qApp || event->type() != QEvent::FileOpen) return false;
    if (m_dedicated || m_stopped) return true;
    const auto* open = static_cast<QFileOpenEvent*>(event);
    ExternalOpenRequest request; QString error;
    const auto url = open->url();
    const bool ok = url.scheme() == "gplatform" ? parseExternalUrl(url, request, error)
        : parseExternalFile(url.isLocalFile() ? url.toLocalFile() : open->file(), QDir::currentPath(), request, error);
    if (!ok) {
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.inputError = error;
        request.rejectedInput = url.toString(QUrl::FullyEncoded);
    }
    if (!enqueue({request}, error)) NotificationManager::instance()->showError(tr("Cannot open file"), error);
    activate();
    return true;
}

void ExternalOpenCoordinator::pump() {
    if (m_stopped || m_dedicated || !m_startup || !m_startup->ready() || m_current || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    if (!m_current->inputError.isEmpty()) { finish(m_current->inputError); return; }
    LOG_INFO("[EXTERNAL OPEN] dispatch request={} remote={}", m_current->id.toStdString(), m_current->remote());
    if (!m_current->remote()) { importFile(m_current->localPath); return; }
    m_downloading = true;
    ensureDownloads();
    if (m_stopped || !m_current) return;
    m_downloads->startExternalDownload(m_current->id, m_current->downloadUrl, m_current->downloadFileName);
    showDownload();
}

void ExternalOpenCoordinator::importFile(const QString& file) {
    const auto receipt = ActionManager::getInstance()->invokeActionWithReceipt("file.import", {{"filePath", file}});
    const auto imported = receipt.value("result").toMap().value("importOutcome").toMap();
    if (!receipt.value("accepted").toBool() || !receipt.value("success").toBool() || !imported.value("success").toBool()) {
        const auto error = receipt.value("error").toString();
        finish(error.isEmpty() ? imported.value("error").toString() : error);
    } else if (imported.value("pending").toBool()) {
        m_invocation = imported.value("invocationId").toString();
    } else finish();
}

void ExternalOpenCoordinator::finish(const QString& error) {
    if (!m_current) return;
    LOG_INFO("[EXTERNAL OPEN] finished request={} error={}", m_current->id.toStdString(), !error.isEmpty());
    if (m_endpoint) m_endpoint->markFinished(m_current->id);
    m_downloading = false; m_invocation.clear(); closeDialog(); m_current.reset();
    if (!error.isEmpty()) NotificationManager::instance()->showError(tr("Cannot open file"), error);
    QTimer::singleShot(0, this, &ExternalOpenCoordinator::pump);
}

void ExternalOpenCoordinator::showDownload() {
    if (!m_current || !m_engine || !m_startup->mainWindow()) return;
    auto* item = m_downloads->getDownload(m_current->id);
    if (!item || (item->status() != "downloading" && item->status() != "paused")) return;
    QQmlComponent component(m_engine, QUrl("qrc:/qt/qml/GPlatform/qml/dialogs/ExternalDownloadDialog.qml"));
    m_dialog = component.createWithInitialProperties({
        {"hostItem", QVariant::fromValue(m_startup->mainWindow()->contentItem())},
        {"downloadItem", QVariant::fromValue(item)}, {"fileName", m_current->downloadFileName},
        {"siteName", m_current->downloadUrl.host()}, {"requestId", m_current->id}});
    if (!m_dialog) { m_downloads->cancelDownload(m_current->id); finish(tr("Cannot show the model download.")); return; }
    connect(m_dialog, SIGNAL(cancelRequested()), this, SLOT(cancelDownload()));
    const auto id = m_current->id;
    connect(m_startup->mainWindow(), &QObject::destroyed, m_dialog, [this, id] {
        cancelCurrentDownload(id);
    });
    connect(m_startup->mainWindow(), &QQuickWindow::closing, m_dialog, [this, id] { cancelCurrentDownload(id); });
    QMetaObject::invokeMethod(m_dialog, "open");
}

void ExternalOpenCoordinator::cancelDownload() {
    if (m_current && m_downloading && m_downloads && sender() &&
        sender()->property("requestId").toString() == m_current->id)
        cancelCurrentDownload(m_current->id);
}
void ExternalOpenCoordinator::cancelCurrentDownload(const QString& requestId) {
    if (!m_current || !m_downloading || m_current->id != requestId || !m_downloads) return;
    const auto id = requestId;
    finish(); // Invalidate queued completion before cancelling the network item.
    m_downloads->cancelDownload(id);
}
void ExternalOpenCoordinator::closeDialog() {
    if (!m_dialog) return;
    m_dialog->setProperty("resolved", true);
    QMetaObject::invokeMethod(m_dialog, "close");
    m_dialog->deleteLater(); m_dialog = nullptr;
}
void ExternalOpenCoordinator::stop() {
    if (m_stopped) return;
    m_stopped = true;
    if (m_current && m_downloading && m_downloads) m_downloads->cancelDownload(m_current->id);
    closeDialog();
    if (m_endpoint) {
        if (m_current) m_endpoint->markFinished(m_current->id);
        for (const auto& r : m_queue) m_endpoint->markFinished(r.id);
        m_endpoint->stop();
    }
    m_current.reset(); m_queue.clear();
}
