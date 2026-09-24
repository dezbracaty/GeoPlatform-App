#include "TransactionDebugBridge.hpp"
#include "BridgeRegistration.hpp"

#include "AutoRegisterDB.hpp"
#include "ChangeTypes.hpp"
#include "Foundation/Log.h"
#include "SystemTypes.hpp"
#include "TransactionManager.hpp"
#include "Transform.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QSet>
#include <QThread>

#include <atomic>
#include <typeinfo>

namespace {

std::atomic_bool s_runtimeEnabled{false};
std::atomic_bool s_runtimeConfigured{false};

QString nowTimestamp() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

QString transactionTypeToString(TransactionManager::ChangeType type) {
    switch (type) {
        case TransactionManager::ChangeType::PROPERTY_CHANGE:
            return "PROPERTY_CHANGE";
        case TransactionManager::ChangeType::OBJECT_CREATED:
            return "OBJECT_CREATED";
        case TransactionManager::ChangeType::OBJECT_DELETED:
            return "OBJECT_DELETED";
        default:
            return "UNKNOWN";
    }
}

QString dbTypeToString(uint32_t value) {
    TypeID type = static_cast<TypeID>(value);
    return typeIdToString(type);
}

QString safeTypeName(const std::type_info& info) {
    return QString::fromLatin1(info.name());
}

QString vector3ToString(const Vector3& vec) {
    return QString("(%1, %2, %3)")
        .arg(vec.x, 0, 'f', 3)
        .arg(vec.y, 0, 'f', 3)
        .arg(vec.z, 0, 'f', 3);
}

QString anyToString(const std::any& value) {
    if (!value.has_value()) {
        return "<empty>";
    }

    try {
        if (value.type() == typeid(int)) {
            return QString::number(std::any_cast<int>(value));
        }
        if (value.type() == typeid(float)) {
            return QString::number(std::any_cast<float>(value), 'f', 4);
        }
        if (value.type() == typeid(double)) {
            return QString::number(std::any_cast<double>(value), 'f', 6);
        }
        if (value.type() == typeid(bool)) {
            return std::any_cast<bool>(value) ? "true" : "false";
        }
        if (value.type() == typeid(long)) {
            return QString::number(std::any_cast<long>(value));
        }
        if (value.type() == typeid(long long)) {
            return QString::number(std::any_cast<long long>(value));
        }
        if (value.type() == typeid(unsigned int)) {
            return QString::number(std::any_cast<unsigned int>(value));
        }
        if (value.type() == typeid(uint64_t)) {
            return QString::number(static_cast<qulonglong>(std::any_cast<uint64_t>(value)));
        }
        if (value.type() == typeid(std::string)) {
            return QString::fromStdString(std::any_cast<std::string>(value));
        }
        if (value.type() == typeid(QString)) {
            return std::any_cast<QString>(value);
        }
        if (value.type() == typeid(Vector2)) {
            const auto vec = std::any_cast<Vector2>(value);
            return QString("(%1, %2)").arg(vec.x, 0, 'f', 3).arg(vec.y, 0, 'f', 3);
        }
        if (value.type() == typeid(Vector3)) {
            const auto vec = std::any_cast<Vector3>(value);
            return vector3ToString(vec);
        }
        if (value.type() == typeid(Vector4)) {
            const auto vec = std::any_cast<Vector4>(value);
            return QString("(%1, %2, %3, %4)")
                .arg(vec.x, 0, 'f', 3)
                .arg(vec.y, 0, 'f', 3)
                .arg(vec.z, 0, 'f', 3)
                .arg(vec.w, 0, 'f', 3);
        }
        if (value.type() == typeid(Color)) {
            const auto color = std::any_cast<Color>(value);
            return QString("rgba(%1, %2, %3, %4)")
                .arg(color.r, 0, 'f', 3)
                .arg(color.g, 0, 'f', 3)
                .arg(color.b, 0, 'f', 3)
                .arg(color.a, 0, 'f', 3);
        }
        if (value.type() == typeid(DBInstanceID)) {
            const auto id = std::any_cast<DBInstanceID>(value);
            return QString::number(id.getValue());
        }
        if (value.type() == typeid(TypeID)) {
            const auto type = std::any_cast<TypeID>(value);
            return typeIdToString(type);
        }
        if (value.type() == typeid(trans::DBInstanceID)) {
            const auto id = std::any_cast<trans::DBInstanceID>(value);
            return QString::number(static_cast<qlonglong>(id.getValue()));
        }
        if (value.type() == typeid(trans::TypeID)) {
            const auto type = std::any_cast<trans::TypeID>(value);
            return dbTypeToString(type.getValue());
        }
        if (value.type() == typeid(Transform)) {
            const auto transform = std::any_cast<Transform>(value);
            return QString("pos%1 rot%2 scale%3")
                .arg(vector3ToString(transform.getPosition()))
                .arg(vector3ToString(transform.getEuler()))
                .arg(vector3ToString(transform.getScale()));
        }
    } catch (const std::bad_any_cast&) {
        return QString("<bad_any_cast:%1>").arg(safeTypeName(value.type()));
    }

    return QString("<%1>").arg(safeTypeName(value.type()));
}

QString resolveDbId(const std::shared_ptr<trans::TransDB>& object) {
    if (!object) {
        return "N/A";
    }
    auto autoDb = std::dynamic_pointer_cast<AutoRegisterDB>(object);
    if (!autoDb) {
        return "N/A";
    }
    return QString::number(autoDb->getDBInstanceID().getValue());
}

QString resolveDbType(const std::shared_ptr<trans::TransDB>& object) {
    if (!object) {
        return "UNKNOWN";
    }
    auto autoDb = std::dynamic_pointer_cast<AutoRegisterDB>(object);
    if (autoDb) {
        return typeIdToString(autoDb->getTypeID());
    }
    return safeTypeName(typeid(*object));
}

} // namespace

