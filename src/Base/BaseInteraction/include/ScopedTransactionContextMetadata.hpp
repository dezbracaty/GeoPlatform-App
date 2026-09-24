#pragma once

#include <TransactionManager.hpp>

#include <QString>
#include <QVariantMap>

inline TransactionManager::TransactionMetadata buildActionTransactionMetadata(
    const QString& actionCode,
    const QVariantMap& metadata) {
    TransactionManager::TransactionMetadata transactionMetadata;
    transactionMetadata.emplace("actionCode", actionCode.toStdString());

    const auto addIfPresent = [&metadata, &transactionMetadata](const char* key) {
        const QString value = metadata.value(QString::fromLatin1(key)).toString().trimmed();
        if (!value.isEmpty()) {
            transactionMetadata.emplace(key, value.toStdString());
        }
    };
    addIfPresent("source");
    addIfPresent("turnId");
    addIfPresent("traceId");
    addIfPresent("toolCallId");
    addIfPresent("requestId");
    addIfPresent("invocationId");
    return transactionMetadata;
}

class ScopedTransactionContextMetadata final {
public:
    explicit ScopedTransactionContextMetadata(
        const TransactionManager::TransactionMetadata& metadata,
        bool enabled = true)
        : m_enabled(enabled) {
        if (!m_enabled) {
            return;
        }
        auto& manager = TransactionManager::instance();
        m_previous = manager.currentContextMetadata();
        manager.setCurrentContextMetadata(metadata);
    }

    ScopedTransactionContextMetadata(const QString& actionCode,
                                     const QVariantMap& metadata,
                                     bool enabled = true)
        : ScopedTransactionContextMetadata(
              buildActionTransactionMetadata(actionCode, metadata), enabled) {
    }

    ~ScopedTransactionContextMetadata() {
        if (m_enabled) {
            TransactionManager::instance().setCurrentContextMetadata(m_previous);
        }
    }

    ScopedTransactionContextMetadata(const ScopedTransactionContextMetadata&) = delete;
    ScopedTransactionContextMetadata& operator=(const ScopedTransactionContextMetadata&) = delete;

private:
    bool m_enabled = false;
    TransactionManager::TransactionMetadata m_previous;
};
