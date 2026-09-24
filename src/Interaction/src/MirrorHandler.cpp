#include "MirrorHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    MirrorHandler,
    "model.mirror",
    "model.mirror.x",
    "model.mirror.y",
    "model.mirror.z",
    "model.remove_mirror_widget")
#include "ActionHandlerRegistry.hpp"
#include "Transaction.hpp"
#include "Foundation/Log.h"
#include "transdb.h"
#include "AIDescriptorHelper.hpp"
#include <QVariantList>
#include <algorithm>

namespace {
std::vector<DBInstanceID> parseModelIdsForAI(const QVariantMap& params) {
    std::vector<DBInstanceID> modelIds;
    const auto appendId = [&modelIds](int rawId) {
        if (rawId > 0) {
            modelIds.emplace_back(rawId);
        }
    };

    appendId(params.value("modelId", params.value("dbId", 0)).toInt());
    for (const auto& value : params.value("modelIds").toList()) {
        appendId(value.toInt());
    }
    for (const auto& value : params.value("dbIds").toList()) {
        appendId(value.toInt());
    }

    std::sort(modelIds.begin(), modelIds.end(),
              [](const DBInstanceID& a, const DBInstanceID& b) {
                  return a.getValue() < b.getValue();
              });
    modelIds.erase(std::unique(modelIds.begin(), modelIds.end(),
                               [](const DBInstanceID& a, const DBInstanceID& b) {
                                   return a.getValue() == b.getValue();
                               }),
                   modelIds.end());
    return modelIds;
}

QVariantMap makeMirrorDescriptor(const QString& actionCode,
                                 const QString& title,
                                 const QString& description,
                                 const QString& defaultAxis = QString()) {
    QVariantMap descriptor;
    descriptor.insert("actionCode", actionCode);
    descriptor.insert("title", title);
    descriptor.insert("description", description);
    descriptor.insert("sideEffectLevel", "write");
    descriptor.insert("async", false);
    descriptor.insert("supportsDryRun", false);
    descriptor.insert("idempotent", false);
    descriptor.insert("tags", QVariantList{"transform", "mirror", "scene"});
    QVariantMap schema{
        {"allowUnknownParams", false},
        {"properties", QVariantMap{
                           {"keepOriginal", QVariantMap{{"type", "bool"}}},
                           {"modelId", QVariantMap{{"type", "number"}}},
                           {"modelIds", QVariantMap{{"type", "array"}}},
                           {"dbId", QVariantMap{{"type", "number"}}},
                           {"dbIds", QVariantMap{{"type", "array"}}}
                       }}
    };
    if (defaultAxis.isEmpty()) {
        QVariantMap properties = schema.value("properties").toMap();
        properties.insert("axis", QVariantMap{
                                      {"type", "string"},
                                      {"enum", QVariantList{"x", "y", "z"}}
                                  });
        schema.insert("properties", properties);
    } else {
        descriptor.insert("defaultAxis", defaultAxis);
    }
    descriptor.insert("inputSchema", schema);
    return descriptor;
}

} // namespace

MirrorHandler::MirrorHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

MirrorHandler::~MirrorHandler() {
}

void MirrorHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    // 检查 action code
    if (context) {
        QString actionCode = context->getActionCode();

        if (actionCode == "model.mirror") {
            // 激活镜像 widget
            SelectionBridge::instance()->setMirrorWidgetStatus(true);
        } else if (actionCode == "model.remove_mirror_widget") {
            // 移除镜像 widget
            SelectionBridge::instance()->setMirrorWidgetStatus(false);
        } else {
            // 执行具体的镜像操作
            performMirrorOperation(actionCode, context->getParams());
        }
    }
}

void MirrorHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    const bool ok = executeAIMirror(context);
    if (!ok && context && !context->hasError()) {
        context->setError(ActionErrorCode::Internal, "AI mirror failed");
    }

    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& MirrorHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"model.mirror", makeDescriptor(
            "model.mirror",
            "Mirror Model",
            "Mirror selected or specified models with optional axis.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"axis", "string", false, QVariant("x"), {"x", "y", "z"}},
                {"keepOriginal", "bool", false}
            },
            {"transform", "mirror", "scene"}
        )},
        {"model.mirror.x", makeDescriptor(
            "model.mirror.x",
            "Mirror Model X",
            "Mirror selected or specified models along X axis.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"keepOriginal", "bool", false}
            },
            {"transform", "mirror", "scene"}
        )},
        {"model.mirror.y", makeDescriptor(
            "model.mirror.y",
            "Mirror Model Y",
            "Mirror selected or specified models along Y axis.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"keepOriginal", "bool", false}
            },
            {"transform", "mirror", "scene"}
        )},
        {"model.mirror.z", makeDescriptor(
            "model.mirror.z",
            "Mirror Model Z",
            "Mirror selected or specified models along Z axis.",
            "write",
            {
                {"modelId", "number", false},
                {"modelIds", "array", false},
                {"dbId", "number", false},
                {"dbIds", "array", false},
                {"keepOriginal", "bool", false}
            },
            {"transform", "mirror", "scene"}
        )}
    };
    return table;
}

void MirrorHandler::onExit() {
    // 镜像操作是一次性操作，执行完成后立即退出
    // 不需要清除 widget 状态，因为用户需要手动点击按钮来关闭
    StandardActionHandler::onExit();
}

void MirrorHandler::performMirrorOperation(const QString& actionCode, const QVariantMap& params) {
    LOG_DEBUG("MirrorHandler::performMirrorOperation - Action code: {}", actionCode.toStdString());

    // 解析镜像轴向
    MirrorOperation::MirrorAxis axis = parseAxisFromAction(actionCode);

    // 获取参数
    bool keepOriginal = params.value("keepOriginal", false).toBool();
    bool applyToAllSelected = params.value("applyToAllSelected", true).toBool();

    LOG_DEBUG("MirrorHandler::performMirrorOperation - Axis: {}, KeepOriginal: {}", 
             static_cast<int>(axis), keepOriginal);

    // 获取选中的 Actor
    auto actors = getSelectedActors();

    if (actors.empty()) {
        LOG_WARN("MirrorHandler::performMirrorOperation - No actors selected");
        return;
    }

    LOG_DEBUG("MirrorHandler::performMirrorOperation - Found {} actors to mirror", actors.size());

    // 执行镜像操作
    executeMirror(actors, axis, keepOriginal);

    // 更新 SelectionBridge 的 widget 状态
    SelectionBridge::instance()->updateMirrorWidgetStatus();
}

std::vector<std::shared_ptr<ActorDB>> MirrorHandler::getSelectedActors() {
    std::vector<std::shared_ptr<ActorDB>> actors;

    auto selectedIds = SelectionBridge::instance()->getSelectedIds();
    auto docManager = DocumentManager::instance();

    for (const auto& id : selectedIds) {
        auto dbInstance = docManager->getDBInstance(id);
        if (dbInstance) {
            auto actor = std::dynamic_pointer_cast<ActorDB>(dbInstance);
            if (actor) {
                actors.push_back(actor);
            }
        }
    }

    return actors;
}