TransactionDebugBridge::TransactionDebugBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    m_capturing = runtimeEnabled();
    if (!runtimeEnabled()) {
        return;
    }

    refreshStackState();
    syncTransactionState();
    setupTransactionListeners();
    setupDocumentListener();

    if (m_capturing) {
        QVariantMap readyEvent{
            {"id", QString("ch-%1").arg(++m_sequence)},
            {"timestamp", nowTimestamp()},
            {"source", "System"},
            {"dbId", "-"},
            {"dbType", "-"},
            {"changeType", "DEBUG_CAPTURE_READY"},
            {"property", "Transaction debug capture started"},
            {"oldValue", ""},
            {"newValue", ""},
            {"transactionActive", m_inTransaction},
            {"transactionDescription", m_currentTransactionDescription}
        };
        appendChange(readyEvent);
    }
}

void TransactionDebugBridge::setRuntimeEnabled(bool enabled) {
    s_runtimeEnabled.store(enabled, std::memory_order_relaxed);
    s_runtimeConfigured.store(true, std::memory_order_relaxed);
}

bool TransactionDebugBridge::runtimeEnabled() {
    if (!s_runtimeConfigured.load(std::memory_order_relaxed)) {
        const QStringList args = QCoreApplication::arguments();
        const bool enabled = args.contains("--debug-transaction");
        s_runtimeEnabled.store(enabled, std::memory_order_relaxed);
        s_runtimeConfigured.store(true, std::memory_order_relaxed);
    }
    return s_runtimeEnabled.load(std::memory_order_relaxed);
}

TransactionDebugBridge::~TransactionDebugBridge() {
    removeListeners();
}

TransactionDebugBridge* TransactionDebugBridge::instance() {
    static TransactionDebugBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new TransactionDebugBridge();
    }
    return s_instance;
}

TransactionDebugBridge* TransactionDebugBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    auto* bridge = instance();
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    return bridge;
}

QVariantList TransactionDebugBridge::transactions() const {
    QMutexLocker locker(&m_mutex);
    return m_transactions;
}

QVariantList TransactionDebugBridge::changes() const {
    QMutexLocker locker(&m_mutex);
    return m_changes;
}

