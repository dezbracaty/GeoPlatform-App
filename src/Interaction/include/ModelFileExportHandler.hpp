#pragma once
#include "StandardBackgroundHandler.hpp"
#include <libslicer/Library.hpp>
#include <QFutureWatcher>
#include <atomic>

class ModelFileExportHandler final : public StandardBackgroundHandler {
    Q_OBJECT
public:
    explicit ModelFileExportHandler(QObject* parent = nullptr);
    ~ModelFileExportHandler() override;
    std::optional<QVariantMap> getAIDescriptor(const QString&) const override;
protected:
    bool onInitialize() override;
    void onBackgroundStarted() override;
    void onBackgroundCompleted() override;
    QVariant getCompletionResult() const override { return m_result; }
    QString getTaskDescription() const override { return tr("Exporting models…"); }
private:
    struct Snapshot;
    std::shared_ptr<Snapshot> m_snapshot;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QFutureWatcher<QVariantMap> m_worker;
    QVariantMap m_result;
};
