#include "FiberMaskBridge.hpp"
#include "BridgeRegistration.hpp"
#include <ActionManager.hpp>
#include <SelectionBridge.hpp>
#include <FiberMaskDB.hpp>
#include <ModelInstanceDB.hpp>
#include <QTimer>
#include <TransactionManager.hpp>
#include <algorithm>

FiberMaskBridge::FiberMaskBridge(QObject* parent) : BridgeBase(parent) {
    if (auto* document = DocumentManager::instance()) {
        m_documentListener = document->addChangeListener([this](const auto& change) {
            if ((change.dbType != TypeID::MODEL_INSTANCE_DB && change.dbType != TypeID::FIBER_MASK_DB)
                || m_groupsRefreshPending)
                return;
            if (change.changeType == ChangeType::PROPERTY_CHANGED &&
                change.propertyName != "DisplayName" && change.propertyName != "Visible" &&
                change.propertyName != "Order" &&
                change.propertyName != FiberMaskDB::kOwnerRelation &&
                change.propertyName != ModelInstanceDB::kObjectRelation &&
                change.propertyName != "PartSet")
                return;
            m_groupsRefreshPending = true;
            QTimer::singleShot(0, this, [this] {
                m_groupsRefreshPending = false;
                refreshModelGroups();
            });
        });
        refreshModelGroups();
    }
}
FiberMaskBridge::~FiberMaskBridge() {
    if (m_documentListener)
        if (auto* document = DocumentManager::instance())
            document->removeChangeListener(m_documentListener);
}
FiberMaskBridge* FiberMaskBridge::instance() {
    static auto* bridge = new FiberMaskBridge;
    return bridge;
}
FiberMaskBridge* FiberMaskBridge::create(QQmlEngine*, QJSEngine*) {
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}
bool FiberMaskBridge::enterSelectedModel() {
    if (active())
        return true;
    const auto id = SelectionBridge::instance()->selectedId();
    return id.isValid() &&
           ActionManager::getInstance()->triggerAction(
               QStringLiteral("model.fiberMask"),
               {{QStringLiteral("modelId"), QVariant::fromValue<qulonglong>(id.getValue())}});
}
bool FiberMaskBridge::selectMask(qulonglong value) {
    auto* document = DocumentManager::instance();
    const DBInstanceID id(value);
    const auto mask = document ? document->getDB<FiberMaskDB>(id) : nullptr;
    const auto ownerId = mask ? document->getOwner(id) : INVALID_DB_ID;
    if (!ownerId.isValid() || !document->getDB<ModelInstanceDB>(ownerId))
        return false;
    if (active() && m_state.value("modelId").toULongLong() == ownerId.getValue()) {
        request(QStringLiteral("select"), {{"id", value}});
        return active() && m_state.value("selectedMaskId").toULongLong() == value;
    }
    if (active() && !finish())
        return false;
    return ActionManager::getInstance()->triggerAction(
        QStringLiteral("model.fiberMask"),
        {{"modelId", QVariant::fromValue<qulonglong>(ownerId.getValue())}, {"maskId", value}});
}
bool FiberMaskBridge::finish() {
    if (active())
        request(QStringLiteral("finish"));
    return !active();
}
void FiberMaskBridge::setMaskVisible(qulonglong value, bool visible) {
    auto* document = DocumentManager::instance();
    const auto mask = document ? document->getDB<FiberMaskDB>(DBInstanceID(value)) : nullptr;
    if (!mask || mask->getVisible() == visible)
        return;
    if (TransactionManager::instance().isInTransaction()) {
        m_state.insert("error", tr("Cannot change mask visibility while another document operation is active."));
        emit changed();
        // Refresh even when the data is unchanged to undo an optimistic switch toggle.
        refreshModelGroups();
        emit modelGroupsChanged();
        return;
    }
    {
        TransactionGuard transaction("Show or hide fiber mask");
        mask->setVisible(visible);
    }
    refreshModelGroups();
    if (active())
        request(QStringLiteral("refresh"));
}
void FiberMaskBridge::refreshModelGroups() {
    QVariantList groups;
    auto* document = DocumentManager::instance();
    if (document) {
        auto ids = document->getAllDBInstanceIds(TypeID::MODEL_INSTANCE_DB);
        std::sort(ids.begin(), ids.end(), [](auto a, auto b) { return a.getValue() < b.getValue(); });
        for (auto id : ids) {
            const auto model = document->getDB<ModelInstanceDB>(id);
            if (!model || model->isTempDB() || !model->isValid())
                continue;
            QVariantList masks;
            std::vector<std::shared_ptr<FiberMaskDB>> orderedMasks;
            for (auto maskId : document->getOwnedChildren(id, FiberMaskDB::kOwnerRelation)) {
                const auto mask = document->getDB<FiberMaskDB>(maskId);
                if (mask) orderedMasks.push_back(mask);
            }
            std::sort(orderedMasks.begin(), orderedMasks.end(), [](const auto& first, const auto& second) {
                return FiberMaskDB::precedes(*first, *second);
            });
            for (const auto& mask : orderedMasks) {
                masks.append(QVariantMap{{"id", QVariant::fromValue<qulonglong>(mask->getDBInstanceID().getValue())},
                                         {"name", QString::fromStdString(mask->getDisplayName())},
                                         {"visible", mask->getVisible()}});
            }
            groups.append(QVariantMap{{"id", QVariant::fromValue<qulonglong>(id.getValue())},
                                      {"name", QString::fromStdString(model->getDisplayName())},
                                      {"count", masks.size()}, {"masks", masks}});
        }
    }
    if (m_modelGroups != groups) {
        m_modelGroups = std::move(groups);
        emit modelGroupsChanged();
    }
}
void FiberMaskBridge::request(const QString& operation, const QVariantMap& values) {
    if (operation == "confirm" || operation == "finish" || operation == "new" ||
        operation == "select" || operation == "copy" || operation == "next")
        flushInputs();
    emit requested(operation, values);
}
void FiberMaskBridge::project(QVariantMap state, QVariantList parameters) {
    const bool wasActive = active();
    const bool stateChanged = m_state != state, parameterChanged = m_parameters != parameters;
    m_state = std::move(state);
    m_parameters = std::move(parameters);
    if (parameterChanged)
        emit parametersChanged();
    if (stateChanged)
        emit changed();
    if (active() && !wasActive)
        refreshModelGroups();
}
REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(FiberMaskBridge, "FiberMaskBridge", &FiberMaskBridge::create)