void TransactionDebugBridge::setCapturing(bool enabled) {
    if (!runtimeEnabled()) {
        if (m_capturing) {
            m_capturing = false;
            emit capturingChanged();
        }
        return;
    }
    if (m_capturing == enabled) {
        return;
    }
    m_capturing = enabled;
    emit capturingChanged();
}

int TransactionDebugBridge::transactionCount() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_transactions.size());
}

int TransactionDebugBridge::changeCount() const {
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_changes.size());
}

void TransactionDebugBridge::setMaxTransactions(int value) {
    value = qBound(50, value, 5000);
    if (m_maxTransactions == value) {
        return;
    }

    m_maxTransactions = value;
    {
        QMutexLocker locker(&m_mutex);
        trimList(m_transactions, m_maxTransactions);
    }
    emit maxTransactionsChanged();
    emit transactionsChanged();
}

void TransactionDebugBridge::setMaxChanges(int value) {
    value = qBound(200, value, 50000);
    if (m_maxChanges == value) {
        return;
    }

    m_maxChanges = value;
    {
        QMutexLocker locker(&m_mutex);
        trimList(m_changes, m_maxChanges);
    }
    emit maxChangesChanged();
    emit changesChanged();
}

void TransactionDebugBridge::clearAll() {
    clearTransactions();
    clearChanges();
}

void TransactionDebugBridge::clearTransactions() {
    bool hadData = false;
    {
        QMutexLocker locker(&m_mutex);
        hadData = !m_transactions.isEmpty();
        m_transactions.clear();
    }
    if (hadData) {
        emit transactionsChanged();
    }
}

void TransactionDebugBridge::clearChanges() {
    bool hadData = false;
    {
        QMutexLocker locker(&m_mutex);
        hadData = !m_changes.isEmpty();
        m_changes.clear();
        m_lastPropertySnapshots.clear();
    }
    if (hadData) {
        emit changesChanged();
    }
}

QVariantMap TransactionDebugBridge::getTransactionById(const QString& txId) const {
    QMutexLocker locker(&m_mutex);
    for (const auto& item : m_transactions) {
        const QVariantMap map = item.toMap();
        if (map.value("id").toString() == txId) {
            return map;
        }
    }
    return {};
}

QVariantList TransactionDebugBridge::getTransactionChangesById(const QString& txId) const {
    QVariantMap tx = getTransactionById(txId);
    return tx.value("changes").toList();
}

