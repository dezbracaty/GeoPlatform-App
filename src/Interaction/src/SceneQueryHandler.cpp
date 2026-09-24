#include "SceneQueryHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(SceneQueryHandler, "query.objects")
#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include <DocumentManager.hpp>
#include "AIDescriptorHelper.hpp"
#include <ActorDB.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelObjectDB.hpp>
#include <algorithm>

namespace {
QString normalize(const QString& value) {
    return value.trimmed().toLower();
}

bool containsKeyword(const QString& textLower, const QStringList& keywords) {
    for (const auto& keyword : keywords) {
        if (!keyword.isEmpty() && textLower.contains(keyword)) {
            return true;
        }
    }
    return false;
}

QString inferObjectType(TypeID typeId,
                        const QString& displayName,
                        const QString& dataSource,
                        const QString& fileFormat) {
    const QString displayLower = displayName.toLower();
    const QString sourceLower = dataSource.toLower();

    switch (typeId) {
        case TypeID::CYLINDER_DB:
            return "cylinder";
        case TypeID::CONE_DB:
            return "cone";
        case TypeID::SPHERE_DB:
            return "sphere";
        case TypeID::CUBE_DB:
            return "cube";
        case TypeID::MODEL_INSTANCE_DB: {
            if (fileFormat.compare("glb", Qt::CaseInsensitive) == 0)
                return "glb";
            if (containsKeyword(displayLower, {"cylinder", "圆柱", "圆柱体"}) ||
                containsKeyword(sourceLower, {"cylinder", "圆柱", "圆柱体"})) {
                return "cylinder";
            }
            if (containsKeyword(displayLower, {"sphere", "球", "球体"}) ||
                containsKeyword(sourceLower, {"sphere", "球", "球体"})) {
                return "sphere";
            }
            if (containsKeyword(displayLower, {"cone", "圆锥", "三角锥", "锥体"}) ||
                containsKeyword(sourceLower, {"cone", "圆锥", "三角锥", "锥体"})) {
                return "cone";
            }
            if (containsKeyword(displayLower, {"cube", "方块", "立方", "立方体", "正方体"}) ||
                containsKeyword(sourceLower, {"cube", "方块", "立方", "立方体", "正方体"})) {
                return "cube";
            }
            return "mesh";
        }
        default:
            return "unknown";
    }
}

QStringList canonicalTokens(const QString& rawQuery) {
    const QString q = normalize(rawQuery);
    QStringList tokens;
    if (q.isEmpty()) {
        return tokens;
    }
    tokens.push_back(q);
    if (q.contains("cylinder") || q.contains(QString::fromUtf8("圆柱")) || q.contains(QString::fromUtf8("圆柱体"))) {
        tokens.push_back("cylinder");
    }
    if (q.contains("sphere") || q.contains(QString::fromUtf8("球")) || q.contains(QString::fromUtf8("球体"))) {
        tokens.push_back("sphere");
    }
    if (q.contains("cone") || q.contains(QString::fromUtf8("圆锥")) || q.contains(QString::fromUtf8("三角锥")) || q.contains(QString::fromUtf8("锥体"))) {
        tokens.push_back("cone");
    }
    if (q.contains("cube") || q.contains(QString::fromUtf8("方块")) || q.contains(QString::fromUtf8("立方")) || q.contains(QString::fromUtf8("立方体")) || q.contains(QString::fromUtf8("正方体"))) {
        tokens.push_back("cube");
    }
    if (q.contains("mesh") || q.contains(QString::fromUtf8("网格")) || q.contains(QString::fromUtf8("模型"))) {
        tokens.push_back("mesh");
    }
    tokens.removeDuplicates();
    return tokens;
}

bool matchesQuery(const QStringList& tokens, const QString& searchable) {
    if (tokens.isEmpty()) {
        return true;
    }
    const QString text = searchable.toLower();
    for (const QString& token : tokens) {
        if (!token.isEmpty() && text.contains(token)) {
            return true;
        }
    }
    return false;
}
} // namespace

