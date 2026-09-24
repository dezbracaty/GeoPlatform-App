#include "ModelWidgetRelations.hpp"
#include "DBRelationsRegistration.hpp"
#include "DBRelationRegistry.hpp"
#include "DBRelationMacros.hpp"
#include "ActorDB.hpp"
#include "DocumentManager.hpp"
#include "Foundation/Log.h"
#include "ModelOrientationWidgetDB.hpp"
#include "ModelInstanceDB.hpp"
#include "ModelScaleWidgetDB.hpp"
#include "ModelTranslateWidgetDB.hpp"
#include "SelectionBoxWidgetDB.hpp"
#include "TransactionManager.hpp"
#include <QCoreApplication>
#include <QTimer>

namespace ModelWidgetRelations {

namespace {

constexpr const char* ActorSyncRule = "ActorSync";

template <typename WidgetDB>
void syncWidgetFromActor(DBRelationUpdateContext& context) {
    auto actor = context.sourceAs<ActorDB>();
    auto widget = context.targetAs<WidgetDB>();
    if (!actor || !widget || widget->getLinkedActorID() != context.sourceId) {
        return;
    }

    widget->updateFromLinkedActor();
}

template <typename WidgetDB>
void attachWidgetToActor(DBRelationAttachContext& context) {
    auto actor = context.sourceAs<ActorDB>();
    auto widget = context.targetAs<WidgetDB>();
    if (!actor || !widget) {
        return;
    }

    if (widget->getLinkedActorID() != context.sourceId) {
        widget->setLinkedActorID(context.sourceId);
    }
    widget->updateFromLinkedActor();
}

template <typename WidgetDB>
void detachWidgetFromActor(DBRelationDetachContext& context) {
    auto widget = context.targetAs<WidgetDB>();
    if (!widget || widget->getLinkedActorID() != context.sourceId) {
        return;
    }

    widget->setLinkedActorID(DBInstanceID());
}

void detachSelectionBoxFromActor(DBRelationDetachContext& context) {
    auto widget = context.targetAs<SelectionBoxWidgetDB>();
    if (!widget || widget->getLinkedActorID() != context.sourceId) {
        return;
    }

    if (context.reason == DBRelationDetachReason::SourceDeleted) {
        const DBInstanceID widgetId = context.targetId;
        auto* application = QCoreApplication::instance();
        if (!application) {
            LOG_WARN("Cannot defer selection box deletion for widget {}", widgetId.getValue());
            return;
        }

        QTimer::singleShot(0, application, [widgetId]() {
            auto* document = DocumentManager::instance();
            if (!document || !document->getDBInstance(widgetId)) {
                return;
            }
            TransactionGuard guard("Delete SelectionBoxWidget");
            document->unregisterDBInstance(widgetId);
        });
        return;
    }

    widget->setLinkedActorID(DBInstanceID());
    widget->setHasValidBounds(false);
}

} // namespace

void registerRelations() {
    auto& registry = DBRelationRegistry::instance();
    registry.registerDependency(
        TypeID::MODEL_INSTANCE_DB,
        TypeID::MODEL_TRANSLATE_WIDGET_DB,
        TranslateWidgetRelation,
        ActorSyncRule,
        WATCH_PROPS("Transform"),
        syncWidgetFromActor<ModelTranslateWidgetDB>,
        attachWidgetToActor<ModelTranslateWidgetDB>,
        detachWidgetFromActor<ModelTranslateWidgetDB>);
    registry.registerDependencyReference(
        TypeID::MODEL_TRANSLATE_WIDGET_DB,
        "LinkedActorID",
        TypeID::MODEL_INSTANCE_DB,
        TranslateWidgetRelation);

    registry.registerDependency(
        TypeID::MODEL_INSTANCE_DB,
        TypeID::MODEL_ORIENTATION_WIDGET_DB,
        OrientationWidgetRelation,
        ActorSyncRule,
        WATCH_PROPS("Transform"),
        syncWidgetFromActor<ModelOrientationWidgetDB>,
        attachWidgetToActor<ModelOrientationWidgetDB>,
        detachWidgetFromActor<ModelOrientationWidgetDB>);
    registry.registerDependencyReference(
        TypeID::MODEL_ORIENTATION_WIDGET_DB,
        "LinkedActorID",
        TypeID::MODEL_INSTANCE_DB,
        OrientationWidgetRelation);

    registry.registerDependency(
        TypeID::MODEL_INSTANCE_DB,
        TypeID::MODEL_SCALE_WIDGET_DB,
        ScaleWidgetRelation,
        ActorSyncRule,
        WATCH_PROPS("Transform"),
        syncWidgetFromActor<ModelScaleWidgetDB>,
        attachWidgetToActor<ModelScaleWidgetDB>,
        detachWidgetFromActor<ModelScaleWidgetDB>);
    registry.registerDependencyReference(
        TypeID::MODEL_SCALE_WIDGET_DB,
        "LinkedActorID",
        TypeID::MODEL_INSTANCE_DB,
        ScaleWidgetRelation);

    registry.registerDependency(
        TypeID::MODEL_INSTANCE_DB,
        TypeID::SELECTION_BOX_WIDGET_DB,
        SelectionBoxRelation,
        ActorSyncRule,
        WATCH_PROPS("WorldBounds"),
        syncWidgetFromActor<SelectionBoxWidgetDB>,
        attachWidgetToActor<SelectionBoxWidgetDB>,
        detachSelectionBoxFromActor);
    registry.registerDependencyReference(
        TypeID::SELECTION_BOX_WIDGET_DB,
        "LinkedActorID",
        TypeID::MODEL_INSTANCE_DB,
        SelectionBoxRelation);
}

} // namespace ModelWidgetRelations

REGISTER_DB_RELATIONS(ModelWidgetRelations::registerRelations())