QString TransactionDebugBridge::snapshotAsJson() const {
    QVariantMap snapshot;
    {
        QMutexLocker locker(&m_mutex);
        snapshot.insert("generatedAt", nowTimestamp());
        snapshot.insert("capturing", m_capturing);
        snapshot.insert("inTransaction", m_inTransaction);
        snapshot.insert("currentTransactionDescription", m_currentTransactionDescription);
        snapshot.insert("undoStackSize", m_undoStackSize);
        snapshot.insert("redoStackSize", m_redoStackSize);
        snapshot.insert("transactions", m_transactions);
        snapshot.insert("changes", m_changes);
    }

    QJsonDocument doc = QJsonDocument::fromVariant(snapshot);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

void TransactionDebugBridge::setupTransactionListeners() {
    if (!runtimeEnabled()) {
        return;
    }
    auto& tm = TransactionManager::instance();

    m_txBegunListenerId = tm.addTransactionBegunListener([this](const std::string& description) {
        QString desc = QString::fromStdString(description);
        QMetaObject::invokeMethod(this, [this, desc]() {
            m_inTransaction = true;
            m_currentTransactionDescription = desc;
            emit inTransactionChanged();
            emit currentTransactionDescriptionChanged();

            if (m_capturing) {
                QVariantMap metaEvent{
                    {"id", QString("ch-%1").arg(++m_sequence)},
                    {"timestamp", nowTimestamp()},
                    {"source", "TransactionManager"},
                    {"dbId", "-"},
                    {"dbType", "-"},
                    {"changeType", "TRANSACTION_BEGUN"},
                    {"property", desc},
                    {"oldValue", ""},
                    {"newValue", ""},
                    {"transactionActive", true},
                    {"transactionDescription", desc}
                };
                appendChange(metaEvent);
            }
        }, Qt::QueuedConnection);
    });

    m_txCommittedListenerId =
        tm.addTransactionCommittedListener([this](const TransactionManager::Transaction& transaction) {
            TransactionManager::Transaction txCopy = transaction;
            QMetaObject::invokeMethod(this, [this, txCopy]() {
                if (!m_capturing) {
                    // Capture is disabled: keep runtime state in sync only.
                    syncTransactionState();
                    refreshStackState();
                    return;
                }

                QVariantList txChanges;
                int propertyChangeCount = 0;
                QSet<QString> dbTypesSet;

                for (size_t i = 0; i < txCopy.changes.size(); ++i) {
                    QVariantMap changeMap;
                    changeMap.insert("index", static_cast<int>(i));

                    std::visit(
                        [&](auto&& item) {
                            using T = std::decay_t<decltype(item)>;
                            if constexpr (std::is_same_v<T, TransactionManager::PropertyChange>) {
                                propertyChangeCount++;
                                auto object = item.object.lock();
                                changeMap.insert("kind", "PROPERTY_CHANGE");
                                changeMap.insert("dbId", resolveDbId(object));
                                const QString resolvedDbType = resolveDbType(object);
                                changeMap.insert("dbType", resolvedDbType);
                                if (!resolvedDbType.isEmpty() && resolvedDbType != "UNKNOWN" && resolvedDbType != "N/A") {
                                    dbTypesSet.insert(resolvedDbType);
                                }
                                changeMap.insert("property", QString::fromStdString(item.propertyName));
                                changeMap.insert("oldValue", anyToString(item.oldValue));
                                changeMap.insert("newValue", anyToString(item.newValue));
                                changeMap.insert("summary",
                                                 QString("%1: %2 -> %3")
                                                     .arg(QString::fromStdString(item.propertyName),
                                                          anyToString(item.oldValue),
                                                          anyToString(item.newValue)));
                            } else if constexpr (std::is_same_v<T, TransactionManager::ObjectLifecycleChange>) {
                                QString lifecycle =
                                    (txCopy.type == TransactionManager::ChangeType::OBJECT_CREATED)
                                        ? "OBJECT_CREATED"
                                        : "OBJECT_DELETED";
                                changeMap.insert("kind", lifecycle);
                                changeMap.insert("dbId", QString::number(
                                    static_cast<qlonglong>(item.instanceId.getValue())));
                                const QString resolvedDbType = dbTypeToString(item.dbType.getValue());
                                changeMap.insert("dbType", resolvedDbType);
                                if (!resolvedDbType.isEmpty() && resolvedDbType != "UNKNOWN" && resolvedDbType != "N/A") {
                                    dbTypesSet.insert(resolvedDbType);
                                }
                                changeMap.insert("property", "");
                                changeMap.insert("oldValue", "");
                                changeMap.insert("newValue", "");
                                changeMap.insert("summary",
                                                 QString("%1 #%2")
                                                     .arg(lifecycle)
                                                     .arg(static_cast<qlonglong>(item.instanceId.getValue())));
                            }
                        },
                        txCopy.changes[i]);

                    txChanges.push_back(changeMap);
                }

                QVariantList dbTypes;
                for (const auto& dbType : dbTypesSet) {
                    dbTypes.push_back(dbType);
                }
                const QString primaryDbType = dbTypes.size() == 1 ? dbTypes.first().toString() : "MULTI";

                QVariantMap txMap{
                    {"id", QString("tx-%1").arg(++m_sequence)},
                    {"timestamp", nowTimestamp()},
                    {"description", QString::fromStdString(txCopy.description)},
                    {"type", transactionTypeToString(txCopy.type)},
                    {"changeCount", static_cast<int>(txCopy.changes.size())},
                    {"propertyChangeCount", propertyChangeCount},
                    {"primaryDbType", primaryDbType},
                    {"dbTypes", dbTypes},
                    {"changes", txChanges}
                };

                appendTransaction(txMap);

                QVariantMap metaEvent{
                    {"id", QString("ch-%1").arg(++m_sequence)},
                    {"timestamp", nowTimestamp()},
                    {"source", "TransactionManager"},
                    {"dbId", "-"},
                    {"dbType", "-"},
                    {"changeType", "TRANSACTION_COMMITTED"},
                    {"property", QString::fromStdString(txCopy.description)},
                    {"oldValue", "-"},
                    {"newValue", QString("changes=%1").arg(static_cast<int>(txCopy.changes.size()))},
                    {"transactionActive", false},
                    {"transactionDescription", QString::fromStdString(txCopy.description)}
                };
                appendChange(metaEvent);

                // Push committed transaction details directly into realtime stream
                // so old/new values are always visible without relying on post-readbacks.
                for (const auto& changeItem : txChanges) {
                    const QVariantMap changeMap = changeItem.toMap();
                    const QString propertyName = changeMap.value("property").toString();
                    const QString summaryText = changeMap.value("summary").toString();
                    QVariantMap detailEvent{
                        {"id", QString("ch-%1").arg(++m_sequence)},
                        {"timestamp", nowTimestamp()},
                        {"source", "TransactionManager"},
                        {"dbId", changeMap.value("dbId").toString()},
                        {"dbType", changeMap.value("dbType").toString()},
                        {"changeType", changeMap.value("kind").toString()},
                        {"property", propertyName.isEmpty() ? summaryText : propertyName},
                        {"oldValue", changeMap.value("oldValue").toString()},
                        {"newValue", changeMap.value("newValue").toString()},
                        {"transactionActive", false},
                        {"transactionDescription", QString::fromStdString(txCopy.description)}
                    };
                    appendChange(detailEvent);
                }

                syncTransactionState();
                refreshStackState();
            }, Qt::QueuedConnection);
        });

    m_undoStackChangedListenerId = tm.addUndoStackChangedListener([this]() {
        QMetaObject::invokeMethod(this, [this]() { refreshStackState(); }, Qt::QueuedConnection);
    });

    m_redoStackChangedListenerId = tm.addRedoStackChangedListener([this]() {
        QMetaObject::invokeMethod(this, [this]() { refreshStackState(); }, Qt::QueuedConnection);
    });
}

void TransactionDebugBridge::setupDocumentListener() {
    if (!runtimeEnabled()) {
        return;
    }
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return;
    }

    m_docListenerId = docManager->addChangeListener([this](const DocumentManager::ChangeNotification& notification) {
        DocumentManager::ChangeNotification copy = notification;
        QMetaObject::invokeMethod(this, [this, copy]() {
            syncTransactionState();

            if (!m_capturing) {
                return;
            }

            const QString propertyKey = makePropertySnapshotKey(copy);
            const QString currentValue = readCurrentPropertyValue(copy);
            QString previousValue;
            if (!propertyKey.isEmpty()) {
                QMutexLocker locker(&m_mutex);
                previousValue = m_lastPropertySnapshots.value(propertyKey);
                m_lastPropertySnapshots.insert(propertyKey, currentValue);
            }

            QVariantMap item{
                {"id", QString("ch-%1").arg(++m_sequence)},
                {"timestamp", nowTimestamp()},
                {"source", "DocumentManager"},
                {"dbId", QString::number(copy.id.getValue())},
                {"dbType", typeIdToString(copy.dbType)},
                {"changeType", changeTypeToString(copy.changeType)},
                {"property", QString::fromStdString(copy.propertyName)},
                {"oldValue", previousValue},
                {"newValue", currentValue},
                {"transactionActive", m_inTransaction},
                {"transactionDescription", m_currentTransactionDescription}
            };
            appendChange(item);
        }, Qt::QueuedConnection);
    });
}