void MirrorHandler::executeMirror(const std::vector<std::shared_ptr<ActorDB>>& actors,
                                  MirrorOperation::MirrorAxis axis,
                                  bool keepOriginal) {
    if (actors.empty()) {
        LOG_WARN("MirrorHandler::executeMirror - No actors to mirror");
        return;
    }

    try {
        TransactionGuard guard("Mirror Model");

        if (keepOriginal) {
            // 创建镜像副本
            for (const auto& actor : actors) {
                auto copy = MirrorOperation::createMirroredCopy(actor, axis);
                if (copy) {
                    LOG_DEBUG("MirrorHandler::executeMirror - Created mirrored copy");
                } else {
                    LOG_WARN("MirrorHandler::executeMirror - Failed to create mirrored copy");
                }
            }
        } else {
            // 直接镜像
            MirrorOperation::applyMirrorToActors(actors, axis);
            LOG_DEBUG("MirrorHandler::executeMirror - Applied mirror to {} actors", actors.size());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MirrorHandler::executeMirror - Exception: {}", e.what());
    }
}

MirrorOperation::MirrorAxis MirrorHandler::parseAxisFromAction(const QString& actionCode) {
    if (actionCode.endsWith(".x")) {
        return MirrorOperation::MirrorAxis::X_AXIS;
    } else if (actionCode.endsWith(".y")) {
        return MirrorOperation::MirrorAxis::Y_AXIS;
    } else if (actionCode.endsWith(".z")) {
        return MirrorOperation::MirrorAxis::Z_AXIS;
    }
    // 默认返回 Z 轴
    return MirrorOperation::MirrorAxis::Z_AXIS;
}

bool MirrorHandler::executeAIMirror(std::shared_ptr<ActionContext> context) {
    if (!context) {
        LOG_ERROR("MirrorHandler::executeAIMirror - null context");
        return false;
    }

    const QString actionCode = context->getActionCode();
    const QVariantMap params = context->getParams();

    if (actionCode == "model.remove_mirror_widget") {
        context->setError(ActionErrorCode::InvalidParams, "AI invoke not supported for action 'model.remove_mirror_widget'");
        return false;
    }

    MirrorOperation::MirrorAxis axis = MirrorOperation::MirrorAxis::Z_AXIS;
    if (actionCode == "model.mirror") {
        const QString axisText = params.value("axis", "z").toString().trimmed().toLower();
        if (axisText == "x") {
            axis = MirrorOperation::MirrorAxis::X_AXIS;
        } else if (axisText == "y") {
            axis = MirrorOperation::MirrorAxis::Y_AXIS;
        } else if (axisText == "z") {
            axis = MirrorOperation::MirrorAxis::Z_AXIS;
        } else {
            context->setError(ActionErrorCode::InvalidParams, "model.mirror axis must be one of x/y/z");
            return false;
        }
    } else if (actionCode == "model.mirror.x" || actionCode == "model.mirror.y" ||
               actionCode == "model.mirror.z") {
        axis = parseAxisFromAction(actionCode);
    } else {
        context->setError(ActionErrorCode::InvalidParams,
                          QString("AI invoke not supported for action '%1'").arg(actionCode));
        return false;
    }

    const bool keepOriginal = params.value("keepOriginal", false).toBool();
    std::vector<DBInstanceID> targetIds = parseModelIdsForAI(params);
    if (targetIds.empty()) {
        if (auto* selectionBridge = SelectionBridge::instance()) {
            targetIds = selectionBridge->getSelectedIds();
        }
    }
    if (targetIds.empty()) {
        context->setError(ActionErrorCode::TargetRequired, "No target model ids and no current selection");
        return false;
    }

    std::vector<std::shared_ptr<ActorDB>> actors;
    actors.reserve(targetIds.size());
    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        context->setError(ActionErrorCode::SystemUnavailable, "DocumentManager not available");
        return false;
    }

    for (const auto& id : targetIds) {
        auto dbInstance = docManager->getDBInstance(id);
        auto actor = std::dynamic_pointer_cast<ActorDB>(dbInstance);
        if (actor) {
            actors.push_back(actor);
        }
    }

    if (actors.empty()) {
        context->setError(ActionErrorCode::TargetNotFound, "No valid ActorDB model found for mirror operation");
        return false;
    }

    executeMirror(actors, axis, keepOriginal);
    context->setResult(QVariantMap{
        {"mirroredCount", static_cast<int>(actors.size())},
        {"keepOriginal", keepOriginal}
    });
    return true;
}