SceneQueryHandler::SceneQueryHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void SceneQueryHandler::onEnter(std::shared_ptr<ActionContext> context) {
    onEnterForAI(context);
}

void SceneQueryHandler::onEnterForAI(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);
    const bool ok = executeQuery(context);
    if (!ok && context && !context->hasError()) {
        context->setError(ActionErrorCode::Internal, "query.objects failed");
    }
    StandardActionHandler::onExit();
}

const QHash<QString, QVariantMap>& SceneQueryHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"query.objects", makeDescriptor(
            "query.objects",
            "Query Scene Objects",
            "Query scene objects by keyword/type and return object ids with position.",
            "read",
            {
                {"query", "string", false},
                {"type", "string", false, QVariant(), {"any", "cylinder", "sphere", "cone", "cube", "mesh", "glb"}},
                {"limit", "number", false}
            },
            {"query", "scene", "object"}
        )}
    };
    return table;
}

bool SceneQueryHandler::executeQuery(std::shared_ptr<ActionContext> context) const {
    if (!context) {
        return false;
    }

    DocumentManager* docManager = DocumentManager::instance();
    if (!docManager) {
        context->setError(ActionErrorCode::SystemUnavailable, "DocumentManager unavailable");
        return false;
    }

    const QVariantMap params = context->getParams();
    const QString query = params.value("query").toString();
    const QString typeFilter = normalize(params.value("type", "any").toString());
    const int limit = std::clamp(params.value("limit", 50).toInt(), 1, 200);
    const QStringList tokens = canonicalTokens(query);

    const std::vector<TypeID> candidateTypes = {
        TypeID::CUBE_DB,
        TypeID::SPHERE_DB,
        TypeID::CONE_DB,
        TypeID::CYLINDER_DB,
        TypeID::MODEL_INSTANCE_DB
    };

    QVariantList objects;
    for (const TypeID typeId : candidateTypes) {
        const auto instances = docManager->getDBInstancesByType(typeId);
        for (const auto& instance : instances) {
            auto actor = std::dynamic_pointer_cast<ActorDB>(instance);
            if (!actor) {
                continue;
            }

            auto model = std::dynamic_pointer_cast<ModelInstanceDB>(instance);
            auto object = model ? model->object() : nullptr;
            const QString displayName = QString::fromStdString(actor->getDisplayName());
            const QString dataSource = object
                ? QString::fromStdString(object->getSourceFile()) : QString();
            const QString fileFormat = object
                ? QString::fromStdString(object->getFileFormat()) : QString();
            const QString objectType = inferObjectType(
                typeId, displayName, dataSource, fileFormat);
            if (!typeFilter.isEmpty() && typeFilter != "any" && objectType != typeFilter) {
                continue;
            }

            const Vector3 pos = actor->getPosition();
            const QString searchable = QString("%1 %2 %3 %4")
                                           .arg(objectType,
                                                typeIdToString(typeId),
                                                displayName,
                                                dataSource);
            if (!matchesQuery(tokens, searchable)) {
                continue;
            }

            QVariantMap item;
            item.insert("dbId", actor->getDBInstanceID().getValue());
            item.insert("modelId", actor->getDBInstanceID().getValue());
            item.insert("type", objectType);
            item.insert("typeId", typeIdToString(typeId));
            item.insert("displayName", displayName);
            if (!dataSource.isEmpty()) {
                item.insert("dataSource", dataSource);
            }
            item.insert("position", QVariantMap{
                                        {"x", static_cast<double>(pos.x)},
                                        {"y", static_cast<double>(pos.y)},
                                        {"z", static_cast<double>(pos.z)}
                                    });
            objects.push_back(item);
            if (objects.size() >= limit) {
                break;
            }
        }
        if (objects.size() >= limit) {
            break;
        }
    }

    context->setResult(QVariantMap{
        {"query", query},
        {"typeFilter", typeFilter.isEmpty() ? QString("any") : typeFilter},
        {"matchedCount", objects.size()},
        {"exactlyOne", objects.size() == 1},
        {"objects", objects}
    });
    return true;
}