void TransactionDebugBridge::removeListeners() {
    auto* docManager = DocumentManager::instance();
    if (docManager && m_docListenerId != 0) {
        docManager->removeChangeListener(m_docListenerId);
        m_docListenerId = 0;
    }

    auto& tm = TransactionManager::instance();
    if (m_txBegunListenerId != 0) {
        tm.removeTransactionBegunListener(m_txBegunListenerId);
        m_txBegunListenerId = 0;
    }
    if (m_txCommittedListenerId != 0) {
        tm.removeTransactionCommittedListener(m_txCommittedListenerId);
        m_txCommittedListenerId = 0;
    }
    if (m_undoStackChangedListenerId != 0) {
        tm.removeUndoStackChangedListener(m_undoStackChangedListenerId);
        m_undoStackChangedListenerId = 0;
    }
    if (m_redoStackChangedListenerId != 0) {
        tm.removeRedoStackChangedListener(m_redoStackChangedListenerId);
        m_redoStackChangedListenerId = 0;
    }
}

void TransactionDebugBridge::refreshStackState() {
    auto& tm = TransactionManager::instance();
    const int undoSize = static_cast<int>(tm.getUndoStackSize());
    const int redoSize = static_cast<int>(tm.getRedoStackSize());

    if (m_undoStackSize != undoSize) {
        m_undoStackSize = undoSize;
        emit undoStackSizeChanged();
    }
    if (m_redoStackSize != redoSize) {
        m_redoStackSize = redoSize;
        emit redoStackSizeChanged();
    }

    syncTransactionState();
}

