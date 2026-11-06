#pragma once

#include "StandardBackgroundHandler.hpp"
#include "StandardActionHandler.hpp"
#include "SlicingBackend.hpp"
#include <QFutureWatcher>
#include <QPointer>
#include <atomic>

// One application workflow for project replacement and geometry append.
// Preparation owns values; only commit() may edit the document.
class ProjectImportHandler final : public StandardBackgroundHandler {
    Q_OBJECT
public:
    explicit ProjectImportHandler(QObject* parent = nullptr);
    ~ProjectImportHandler() override;
    void cancelImport();
    bool onKeyPressEvent(QKeyEvent*) override;
    std::optional<QVariantMap> getAIDescriptor(const QString&) const override;
protected:
    bool onInitialize() override;
    void onBackgroundStarted() override;
    void onBackgroundCompleted() override;
    void onCleanup() override;
    bool shouldKeepActive() const override { return m_pending; }
    QVariant getCompletionResult() const override { return m_completion; }
    const QHash<QString, QVariantMap>& aiDescriptorTable() const;
private slots:
    void resolveDecision(QVariant);
    void workerFinished();
private:
    void startRead();
    void consumeCandidate();
    void commit();
    bool showDecision(const QString& stage, const QString& question = {});
    void cancelPendingRead();
    void closeDecision();
    void finish(QString status, QString code = {}, QString message = {});
    QString m_path, m_intent, m_requestId, m_sourceHash, m_dialogStage;
    std::uint64_t m_revision{0};
    QVariantMap m_parameters, m_completion;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    struct DecisionWait;
    std::shared_ptr<DecisionWait> m_decisionWait;
    QFutureWatcher<libslicer::ProjectReadResult> m_watcher;
    libslicer::ProjectReadResult m_candidate;
    QPointer<QObject> m_dialog;
    bool m_pending{false}, m_terminal{false}, m_resuming{false};
};

// Existing workflow controls: cancellation and active-plate selection.
class ProjectControlHandler final : public StandardActionHandler {
    Q_OBJECT
public:
    using StandardActionHandler::StandardActionHandler;
    HandlerType getHandlerType() const override { return HandlerType::Middleware; }
    void onEnter(std::shared_ptr<ActionContext>) override;
protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;
};
