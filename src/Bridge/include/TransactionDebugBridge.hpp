#pragma once

#include <BridgeBase.hpp>
#include <DocumentManager.hpp>
#include <QObject>
#include <QMutex>
#include <QVariantList>
#include <QHash>
#include <qqmlregistration.h>
#include <QQmlEngine>
#include <QJSEngine>

/**
 * @brief TransactionDebugBridge - 事务与数据变更调试桥接
 *
 * 提供 QML 可访问的调试数据：
 * - 事务历史（提交后的事务）
 * - 实时变更流（DocumentManager 通知）
 * - 事务状态（进行中、undo/redo 栈大小）
 */
class TransactionDebugBridge : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList transactions READ transactions NOTIFY transactionsChanged)
    Q_PROPERTY(QVariantList changes READ changes NOTIFY changesChanged)
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY capturingChanged)
    Q_PROPERTY(bool runtimeEnabled READ runtimeEnabledQml CONSTANT)
    Q_PROPERTY(bool inTransaction READ inTransaction NOTIFY inTransactionChanged)
    Q_PROPERTY(QString currentTransactionDescription READ currentTransactionDescription
               NOTIFY currentTransactionDescriptionChanged)
    Q_PROPERTY(int undoStackSize READ undoStackSize NOTIFY undoStackSizeChanged)
    Q_PROPERTY(int redoStackSize READ redoStackSize NOTIFY redoStackSizeChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY transactionsChanged)
    Q_PROPERTY(int changeCount READ changeCount NOTIFY changesChanged)
    Q_PROPERTY(int maxTransactions READ maxTransactions WRITE setMaxTransactions NOTIFY maxTransactionsChanged)
    Q_PROPERTY(int maxChanges READ maxChanges WRITE setMaxChanges NOTIFY maxChangesChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    static void setRuntimeEnabled(bool enabled);
    static bool runtimeEnabled();

    static TransactionDebugBridge* instance();
    static TransactionDebugBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    QVariantList transactions() const;
    QVariantList changes() const;

    bool capturing() const { return m_capturing; }
    void setCapturing(bool enabled);
    bool runtimeEnabledQml() const { return runtimeEnabled(); }

    bool inTransaction() const { return m_inTransaction; }
    QString currentTransactionDescription() const { return m_currentTransactionDescription; }
    int undoStackSize() const { return m_undoStackSize; }
    int redoStackSize() const { return m_redoStackSize; }
    int transactionCount() const;
    int changeCount() const;

    int maxTransactions() const { return m_maxTransactions; }
    void setMaxTransactions(int value);

    int maxChanges() const { return m_maxChanges; }
    void setMaxChanges(int value);

    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void clearTransactions();
    Q_INVOKABLE void clearChanges();
    Q_INVOKABLE QVariantMap getTransactionById(const QString& txId) const;
    Q_INVOKABLE QVariantList getTransactionChangesById(const QString& txId) const;
    Q_INVOKABLE QString snapshotAsJson() const;

signals:
    void transactionsChanged();
    void changesChanged();
    void capturingChanged();
    void inTransactionChanged();
    void currentTransactionDescriptionChanged();
    void undoStackSizeChanged();
    void redoStackSizeChanged();
    void maxTransactionsChanged();
    void maxChangesChanged();

private:
    explicit TransactionDebugBridge(QObject* parent = nullptr);
    ~TransactionDebugBridge() override;

    void setupTransactionListeners();
    void setupDocumentListener();
    void removeListeners();
    void refreshStackState();
    void syncTransactionState();
    QString readCurrentPropertyValue(const DocumentManager::ChangeNotification& notification) const;
    QString makePropertySnapshotKey(const DocumentManager::ChangeNotification& notification) const;
    void appendTransaction(const QVariantMap& item);
    void appendChange(const QVariantMap& item);
    void trimList(QVariantList& list, int maxSize);

private:
    mutable QMutex m_mutex;
    QVariantList m_transactions;
    QVariantList m_changes;

    bool m_capturing = true;
    bool m_inTransaction = false;
    QString m_currentTransactionDescription;
    int m_undoStackSize = 0;
    int m_redoStackSize = 0;

    int m_maxTransactions = 300;
    int m_maxChanges = 4000;

    quint64 m_sequence = 0;
    QHash<QString, QString> m_lastPropertySnapshots;

    DocumentManager::ListenerID m_docListenerId = 0;
    size_t m_txBegunListenerId = 0;
    size_t m_txCommittedListenerId = 0;
    size_t m_undoStackChangedListenerId = 0;
    size_t m_redoStackChangedListenerId = 0;
};