void TransactionDebugBridge::syncTransactionState() {
    auto& tm = TransactionManager::instance();
    const bool inTx = tm.isInTransaction();
    if (m_inTransaction != inTx) {
        m_inTransaction = inTx;
        emit inTransactionChanged();
    }

    if (!m_inTransaction && !m_currentTransactionDescription.isEmpty()) {
        m_currentTransactionDescription.clear();
        emit currentTransactionDescriptionChanged();
    }
}

QString TransactionDebugBridge::readCurrentPropertyValue(
    const DocumentManager::ChangeNotification& notification) const {
    if (notification.propertyName.empty()) {
        return "";
    }

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        return "";
    }

    auto object = docManager->getDBInstance(notification.id);
    if (!object) {
        return "";
    }

    try {
        const std::string propertyName = notification.propertyName;
        const std::any value = object->getPropertyByName(propertyName);
        if (value.has_value()) {
            return anyToString(value);
        }

        // Fallback for Actor-like position edits:
        // many setPosition calls mutate Transform and notify pseudo fields.
        const std::any transformAny = object->getPropertyByName("Transform");
        if (transformAny.type() == typeid(Transform)) {
            const auto transform = std::any_cast<Transform>(transformAny);
            if (propertyName == "Position") {
                return vector3ToString(transform.getPosition());
            }
            if (propertyName == "Scale") {
                return vector3ToString(transform.getScale());
            }
            if (propertyName == "Rotation") {
                return vector3ToString(transform.getEuler());
            }
            if (propertyName == "Transform") {
                return anyToString(transformAny);
            }
        }

        return "";
    } catch (...) {
        return "";
    }
}

QString TransactionDebugBridge::makePropertySnapshotKey(
    const DocumentManager::ChangeNotification& notification) const {
    if (notification.propertyName.empty()) {
        return "";
    }
    const QString normalizedProperty = QString::fromStdString(notification.propertyName).toLower();
    return QString("%1|%2")
        .arg(static_cast<qlonglong>(notification.id.getValue()))
        .arg(normalizedProperty);
}

void TransactionDebugBridge::appendTransaction(const QVariantMap& item) {
    {
        QMutexLocker locker(&m_mutex);
        m_transactions.push_back(item);
        trimList(m_transactions, m_maxTransactions);
    }
    emit transactionsChanged();
}

void TransactionDebugBridge::appendChange(const QVariantMap& item) {
    {
        QMutexLocker locker(&m_mutex);
        m_changes.push_back(item);
        trimList(m_changes, m_maxChanges);
    }
    emit changesChanged();
}

void TransactionDebugBridge::trimList(QVariantList& list, int maxSize) {
    while (list.size() > maxSize) {
        list.removeFirst();
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    TransactionDebugBridge,
    "TransactionDebugBridge",
    &TransactionDebugBridge::create)
