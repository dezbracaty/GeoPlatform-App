#include "ModelFilamentAssignmentHandler.hpp"

#include "AIDescriptorHelper.hpp"
#include "InteractionRegistration.hpp"

#include <DocumentManager.hpp>
#include <ModelGraphUtil.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <SlicingConfigDB.hpp>
#include <TransactionManager.hpp>

#include <algorithm>
#include <vector>

REGISTER_INTERACTION_ACTION(
    ModelFilamentAssignmentHandler,
    "model.filament.assign",
    "model.filament.use_default")

ModelFilamentAssignmentHandler::ModelFilamentAssignmentHandler(QObject* parent)
    : StandardActionHandler(parent) {}

void ModelFilamentAssignmentHandler::onEnter(
    std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    apply(context);
    StandardActionHandler::onExit();
}

bool ModelFilamentAssignmentHandler::apply(
    const std::shared_ptr<ActionContext>& context) {
    if (!context) return false;
    bool modelIdOk = false;
    const auto rawModelId = context->getParam(QStringLiteral("modelId"))
        .toULongLong(&modelIdOk);
    const DBInstanceID modelId = modelIdOk && rawModelId > 0
        ? DBInstanceID(rawModelId)
        : DBInstanceID{};
    auto* document = DocumentManager::instance();
    auto instance = document
        ? document->getDB<ModelInstanceDB>(modelId)
        : nullptr;
    if (!instance) {
        context->setError(
            ActionErrorCode::TargetNotFound,
            QStringLiteral("Model instance does not exist"));
        return false;
    }

    const bool useDefault = context->getActionCode() ==
        QStringLiteral("model.filament.use_default");
    bool slotOk = useDefault;
    const int slot = useDefault
        ? 1
        : context->getParam(QStringLiteral("slot")).toInt(&slotOk);
    const auto config = document->getDB<GPlatform::SlicingConfigDB>(
        instance->getSlicingConfigDBId());
    const std::size_t slotCount = config
        ? config->getFilaments().size()
        : 0;
    if (!useDefault &&
        (!slotOk || slot < 1 || slotCount == 0 ||
         static_cast<std::size_t>(slot) > slotCount)) {
        context->setError(
            ActionErrorCode::InvalidParams,
            QStringLiteral("Filament slot is outside the active filament list"));
        return false;
    }

    std::vector<DBInstanceID> sourceParts;
    for (const auto& part : instance->parts()) {
        if (part) sourceParts.push_back(part->getDBInstanceID());
    }
    bool partIdOk = false;
    const auto rawPartId = context->getParam(QStringLiteral("partId"))
        .toULongLong(&partIdOk);
    if (partIdOk && rawPartId > 0) {
        const DBInstanceID requested(rawPartId);
        const auto found = std::find(
            sourceParts.begin(), sourceParts.end(), requested);
        if (found == sourceParts.end()) {
            context->setError(
                ActionErrorCode::TargetNotFound,
                QStringLiteral("Part does not belong to the model instance"));
            return false;
        }
        sourceParts = {requested};
    }
    if (sourceParts.empty()) {
        context->setError(
            ActionErrorCode::TargetNotFound,
            QStringLiteral("Model instance has no parts"));
        return false;
    }

    TransactionGuard guard(useDefault
        ? "Use Project Default Filament"
        : "Assign Model Filament");
    const auto unique = ModelGraphUtil::ensureUniqueObjectForInstance(modelId);
    if (!unique) {
        guard.rollback();
        context->setError(
            ActionErrorCode::Internal,
            QString::fromStdString(unique.error));
        return false;
    }

    QVariantList changedPartIds;
    for (const DBInstanceID sourcePartId : sourceParts) {
        const DBInstanceID targetPartId = unique.mapPart(sourcePartId);
        auto part = document->getDB<ModelPartDB>(targetPartId);
        if (!part) {
            guard.rollback();
            context->setError(
                ActionErrorCode::Internal,
                QStringLiteral("Unable to resolve unique model part"));
            return false;
        }
        if (!useDefault) part->setDefaultFilamentSlot(slot);
        part->setFilamentBindingMode(static_cast<int>(
            useDefault
                ? ModelFilamentBindingMode::InheritProjectDefault
                : ModelFilamentBindingMode::ExplicitSlot));
        changedPartIds.push_back(QVariant::fromValue<qulonglong>(
            targetPartId.getValue()));
    }

    context->setResult(QVariantMap{
        {QStringLiteral("modelId"),
         QVariant::fromValue<qulonglong>(modelId.getValue())},
        {QStringLiteral("mode"), useDefault
            ? QStringLiteral("projectDefault")
            : QStringLiteral("explicitSlot")},
        {QStringLiteral("slot"), useDefault ? QVariant{} : QVariant(slot)},
        {QStringLiteral("partIds"), changedPartIds}});
    return true;
}

const QHash<QString, QVariantMap>&
ModelFilamentAssignmentHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {QStringLiteral("model.filament.assign"), makeDescriptor(
            "model.filament.assign",
            "Assign Model Filament",
            "Explicitly bind a model or one of its parts to a one-based filament slot.",
            "write",
            {
                {"modelId", "number", true},
                {"partId", "number", false},
                {"slot", "number", true}
            },
            {"model", "filament", "assignment"})},
        {QStringLiteral("model.filament.use_default"), makeDescriptor(
            "model.filament.use_default",
            "Use Project Default Filament",
            "Remove explicit filament binding while retaining the model's authored appearance.",
            "write",
            {
                {"modelId", "number", true},
                {"partId", "number", false}
            },
            {"model", "filament", "assignment"})}
    };
    return table;
}
