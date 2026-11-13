#pragma once
#include "ExternalOpenRequest.hpp"
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <optional>
class StartupCoordinator;
class QQmlApplicationEngine;
class LocalInstanceEndpoint;
class DownloadManager;

class ExternalOpenCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit ExternalOpenCoordinator(bool dedicatedJournal, QObject* parent = nullptr);
    ~ExternalOpenCoordinator() override;
    void initialize(StartupCoordinator* startup, QQmlApplicationEngine* engine,
                    LocalInstanceEndpoint* endpoint);
    bool enqueue(const QVector<ExternalOpenRequest>& requests, QString& error);
    void activate();
    void stop();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private slots:
    void pump();
    void cancelDownload();
private:
    void importFile(const QString& file);
    void finish(const QString& error = {});
    void closeDialog();
    void cancelCurrentDownload(const QString& id);
    void showDownload();
    void ensureDownloads();
    bool m_dedicated;
    bool m_stopped{false};
    bool m_downloading{false};
    QPointer<StartupCoordinator> m_startup;
    QPointer<QQmlApplicationEngine> m_engine;
    QPointer<DownloadManager> m_downloads;
    QPointer<QObject> m_dialog;
    LocalInstanceEndpoint* m_endpoint{nullptr};
    QQueue<ExternalOpenRequest> m_queue;
    std::optional<ExternalOpenRequest> m_current;
    QString m_invocation;
};
