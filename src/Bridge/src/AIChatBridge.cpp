#include "AIChatBridge.hpp"
#include "BridgeRegistration.hpp"
#include "AI/AgentBackendCatalog.hpp"
#include "Foundation/Log.h"
#include <ActionManager.hpp>
#include <ScopedTransactionContextMetadata.hpp>
#include <GeometryCommandBridge.hpp>
#include <ParametricCommandNames.hpp>
#include <AutoRegisterDB.hpp>
#include <DocumentManager.hpp>
#include <SystemTypes.hpp>
#include <Transform.hpp>
#include <TransactionManager.hpp>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>
#include <algorithm>
#include <any>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>

namespace {
constexpr int kDbPropertyBagMaxDepth = 4;

bool hasAnyExecutedActionStep(const QVariantMap& execution) {
    if (execution.isEmpty()) {
        return false;
    }
    if (execution.value("executedSteps").toInt() > 0) {
        return true;
    }
    const QVariantList steps = execution.value("stepResults").toList();
    for (const QVariant& value : steps) {
        const QVariantMap step = value.toMap();
        if (step.value("success").toBool()) {
            return true;
        }
    }
    return false;
}

QVariantMap transactionMetadataToVariantMap(
    const TransactionManager::TransactionMetadata& metadata) {
    QVariantMap map;
    for (const auto& [key, value] : metadata) {
        map.insert(QString::fromStdString(key), QString::fromStdString(value));
    }
    return map;
}

bool parseDoubleValue(const QVariant& value, double& out) {
    bool ok = false;
    out = value.toDouble(&ok);
    return ok;
}

bool parseUnsignedLongLong(const QVariant& value, qulonglong& out) {
    bool ok = false;
    out = value.toULongLong(&ok);
    if (ok) {
        return true;
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return false;
    }
    out = text.toULongLong(&ok);
    return ok;
}

bool parseDBInstanceIdValue(const QVariant& value, DBInstanceID::ValueType& out) {
    bool ok = false;
    out = value.toLongLong(&ok);
    if (ok) return true;
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) return false;
    out = text.toLongLong(&ok);
    return ok;
}

QVariantMap vector2ToVariantMap(const Vector2& vec) {
    return QVariantMap{{"x", vec.x}, {"y", vec.y}};
}

QVariantMap vector3ToVariantMap(const Vector3& vec) {
    return QVariantMap{{"x", vec.x}, {"y", vec.y}, {"z", vec.z}};
}

QVariantMap vector4ToVariantMap(const Vector4& vec) {
    return QVariantMap{{"x", vec.x}, {"y", vec.y}, {"z", vec.z}, {"w", vec.w}};
}

QVariantMap colorToVariantMap(const Color& color) {
    return QVariantMap{{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
}

bool parseVector2(const QVariant& value, Vector2& out, QString& error) {
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (list.size() != 2) {
            error = "Vector2 array must contain 2 elements";
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        if (!parseDoubleValue(list.at(0), x) || !parseDoubleValue(list.at(1), y)) {
            error = "Vector2 array elements must be numbers";
            return false;
        }
        out = Vector2(static_cast<float>(x), static_cast<float>(y));
        return true;
    }

    if (!value.canConvert<QVariantMap>()) {
        error = "Vector2 expects object or array";
        return false;
    }

    const QVariantMap map = value.toMap();
    double x = 0.0;
    double y = 0.0;
    if (!parseDoubleValue(map.value("x"), x) || !parseDoubleValue(map.value("y"), y)) {
        error = "Vector2 expects x/y numeric fields";
        return false;
    }
    out = Vector2(static_cast<float>(x), static_cast<float>(y));
    return true;
}

bool parseVector3(const QVariant& value, Vector3& out, QString& error) {
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (list.size() != 3) {
            error = "Vector3 array must contain 3 elements";
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        if (!parseDoubleValue(list.at(0), x) || !parseDoubleValue(list.at(1), y) ||
            !parseDoubleValue(list.at(2), z)) {
            error = "Vector3 array elements must be numbers";
            return false;
        }
        out = Vector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return true;
    }

    if (!value.canConvert<QVariantMap>()) {
        error = "Vector3 expects object or array";
        return false;
    }

    const QVariantMap map = value.toMap();
    const bool hasXYZ = map.contains("x") || map.contains("y") || map.contains("z");
    const bool hasRGB = map.contains("r") || map.contains("g") || map.contains("b");
    if (!hasXYZ && !hasRGB) {
        error = "Vector3 expects x/y/z or r/g/b fields";
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (hasXYZ) {
        if (!parseDoubleValue(map.value("x"), x) || !parseDoubleValue(map.value("y"), y) ||
            !parseDoubleValue(map.value("z"), z)) {
            error = "Vector3 x/y/z must be numbers";
            return false;
        }
    } else {
        if (!parseDoubleValue(map.value("r"), x) || !parseDoubleValue(map.value("g"), y) ||
            !parseDoubleValue(map.value("b"), z)) {
            error = "Vector3 r/g/b must be numbers";
            return false;
        }
    }

    out = Vector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return true;
}

bool parseVector4(const QVariant& value, Vector4& out, QString& error) {
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (list.size() != 4) {
            error = "Vector4 array must contain 4 elements";
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 0.0;
        if (!parseDoubleValue(list.at(0), x) || !parseDoubleValue(list.at(1), y) ||
            !parseDoubleValue(list.at(2), z) || !parseDoubleValue(list.at(3), w)) {
            error = "Vector4 array elements must be numbers";
            return false;
        }
        out = Vector4(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                      static_cast<float>(w));
        return true;
    }

    if (!value.canConvert<QVariantMap>()) {
        error = "Vector4 expects object or array";
        return false;
    }

    const QVariantMap map = value.toMap();
    const bool hasXYZW = map.contains("x") || map.contains("y") || map.contains("z") || map.contains("w");
    const bool hasRGBA = map.contains("r") || map.contains("g") || map.contains("b") || map.contains("a");
    if (!hasXYZW && !hasRGBA) {
        error = "Vector4 expects x/y/z/w or r/g/b/a fields";
        return false;
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    if (hasXYZW) {
        if (!parseDoubleValue(map.value("x"), x) || !parseDoubleValue(map.value("y"), y) ||
            !parseDoubleValue(map.value("z"), z) || !parseDoubleValue(map.value("w"), w)) {
            error = "Vector4 x/y/z/w must be numbers";
            return false;
        }
    } else {
        if (!parseDoubleValue(map.value("r"), x) || !parseDoubleValue(map.value("g"), y) ||
            !parseDoubleValue(map.value("b"), z) || !parseDoubleValue(map.value("a"), w)) {
            error = "Vector4 r/g/b/a must be numbers";
            return false;
        }
    }

    out = Vector4(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                  static_cast<float>(w));
    return true;
}

std::shared_ptr<trans::TransDB> extractNestedTransDb(const std::any& value) {
    try {
        if (value.type() == typeid(std::shared_ptr<trans::TransDB>)) {
            return std::any_cast<std::shared_ptr<trans::TransDB>>(value);
        }
    } catch (const std::bad_any_cast&) {
        return {};
    }
    return {};
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    AIChatBridge, "AIChatBridge", &AIChatBridge::create)

QVariantMap propertyBagFromTransDb(const std::shared_ptr<trans::TransDB>& object,
                                   int depth,
                                   std::set<const trans::TransDB*>& visiting);

QVariant anyToVariantForDb(const std::any& value,
                           int depth,
                           std::set<const trans::TransDB*>& visiting) {
    if (!value.has_value()) {
        return {};
    }

    try {
        if (value.type() == typeid(bool)) {
            return QVariant(std::any_cast<bool>(value));
        }
        if (value.type() == typeid(int)) {
            return QVariant(std::any_cast<int>(value));
        }
        if (value.type() == typeid(unsigned int)) {
            return QVariant::fromValue(static_cast<qulonglong>(std::any_cast<unsigned int>(value)));
        }
        if (value.type() == typeid(long)) {
            return QVariant::fromValue(static_cast<qlonglong>(std::any_cast<long>(value)));
        }
        if (value.type() == typeid(unsigned long)) {
            return QVariant::fromValue(static_cast<qulonglong>(std::any_cast<unsigned long>(value)));
        }
        if (value.type() == typeid(long long)) {
            return QVariant::fromValue(static_cast<qlonglong>(std::any_cast<long long>(value)));
        }
        if (value.type() == typeid(unsigned long long)) {
            return QVariant::fromValue(static_cast<qulonglong>(std::any_cast<unsigned long long>(value)));
        }
        if (value.type() == typeid(float)) {
            return QVariant(std::any_cast<float>(value));
        }
        if (value.type() == typeid(double)) {
            return QVariant(std::any_cast<double>(value));
        }
        if (value.type() == typeid(std::string)) {
            return QString::fromStdString(std::any_cast<std::string>(value));
        }
        if (value.type() == typeid(QString)) {
            return std::any_cast<QString>(value);
        }
        if (value.type() == typeid(Vector2)) {
            return vector2ToVariantMap(std::any_cast<Vector2>(value));
        }
        if (value.type() == typeid(Vector3)) {
            return vector3ToVariantMap(std::any_cast<Vector3>(value));
        }
        if (value.type() == typeid(Vector4)) {
            return vector4ToVariantMap(std::any_cast<Vector4>(value));
        }
        if (value.type() == typeid(Color)) {
            return colorToVariantMap(std::any_cast<Color>(value));
        }
        if (value.type() == typeid(DBInstanceID)) {
            return QVariant::fromValue(static_cast<qlonglong>(
                std::any_cast<DBInstanceID>(value).getValue()));
        }
        if (value.type() == typeid(trans::DBInstanceID)) {
            return QVariant::fromValue(
                static_cast<qlonglong>(
                    std::any_cast<trans::DBInstanceID>(value).getValue()));
        }
        if (value.type() == typeid(TypeID)) {
            return QVariantMap{
                {"value", static_cast<qulonglong>(static_cast<uint32_t>(std::any_cast<TypeID>(value)))},
                {"name", typeIdToString(std::any_cast<TypeID>(value))}
            };
        }
        if (value.type() == typeid(trans::TypeID)) {
            const auto type = std::any_cast<trans::TypeID>(value);
            return QVariantMap{
                {"value", static_cast<qulonglong>(type.getValue())},
                {"name", typeIdToString(static_cast<TypeID>(type.getValue()))}
            };
        }
        if (value.type() == typeid(Transform)) {
            const auto transform = std::any_cast<Transform>(value);
            return QVariantMap{
                {"position", vector3ToVariantMap(transform.getPosition())},
                {"rotation", vector3ToVariantMap(transform.getEuler())},
                {"scale", vector3ToVariantMap(transform.getScale())}
            };
        }

        const auto nested = extractNestedTransDb(value);
        if (nested) {
            if (depth >= kDbPropertyBagMaxDepth) {
                return QVariantMap{{"__truncated", true}, {"reason", "max_depth_reached"}};
            }
            return propertyBagFromTransDb(nested, depth + 1, visiting);
        }
    } catch (const std::bad_any_cast&) {
        return QString("<bad_any_cast>");
    }

    return QString("<unsupported:%1>").arg(value.type().name());
}

QVariantMap propertyBagFromTransDb(const std::shared_ptr<trans::TransDB>& object,
                                   int depth,
                                   std::set<const trans::TransDB*>& visiting) {
    QVariantMap bag;
    if (!object) {
        return bag;
    }

    const auto* raw = object.get();
    if (visiting.find(raw) != visiting.end()) {
        bag.insert("__cycle", true);
        return bag;
    }

    visiting.insert(raw);
    const auto entries = object->propertyEntries();
    for (const auto& entry : entries) {
        const QString key = QString::fromUtf8(entry.prop.name());
        bag.insert(key, anyToVariantForDb(entry.value, depth, visiting));
    }
    visiting.erase(raw);
    return bag;
}

struct ResolvedProperty {
    trans::Prop prop;
    std::any value;
};

std::optional<ResolvedProperty> resolveStoredProperty(const std::shared_ptr<trans::TransDB>& object,
                                                      const QString& fieldName) {
    if (!object) {
        return std::nullopt;
    }
    const QString expected = fieldName.trimmed();
    if (expected.isEmpty()) {
        return std::nullopt;
    }

    const auto entries = object->propertyEntries();
    for (const auto& entry : entries) {
        const QString current = QString::fromUtf8(entry.prop.name());
        if (QString::compare(current, expected, Qt::CaseInsensitive) == 0) {
            return ResolvedProperty{entry.prop, entry.value};
        }
    }
    return std::nullopt;
}

bool convertVariantToTargetAny(const QVariant& input,
                               const std::any& oldAny,
                               std::any& out,
                               QString& error) {
    error.clear();
    if (!oldAny.has_value()) {
        error = "target field has no runtime type";
        return false;
    }

    const std::type_info& target = oldAny.type();

    if (target == typeid(bool)) {
        if (input.typeId() == QMetaType::Bool) {
            out = std::any(input.toBool());
            return true;
        }
        const QString text = input.toString().trimmed().toLower();
        if (text == "true" || text == "1") {
            out = std::any(true);
            return true;
        }
        if (text == "false" || text == "0") {
            out = std::any(false);
            return true;
        }
        error = "bool expects true/false";
        return false;
    }

    if (target == typeid(int)) {
        bool ok = false;
        const int value = input.toInt(&ok);
        if (!ok) {
            error = "int expects numeric value";
            return false;
        }
        out = std::any(value);
        return true;
    }

    if (target == typeid(unsigned int)) {
        qulonglong value = 0;
        if (!parseUnsignedLongLong(input, value) || value > std::numeric_limits<unsigned int>::max()) {
            error = "unsigned int expects non-negative integer";
            return false;
        }
        out = std::any(static_cast<unsigned int>(value));
        return true;
    }

    if (target == typeid(long)) {
        bool ok = false;
        const long long value = input.toLongLong(&ok);
        if (!ok || value < std::numeric_limits<long>::min() || value > std::numeric_limits<long>::max()) {
            error = "long expects integer within range";
            return false;
        }
        out = std::any(static_cast<long>(value));
        return true;
    }

    if (target == typeid(unsigned long)) {
        qulonglong value = 0;
        if (!parseUnsignedLongLong(input, value) || value > std::numeric_limits<unsigned long>::max()) {
            error = "unsigned long expects non-negative integer";
            return false;
        }
        out = std::any(static_cast<unsigned long>(value));
        return true;
    }

    if (target == typeid(long long)) {
        bool ok = false;
        const long long value = input.toLongLong(&ok);
        if (!ok) {
            error = "long long expects integer";
            return false;
        }
        out = std::any(static_cast<long long>(value));
        return true;
    }

    if (target == typeid(unsigned long long)) {
        qulonglong value = 0;
        if (!parseUnsignedLongLong(input, value)) {
            error = "unsigned long long expects non-negative integer";
            return false;
        }
        out = std::any(static_cast<unsigned long long>(value));
        return true;
    }

    if (target == typeid(float)) {
        double value = 0.0;
        if (!parseDoubleValue(input, value)) {
            error = "float expects numeric value";
            return false;
        }
        out = std::any(static_cast<float>(value));
        return true;
    }

    if (target == typeid(double)) {
        double value = 0.0;
        if (!parseDoubleValue(input, value)) {
            error = "double expects numeric value";
            return false;
        }
        out = std::any(value);
        return true;
    }

    if (target == typeid(std::string)) {
        out = std::any(input.toString().toStdString());
        return true;
    }

    if (target == typeid(QString)) {
        out = std::any(input.toString());
        return true;
    }

    if (target == typeid(Vector2)) {
        Vector2 vec;
        if (!parseVector2(input, vec, error)) {
            return false;
        }
        out = std::any(vec);
        return true;
    }

    if (target == typeid(Vector3)) {
        Vector3 vec;
        if (!parseVector3(input, vec, error)) {
            return false;
        }
        out = std::any(vec);
        return true;
    }

    if (target == typeid(Vector4)) {
        Vector4 vec;
        if (!parseVector4(input, vec, error)) {
            return false;
        }
        out = std::any(vec);
        return true;
    }

    if (target == typeid(Color)) {
        Vector4 vec4;
        if (!parseVector4(input, vec4, error)) {
            return false;
        }
        out = std::any(Color(vec4.x, vec4.y, vec4.z, vec4.w));
        return true;
    }

    if (target == typeid(DBInstanceID)) {
        DBInstanceID::ValueType value = 0;
        if (!parseDBInstanceIdValue(input, value)) {
            error = "DBInstanceID expects integer";
            return false;
        }
        out = std::any(DBInstanceID(value));
        return true;
    }

    if (target == typeid(trans::DBInstanceID)) {
        DBInstanceID::ValueType value = 0;
        if (!parseDBInstanceIdValue(input, value)) {
            error = "trans::DBInstanceID expects integer";
            return false;
        }
        out = std::any(trans::DBInstanceID(value));
        return true;
    }

    if (target == typeid(TypeID)) {
        qulonglong value = 0;
        if (!parseUnsignedLongLong(input, value)) {
            error = "TypeID expects integer";
            return false;
        }
        out = std::any(static_cast<TypeID>(static_cast<uint32_t>(value)));
        return true;
    }

    if (target == typeid(trans::TypeID)) {
        qulonglong value = 0;
        if (!parseUnsignedLongLong(input, value)) {
            error = "trans::TypeID expects integer";
            return false;
        }
        out = std::any(trans::TypeID(static_cast<uint32_t>(value)));
        return true;
    }

    if (target == typeid(Transform)) {
        if (!input.canConvert<QVariantMap>()) {
            error = "Transform expects object";
            return false;
        }
        Transform transform;
        try {
            transform = std::any_cast<Transform>(oldAny);
        } catch (const std::bad_any_cast&) {
            transform = Transform();
        }

        const QVariantMap map = input.toMap();
        bool touched = false;

        if (map.contains("position")) {
            Vector3 position;
            if (!parseVector3(map.value("position"), position, error)) {
                error = QString("Transform.position invalid: %1").arg(error);
                return false;
            }
            transform.setPosition(position);
            touched = true;
        }

        if (map.contains("rotation")) {
            Vector3 rotation;
            if (!parseVector3(map.value("rotation"), rotation, error)) {
                error = QString("Transform.rotation invalid: %1").arg(error);
                return false;
            }
            const Vector3 position = transform.getPosition();
            const Vector3 scale = transform.getScale();
            Transform rebuilt;
            rebuilt.identity();
            rebuilt.setPosition(position);
            rebuilt.rotateX(rotation.x);
            rebuilt.rotateY(rotation.y);
            rebuilt.rotateZ(rotation.z);
            rebuilt.setScale(scale);
            transform = rebuilt;
            touched = true;
        }

        if (map.contains("scale")) {
            Vector3 scale;
            if (!parseVector3(map.value("scale"), scale, error)) {
                error = QString("Transform.scale invalid: %1").arg(error);
                return false;
            }
            transform.setScale(scale);
            touched = true;
        }

        if (!touched) {
            error = "Transform expects at least one of position/rotation/scale";
            return false;
        }

        out = std::any(transform);
        return true;
    }

    if (target == typeid(std::shared_ptr<trans::TransDB>)) {
        error = "nested object field cannot be replaced directly";
        return false;
    }

    error = QString("unsupported target type: %1").arg(target.name());
    return false;
}

TransactionManager::TransactionMetadata buildDbPatchMetadata(const QVariantMap& meta) {
    TransactionManager::TransactionMetadata txMeta;
    txMeta.emplace("actionCode", "db_patch");
    const auto addIfPresent = [&meta, &txMeta](const char* key) {
        const QString value = meta.value(QString::fromLatin1(key)).toString().trimmed();
        if (!value.isEmpty()) {
            txMeta.emplace(key, value.toStdString());
        }
    };
    addIfPresent("source");
    addIfPresent("turnId");
    addIfPresent("traceId");
    addIfPresent("toolCallId");
    addIfPresent("requestId");
    return txMeta;
}

void applyDbPatchRecursive(const std::shared_ptr<trans::TransDB>& object,
                           const QVariantMap& changes,
                           const QString& pathPrefix,
                           QVariantList& applied,
                           QVariantList& failed) {
    if (!object) {
        failed.push_back(QVariantMap{
            {"field", pathPrefix},
            {"error", "target object is null"},
        });
        return;
    }

    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString field = it.key().trimmed();
        if (field.isEmpty()) {
            failed.push_back(QVariantMap{
                {"field", pathPrefix},
                {"error", "field name is empty"},
            });
            continue;
        }

        const int dotIndex = field.indexOf('.');
        if (dotIndex > 0) {
            const QString head = field.left(dotIndex).trimmed();
            const QString tail = field.mid(dotIndex + 1).trimmed();
            const QString headPath = pathPrefix.isEmpty() ? head : (pathPrefix + "." + head);
            auto resolvedHead = resolveStoredProperty(object, head);
            if (!resolvedHead.has_value()) {
                failed.push_back(QVariantMap{
                    {"field", headPath},
                    {"error", "field not found"},
                });
                continue;
            }
            const auto nested = extractNestedTransDb(resolvedHead->value);
            if (!nested) {
                failed.push_back(QVariantMap{
                    {"field", headPath},
                    {"error", "field is not nested object"},
                });
                continue;
            }
            QVariantMap nestedChanges;
            nestedChanges.insert(tail, it.value());
            applyDbPatchRecursive(nested, nestedChanges, headPath, applied, failed);
            continue;
        }

        const QString fullPath = pathPrefix.isEmpty() ? field : (pathPrefix + "." + field);
        auto resolved = resolveStoredProperty(object, field);
        if (!resolved.has_value()) {
            failed.push_back(QVariantMap{
                {"field", fullPath},
                {"error", "field not found"},
            });
            continue;
        }

        const auto nested = extractNestedTransDb(resolved->value);
        if (nested && it.value().canConvert<QVariantMap>()) {
            applyDbPatchRecursive(nested, it.value().toMap(), fullPath, applied, failed);
            continue;
        }

        std::any converted;
        QString convertError;
        if (!convertVariantToTargetAny(it.value(), resolved->value, converted, convertError)) {
            failed.push_back(QVariantMap{
                {"field", fullPath},
                {"error", convertError},
            });
            continue;
        }

        const bool changed = object->setPropertyAny(resolved->prop, converted);
        std::set<const trans::TransDB*> visiting;
        const std::any newAny = object->getPropertyImpl(resolved->prop);
        applied.push_back(QVariantMap{
            {"field", fullPath},
            {"changed", changed},
            {"oldValue", anyToVariantForDb(resolved->value, 0, visiting)},
            {"newValue", anyToVariantForDb(newAny, 0, visiting)},
        });
    }
}

QVariantMap executeDbQueryTool(const QVariantMap& args) {
    QVariantMap result;
    result.insert("success", false);

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        result.insert("error", "DocumentManager unavailable");
        result.insert("error_code", "DOC_MANAGER_UNAVAILABLE");
        return result;
    }

    DBInstanceID::ValueType objectId = 0;
    const bool hasObjectId =
        parseDBInstanceIdValue(args.value("objectId"), objectId) && objectId != 0;
    const uint32_t typeFilter = static_cast<uint32_t>(args.value("typeId").toUInt());
    const QString typeNameFilter = args.value("typeName").toString().trimmed().toLower();
    const QString nameContains = args.value("nameContains").toString().trimmed().toLower();
    const int limit = std::clamp(args.value("limit", 50).toInt(), 1, 500);

    QVariantList objects;
    auto appendObject = [&](const std::shared_ptr<AutoRegisterDB>& object) {
        if (!object) {
            return;
        }
        const TypeID typeId = object->getTypeID();
        if (typeFilter != 0 && static_cast<uint32_t>(typeId) != typeFilter) {
            return;
        }

        const QString typeName = typeIdToString(typeId);
        const QString normalizedTypeName = typeName.toLower();
        if (!typeNameFilter.isEmpty() &&
            normalizedTypeName != typeNameFilter &&
            !normalizedTypeName.contains(typeNameFilter)) {
            return;
        }

        const QString displayName = QString::fromStdString(object->getDisplayName());
        const QString normalizedDisplayName = displayName.toLower();
        if (!nameContains.isEmpty() &&
            !normalizedDisplayName.contains(nameContains) &&
            !normalizedTypeName.contains(nameContains)) {
            return;
        }

        std::set<const trans::TransDB*> visiting;
        QVariantMap item;
        item.insert("dbId", static_cast<qlonglong>(
            object->getDBInstanceID().getValue()));
        item.insert("typeId", static_cast<qulonglong>(static_cast<uint32_t>(typeId)));
        item.insert("typeName", typeName);
        item.insert("displayName", displayName.isEmpty() ? typeName : displayName);
        item.insert("propertyBag",
                    propertyBagFromTransDb(std::static_pointer_cast<trans::TransDB>(object), 0, visiting));
        objects.push_back(item);
    };

    if (hasObjectId) {
        auto object = docManager->getDBInstance(DBInstanceID(objectId));
        if (!object) {
            result.insert("error", QString("object not found: %1").arg(objectId));
            result.insert("error_code", "OBJECT_NOT_FOUND");
            return result;
        }
        appendObject(object);
    } else {
        const auto allObjects = docManager->getDBInstancesByType(TypeID::UNKNOWN);
        for (const auto& object : allObjects) {
            if (objects.size() >= limit) {
                break;
            }
            appendObject(object);
        }
    }

    result.insert("success", true);
    result.insert("matchedCount", objects.size());
    result.insert("objects", objects);
    return result;
}

QVariantMap executeDbPatchTool(const QVariantMap& args, const QVariantMap& meta) {
    QVariantMap result;
    result.insert("success", false);

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        result.insert("error", "DocumentManager unavailable");
        result.insert("error_code", "DOC_MANAGER_UNAVAILABLE");
        return result;
    }

    DBInstanceID::ValueType objectId = 0;
    if (!parseDBInstanceIdValue(args.value("objectId"), objectId) || objectId == 0) {
        result.insert("error", "db_patch requires objectId");
        result.insert("error_code", "OBJECT_ID_MISSING");
        return result;
    }

    if (!args.value("changes").canConvert<QVariantMap>()) {
        result.insert("error", "db_patch requires changes object");
        result.insert("error_code", "CHANGES_MISSING");
        return result;
    }

    const QVariantMap changes = args.value("changes").toMap();
    if (changes.isEmpty()) {
        result.insert("success", true);
        result.insert("objectId", objectId);
        result.insert("applied", QVariantList{});
        result.insert("failed", QVariantList{});
        result.insert("message", "no changes");
        return result;
    }

    auto object = docManager->getDBInstance(DBInstanceID(objectId));
    if (!object) {
        result.insert("error", QString("object not found: %1").arg(objectId));
        result.insert("error_code", "OBJECT_NOT_FOUND");
        return result;
    }

    const ScopedTransactionContextMetadata txScope(buildDbPatchMetadata(meta));
    TransactionGuard guard("AI DB Patch");

    QVariantList applied;
    QVariantList failed;
    applyDbPatchRecursive(std::static_pointer_cast<trans::TransDB>(object),
                          changes,
                          QString(),
                          applied,
                          failed);

    result.insert("success", failed.isEmpty());
    result.insert("partial", !applied.isEmpty() && !failed.isEmpty());
    result.insert("objectId", objectId);
    result.insert("applied", applied);
    result.insert("failed", failed);
    result.insert("appliedCount", applied.size());
    result.insert("failedCount", failed.size());
    return result;
}

void appendUndoTransactionsToTurnLedger(QHash<QString, QVariantMap>& turnLedger,
                                        const QString& turnId,
                                        qulonglong undoBefore,
                                        qulonglong undoAfter) {
    if (turnId.isEmpty() || !turnLedger.contains(turnId) || undoAfter <= undoBefore) {
        return;
    }

    const qulonglong undoDelta = undoAfter - undoBefore;
    if (undoDelta == 0) {
        return;
    }

    QVariantMap turn = turnLedger.value(turnId);
    QVariantList txIds = turn.value("transactionIds").toList();
    QVariantList txReceipts = turn.value("transactions").toList();
    const auto txSummaries = TransactionManager::instance().getUndoTransactionSummaries(
        static_cast<size_t>(undoDelta));
    for (auto it = txSummaries.rbegin(); it != txSummaries.rend(); ++it) {
        if (it->id == 0) {
            continue;
        }
        txIds.push_back(static_cast<qulonglong>(it->id));
        QVariantMap receipt;
        receipt.insert("id", static_cast<qulonglong>(it->id));
        receipt.insert("description", QString::fromStdString(it->description));
        receipt.insert("changeCount", static_cast<qulonglong>(it->changeCount));
        receipt.insert("metadata", transactionMetadataToVariantMap(it->metadata));
        txReceipts.push_back(receipt);
    }
    turn.insert("transactionIds", txIds);
    turn.insert("transactions", txReceipts);
    turnLedger.insert(turnId, turn);
}
} // namespace

AIChatBridge::AIChatBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
    m_agentBackend = resolveConfiguredBackend();
    if (m_agentBackend) {
        m_agentBackend->setEventSink(this);
        m_agentBackend->ensureReady();
    }

    connect(ActionManager::getInstance(),
            &ActionManager::invocationCompleted,
            this,
            [this](const QString& invocationId, const QVariantMap& outcome) {
                auto pendingIt = m_pendingAgentInvocations.find(invocationId);
                if (pendingIt == m_pendingAgentInvocations.end()) {
                    return;
                }

                const QVariantMap pending = pendingIt.value();
                m_pendingAgentInvocations.erase(pendingIt);
                QVariantMap finalOutcome = outcome;
                finalOutcome.insert("invocationId", invocationId);

                const qulonglong undoBefore = pending.value("undoBefore").toULongLong();
                const qulonglong undoAfter = static_cast<qulonglong>(
                    TransactionManager::instance().getUndoStackSize());
                appendUndoTransactionsToTurnLedger(
                    m_turnLedger, pending.value("turnId").toString(), undoBefore, undoAfter);

                m_lastActionExecution = finalOutcome;
                m_agentLatestActionExecution = finalOutcome;
                const QString callId = pending.value("callId").toString();
                const QString functionName = pending.value("functionName").toString();
                const QString resultText = QString::fromUtf8(
                    QJsonDocument::fromVariant(finalOutcome).toJson(QJsonDocument::Compact));
                const bool success = finalOutcome.value("success").toBool();
                upsertToolTraceMessage(callId,
                                       functionName,
                                       success ? "success" : "failed",
                                       {},
                                       resultText.left(5000),
                                       finalOutcome.value("error").toString());
                submitToolResult(callId, finalOutcome);
            });
    appendMessage("assistant", "AI Assistant ready. You can start chatting now.");
}

AIChatBridge* AIChatBridge::instance() {
    static AIChatBridge* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new AIChatBridge();
    }
    return s_instance;
}

AIChatBridge* AIChatBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    AIChatBridge* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

void AIChatBridge::setPanelVisible(bool visible) {
    if (m_panelVisible == visible) {
        return;
    }
    m_panelVisible = visible;
    emit panelVisibleChanged();
}

void AIChatBridge::openPanel() {
    setPanelVisible(true);
}

void AIChatBridge::closePanel() {
    setPanelVisible(false);
}

void AIChatBridge::togglePanel() {
    setPanelVisible(!m_panelVisible);
}

void AIChatBridge::clearMessages() {
    cancelStreamingRequest("clear_messages");
    if (m_agentBackend) {
        m_agentBackend->resetSession();
    }
    m_messages.clear();
    m_lastActionExecution.clear();
    m_turnLedger.clear();
    m_activeTurnId.clear();
    m_latestRetryableTurnId.clear();
    m_latestCompletedTurnId.clear();
    appendMessage("assistant", "Chat history cleared.");
    emit messagesChanged();
}

void AIChatBridge::sendMessage(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (m_streaming) {
        appendMessage("assistant", "A previous request is still running. Please wait.");
        return;
    }

    const QString turnId = beginTurn(trimmed);
    appendMessage("user", trimmed, QVariantMap{{"turnId", turnId}, {"turnStatus", "running"}});
    m_lastActionExecution.clear();

    if (!m_agentBackend) {
        const QString error = QStringLiteral("Configured Agent backend is unavailable.");
        const QVariantMap extra = finalizeActiveTurn("failed_protocol", error);
        appendMessage("assistant", error, extra);
        return;
    }

    setStreaming(true);
    requestModelReply();
}

QVariantMap AIChatBridge::queryTurn(const QString& turnId) const {
    QVariantMap result;
    result.insert("activeTurnId", m_activeTurnId);
    result.insert("latestCompletedTurnId", m_latestCompletedTurnId);
    result.insert("latestRetryableTurnId", m_latestRetryableTurnId);

    QString resolvedTurnId = turnId.trimmed();
    if (resolvedTurnId.isEmpty()) {
        resolvedTurnId = !m_activeTurnId.isEmpty() ? m_activeTurnId : m_latestCompletedTurnId;
    }
    result.insert("turnId", resolvedTurnId);

    if (resolvedTurnId.isEmpty()) {
        result.insert("found", false);
        return result;
    }

    const QVariantMap turn = m_turnLedger.value(resolvedTurnId);
    if (turn.isEmpty()) {
        result.insert("found", false);
        return result;
    }

    result.insert("found", true);
    for (auto it = turn.constBegin(); it != turn.constEnd(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

bool AIChatBridge::retryTurn(const QString& turnId) {
    if (m_streaming) {
        appendMessage("assistant", "Retry blocked: a request is still running.");
        return false;
    }

    QString targetId = turnId.trimmed();
    if (targetId.isEmpty()) {
        targetId = !m_latestRetryableTurnId.isEmpty() ? m_latestRetryableTurnId : m_latestCompletedTurnId;
    }
    if (targetId.isEmpty()) {
        appendMessage("assistant", "Retry blocked: no finished turn available.");
        return false;
    }
    if (!m_turnLedger.contains(targetId)) {
        appendMessage("assistant", QString("Retry blocked: turn '%1' not found.").arg(targetId));
        return false;
    }

    QVariantMap targetTurn = m_turnLedger.value(targetId);
    if (!isTurnRetryable(targetTurn)) {
        appendMessage("assistant", QString("Retry blocked: turn '%1' is not retryable.").arg(targetId));
        return false;
    }

    const std::vector<qulonglong> turnTxIds = parseTurnTransactionIds(targetTurn);
    int undoneCount = 0;
    QString rollbackError;
    if (!turnTxIds.empty()) {
        const auto topSummaries =
            TransactionManager::instance().getUndoTransactionSummaries(turnTxIds.size());
        if (topSummaries.size() < turnTxIds.size()) {
            appendMessage("assistant", "Retry blocked: scene changed by external transactions.");
            return false;
        }

        bool consistent = true;
        for (size_t i = 0; i < turnTxIds.size(); ++i) {
            const qulonglong expectedId = turnTxIds[turnTxIds.size() - 1 - i];
            const auto& summary = topSummaries[i];
            const QString summaryTurnId =
                QString::fromStdString(summary.metadata.count("turnId")
                                           ? summary.metadata.at("turnId")
                                           : std::string{});
            if (summary.id != expectedId || summaryTurnId != targetId) {
                consistent = false;
                break;
            }
        }
        if (!consistent) {
            appendMessage("assistant", "Retry blocked: scene changed by external transactions.");
            return false;
        }

        const bool rollbackOk =
            rollbackUndoSteps(static_cast<qulonglong>(turnTxIds.size()), targetId, undoneCount, rollbackError);
        if (!rollbackOk) {
            appendMessage("assistant", QString("Retry failed during rollback: %1").arg(rollbackError));
            return false;
        }
    }

    targetTurn.insert("status", "retried");
    targetTurn.insert("retryable", false);
    m_turnLedger.insert(targetId, targetTurn);
    if (m_latestRetryableTurnId == targetId) {
        m_latestRetryableTurnId.clear();
    }

    appendMessage("assistant",
                  QString("Retry: rolled back %1 undo step(s), resending previous request.")
                      .arg(undoneCount),
                  QVariantMap{{"retriedFromTurnId", targetId},
                              {"rollbackUndone", undoneCount},
                              {"rollbackMode", "transaction_ids"},
                              {"turnTransactionCount", static_cast<qulonglong>(turnTxIds.size())}});

    const QString userText = targetTurn.value("userText").toString().trimmed();
    if (userText.isEmpty()) {
        appendMessage("assistant", "Retry failed: original user message is empty.");
        return false;
    }

    sendMessage(userText);
    if (!m_activeTurnId.isEmpty() && m_turnLedger.contains(m_activeTurnId)) {
        QVariantMap newTurn = m_turnLedger.value(m_activeTurnId);
        newTurn.insert("retriedFromTurnId", targetId);
        m_turnLedger.insert(m_activeTurnId, newTurn);
    }

    return true;
}

bool AIChatBridge::cancelStreamingRequest(const QString& reason) {
    if (!m_streaming && m_pendingAgentInvocations.isEmpty()) {
        return false;
    }

    if (m_agentTurnInFlight && m_agentBackend) {
        m_agentBackend->cancelTurn();
    }
    m_agentTurnInFlight = false;
    m_agentActionTrace.clear();
    m_agentLatestActionExecution.clear();
    m_pendingAgentInvocations.clear();
    setStreaming(false);
    if (!m_activeTurnId.isEmpty()) {
        finalizeActiveTurn("failed_protocol", "request canceled", m_lastActionExecution);
    }

    if (!reason.trimmed().isEmpty()) {
        LOG_WARN("[AI Chat] canceled in-flight request(s), reason={}", reason.toStdString());
    } else {
        LOG_WARN("[AI Chat] canceled in-flight request(s)");
    }
    return true;
}

void AIChatBridge::appendMessage(const QString& role,
                                 const QString& text,
                                 const QVariantMap& extra) {
    QVariantMap message = extra;
    message.insert("role", role);
    message.insert("text", text);
    message.insert("time", QDateTime::currentDateTime().toString("HH:mm:ss"));
    m_messages.push_back(message);
    emit messagesChanged();
}

void AIChatBridge::upsertToolTraceMessage(const QString& traceId,
                                          const QString& toolName,
                                          const QString& status,
                                          const QString& arguments,
                                          const QString& resultPayload,
                                          const QString& errorMessage) {
    const bool running = status == "running";
    const bool failed = status == "failed";
    const QString statusText = running ? "running" : (failed ? "failed" : "success");
    const QString summaryText = running
                                    ? QString("Tool %1 is running").arg(toolName)
                                    : QString("Tool %1 finished: %2").arg(toolName, statusText);

    for (int i = m_messages.size() - 1; i >= 0; --i) {
        QVariantMap message = m_messages.at(i).toMap();
        if (message.value("role").toString() != "tool_trace") {
            continue;
        }
        if (message.value("traceId").toString() != traceId) {
            continue;
        }

        message.insert("text", summaryText);
        message.insert("toolName", toolName);
        message.insert("traceId", traceId);
        message.insert("status", status);
        if (!arguments.isEmpty()) {
            message.insert("arguments", arguments);
        }
        if (!resultPayload.isEmpty()) {
            message.insert("resultPayload", resultPayload);
        }
        if (!errorMessage.isEmpty()) {
            message.insert("error", errorMessage);
        }
        if (!running) {
            message.insert("success", !failed);
        }
        m_messages[i] = message;
        emit messagesChanged();
        return;
    }

    QVariantMap extra;
    extra.insert("traceId", traceId);
    extra.insert("toolName", toolName);
    extra.insert("status", status);
    extra.insert("arguments", arguments);
    if (!resultPayload.isEmpty()) {
        extra.insert("resultPayload", resultPayload);
    }
    if (!errorMessage.isEmpty()) {
        extra.insert("error", errorMessage);
    }
    if (!running) {
        extra.insert("success", !failed);
    }
    appendMessage("tool_trace", summaryText, extra);
}

void AIChatBridge::setStreaming(bool streaming) {
    if (m_streaming == streaming) {
        return;
    }
    m_streaming = streaming;
    emit streamingChanged();
}

void AIChatBridge::requestModelReply() {
    if (!m_agentBackend) {
        const QString error = QStringLiteral("Configured Agent backend is unavailable.");
        const QVariantMap extra = finalizeActiveTurn("failed_protocol", error);
        appendMessage("assistant", error, extra);
        setStreaming(false);
        return;
    }

    const QString instructions = buildSystemPrompt();
    if (instructions.trimmed().isEmpty()) {
        const QString error = QStringLiteral("AI system prompt is unavailable or invalid.");
        const QVariantMap extra = finalizeActiveTurn("failed_protocol", error);
        appendMessage("assistant", error, extra);
        setStreaming(false);
        return;
    }

    m_agentActionTrace.clear();
    m_agentLatestActionExecution.clear();
    m_lastActionExecution.clear();

    const QByteArray turnIdBytes = m_activeTurnId.toUtf8();
    const QByteArray inputBytes = latestUserMessage().toUtf8();
    const QByteArray instructionsBytes = instructions.toUtf8();
    GPlatform::AI::AgentTurnRequest request;
    request.applicationTurnId =
        std::string(turnIdBytes.constData(), static_cast<std::size_t>(turnIdBytes.size()));
    request.input =
        std::string(inputBytes.constData(), static_cast<std::size_t>(inputBytes.size()));
    request.instructions =
        std::string(instructionsBytes.constData(),
                    static_cast<std::size_t>(instructionsBytes.size()));
    request.tools = buildToolDefinitions();

    m_agentTurnInFlight = true;
    const GPlatform::AI::AgentTurnStartResult startResult =
        m_agentBackend->startTurn(request);
    if (!startResult.accepted) {
        m_agentTurnInFlight = false;
        const QString error = QString::fromUtf8(
            startResult.error.data(), static_cast<qsizetype>(startResult.error.size()));
        const QVariantMap extra = finalizeActiveTurn("failed_protocol", error);
        appendMessage("assistant",
                      error.isEmpty() ? QStringLiteral("Failed to start Agent turn.") : error,
                      extra);
        setStreaming(false);
    }
}

void AIChatBridge::submitToolResult(const QString& callId, const QVariantMap& result) {
    if (!m_agentBackend) {
        LOG_ERROR("[AI Agent] cannot submit tool result without a configured backend");
        return;
    }

    const QByteArray callIdBytes = callId.toUtf8();
    const QByteArray payload =
        QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
    const bool submitted = m_agentBackend->submitToolResult(
        GPlatform::AI::AgentToolResult{
            std::string(callIdBytes.constData(),
                        static_cast<std::size_t>(callIdBytes.size())),
            result.value("success").toBool(),
            std::string(payload.constData(), static_cast<std::size_t>(payload.size()))});
    if (!submitted) {
        LOG_ERROR("[AI Agent] backend rejected tool result callId={}",
                  callId.toStdString());
        m_agentBackend->cancelTurn();
    }
}

void AIChatBridge::onAgentTextDelta(const GPlatform::AI::AgentTextDelta& delta) {
    const QString turnId = QString::fromUtf8(
        delta.applicationTurnId.data(),
        static_cast<qsizetype>(delta.applicationTurnId.size()));
    if (!m_agentTurnInFlight || turnId != m_activeTurnId) {
        LOG_WARN("[AI Agent] ignored text delta for inactive turnId={}",
                 turnId.toStdString());
    }
}

void AIChatBridge::onAgentToolCall(const GPlatform::AI::AgentToolCall& call) {
    const QString turnId = QString::fromUtf8(
        call.applicationTurnId.data(),
        static_cast<qsizetype>(call.applicationTurnId.size()));
    const QString callId = QString::fromUtf8(
        call.callId.data(), static_cast<qsizetype>(call.callId.size())).trimmed();
    const QString functionName = QString::fromUtf8(
        call.name.data(), static_cast<qsizetype>(call.name.size())).trimmed();
    const QByteArray argumentsBytes(call.argumentsJson.data(),
                                    static_cast<qsizetype>(call.argumentsJson.size()));
    const QString argumentsText = QString::fromUtf8(argumentsBytes);

    if (!m_agentTurnInFlight || turnId != m_activeTurnId) {
        submitToolResult(callId,
                         QVariantMap{{"success", false},
                                     {"error", "GPlatform has no matching active Agent turn."},
                                     {"error_code", "TURN_NOT_ACTIVE"}});
        return;
    }

    upsertToolTraceMessage(callId,
                           functionName,
                           "running",
                           argumentsText.left(3000),
                           {},
                           {});

    QJsonParseError parseError;
    const QJsonDocument argumentsDocument =
        QJsonDocument::fromJson(argumentsBytes, &parseError);
    QVariantMap toolResult;
    if (callId.isEmpty() || functionName.isEmpty()) {
        toolResult = QVariantMap{{"success", false},
                                 {"error", "tool call id and name are required"},
                                 {"error_code", "TOOL_CALL_INVALID"}};
    } else if (parseError.error != QJsonParseError::NoError ||
               !argumentsDocument.isObject()) {
        toolResult = QVariantMap{{"success", false},
                                 {"error", "tool arguments must be a JSON object"},
                                 {"error_code", "ARGS_NOT_OBJECT"}};
    } else {
        toolResult = executeToolCall(functionName,
                                     argumentsDocument.object().toVariantMap(),
                                     callId,
                                     m_agentActionTrace,
                                     m_agentLatestActionExecution);
    }

    const QString invocationId = toolResult.value("invocationId").toString();
    if (toolResult.value("pending").toBool() && !invocationId.isEmpty()) {
        QVariantMap pending = m_pendingAgentInvocations.value(invocationId);
        pending.insert("callId", callId);
        pending.insert("functionName", functionName);
        m_pendingAgentInvocations.insert(invocationId, pending);
        return;
    }

    const QString resultText = QString::fromUtf8(
        QJsonDocument::fromVariant(toolResult).toJson(QJsonDocument::Compact));
    upsertToolTraceMessage(callId,
                           functionName,
                           toolResult.value("success").toBool() ? "success" : "failed",
                           {},
                           resultText.left(5000),
                           toolResult.value("error").toString());
    submitToolResult(callId, toolResult);
}

void AIChatBridge::onAgentTurnFinished(const GPlatform::AI::AgentTurnResult& result) {
    const QString turnId = QString::fromUtf8(
        result.applicationTurnId.data(),
        static_cast<qsizetype>(result.applicationTurnId.size()));
    if (!m_agentTurnInFlight || turnId != m_activeTurnId) {
        LOG_WARN("[AI Agent] ignored completion for inactive turnId={}",
                 turnId.toStdString());
        return;
    }
    m_agentTurnInFlight = false;

    QVariantMap extra;
    if (!m_agentActionTrace.isEmpty()) {
        extra.insert("actions", m_agentActionTrace);
    }
    if (!m_agentLatestActionExecution.isEmpty()) {
        extra.insert("actionExecution", m_agentLatestActionExecution);
    }

    QString status = result.success ? QStringLiteral("success")
                                    : QStringLiteral("failed_protocol");
    QString finalError = QString::fromUtf8(
        result.error.data(), static_cast<qsizetype>(result.error.size()));
    if (!m_agentLatestActionExecution.isEmpty() &&
        !m_agentLatestActionExecution.value("success", true).toBool()) {
        status = hasAnyExecutedActionStep(m_agentLatestActionExecution)
                     ? QStringLiteral("failed_partial")
                     : QStringLiteral("failed_action");
        if (finalError.isEmpty()) {
            finalError = m_agentLatestActionExecution.value("error").toString();
        }
    }

    const QVariantMap turnExtra =
        finalizeActiveTurn(status, finalError, m_agentLatestActionExecution);
    for (auto it = turnExtra.constBegin(); it != turnExtra.constEnd(); ++it) {
        extra.insert(it.key(), it.value());
    }

    const QString text = QString::fromUtf8(
        result.text.data(), static_cast<qsizetype>(result.text.size())).trimmed();
    const QString finalText = result.success
                                  ? (text.isEmpty() ? QStringLiteral("Done.") : text)
                                  : QStringLiteral("AI request failed: %1")
                                        .arg(finalError.isEmpty()
                                                 ? QStringLiteral("unknown Agent error")
                                                 : finalError);
    appendMessage("assistant", finalText, extra);
    m_agentActionTrace.clear();
    m_agentLatestActionExecution.clear();
    m_pendingAgentInvocations.clear();
    setStreaming(false);
}

QVariantMap AIChatBridge::executeToolCall(const QString& functionName,
                                          const QVariantMap& arguments,
                                          const QString& callId,
                                          QVariantList& actionTrace,
                                          QVariantMap& latestActionExecution) {
    QVariantMap toolResult;

    if (functionName == "action_list") {
        toolResult.insert("success", true);
        toolResult.insert("actions", ActionManager::getInstance()->listActionsForAI());
    } else if (functionName == "action_describe") {
        const QString actionCode = arguments.value("actionCode").toString().trimmed();
        if (actionCode.isEmpty()) {
            toolResult.insert("success", false);
            toolResult.insert("error", "action_describe requires actionCode");
            toolResult.insert("error_code", "ACTION_CODE_MISSING");
        } else {
            toolResult = ActionManager::getInstance()->describeActionForAI(actionCode);
            toolResult.insert("success", !toolResult.isEmpty());
            if (toolResult.size() == 1) {
                toolResult.insert("error", QString("action is not exposed to AI: %1").arg(actionCode));
                toolResult.insert("error_code", "ACTION_NOT_EXPOSED");
            }
        }
    } else if (functionName == "action_invoke") {
        const QString actionCode = arguments.value("actionCode").toString().trimmed();
        if (!arguments.contains("params") ||
            !arguments.value("params").canConvert<QVariantMap>()) {
            toolResult.insert("success", false);
            toolResult.insert(
                "error",
                "action_invoke requires a 'params' object. Parameters must be inside params.");
            toolResult.insert("error_code", "PARAMS_OBJECT_REQUIRED");
            latestActionExecution = QVariantMap{{"success", false},
                                                {"failedActionCode", actionCode},
                                                {"error", "action_invoke requires a params object"}};
        } else if (actionCode.isEmpty()) {
            toolResult.insert("success", false);
            toolResult.insert("error", "action_invoke requires actionCode");
            toolResult.insert("error_code", "ACTION_CODE_MISSING");
            latestActionExecution = QVariantMap{{"success", false},
                                                {"failedActionCode", actionCode},
                                                {"error", "action_invoke requires actionCode"}};
        } else {
            const QVariantMap params = arguments.value("params").toMap();
            actionTrace.push_back(QVariantMap{{"actionCode", actionCode}, {"params", params}});

            QVariantMap meta;
            meta.insert("traceId", QUuid::createUuid().toString(QUuid::WithoutBraces));
            meta.insert("source", "ai_agent_dynamic_tool_call");
            meta.insert("toolCallId", callId);
            meta.insert("turnId", m_activeTurnId);

            const qulonglong undoBefore =
                static_cast<qulonglong>(TransactionManager::instance().getUndoStackSize());
            toolResult = ActionManager::getInstance()->invokeActionByAI(actionCode, params, meta);
            const qulonglong undoAfter =
                static_cast<qulonglong>(TransactionManager::instance().getUndoStackSize());
            if (toolResult.value("pending").toBool()) {
                const QString invocationId = toolResult.value("invocationId").toString();
                m_pendingAgentInvocations.insert(
                    invocationId,
                    QVariantMap{{"undoBefore", undoBefore},
                                {"turnId", m_activeTurnId},
                                {"actionCode", actionCode}});
            } else {
                appendUndoTransactionsToTurnLedger(
                    m_turnLedger, m_activeTurnId, undoBefore, undoAfter);
                m_lastActionExecution = toolResult;
                latestActionExecution = toolResult;
            }
            if (!toolResult.value("success").toBool()) {
                const QVariantMap descriptor =
                    ActionManager::getInstance()->describeActionForAI(actionCode);
                if (!descriptor.isEmpty()) {
                    toolResult.insert("actionDescriptor", descriptor);
                }
            }
        }
    } else if (functionName == "db_query") {
        toolResult = executeDbQueryTool(arguments);
    } else if (functionName == "db_patch") {
        QVariantMap meta;
        meta.insert("traceId", QUuid::createUuid().toString(QUuid::WithoutBraces));
        meta.insert("source", "ai_agent_db_tool_call");
        meta.insert("toolCallId", callId);
        meta.insert("turnId", m_activeTurnId);

        const qulonglong undoBefore =
            static_cast<qulonglong>(TransactionManager::instance().getUndoStackSize());
        toolResult = executeDbPatchTool(arguments, meta);
        const qulonglong undoAfter =
            static_cast<qulonglong>(TransactionManager::instance().getUndoStackSize());
        appendUndoTransactionsToTurnLedger(m_turnLedger, m_activeTurnId, undoBefore, undoAfter);

        m_lastActionExecution = toolResult;
        latestActionExecution = toolResult;
    } else {
        toolResult.insert("success", false);
        toolResult.insert("error", QString("unknown tool function '%1'").arg(functionName));
        toolResult.insert("error_code", "UNKNOWN_TOOL");
    }

    LOG_INFO("[AI Tool] result fn={} success={} error={}",
             functionName.toStdString(),
             toolResult.value("success").toBool() ? "true" : "false",
             toolResult.value("error").toString().left(200).toStdString());
    return toolResult;
}

QString AIChatBridge::beginTurn(const QString& userText) {
    const QString turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap turn;
    turn.insert("turnId", turnId);
    turn.insert("userText", userText);
    turn.insert("status", "running");
    turn.insert("retryable", false);
    turn.insert("transactionIds", QVariantList{});
    turn.insert("transactions", QVariantList{});
    m_turnLedger.insert(turnId, turn);
    m_activeTurnId = turnId;
    return turnId;
}

std::vector<qulonglong> AIChatBridge::parseTurnTransactionIds(const QVariantMap& turn) const {
    std::vector<qulonglong> ids;
    const QVariantList list = turn.value("transactionIds").toList();
    ids.reserve(list.size());
    for (const QVariant& value : list) {
        bool ok = false;
        const qulonglong id = value.toULongLong(&ok);
        if (ok && id > 0) {
            ids.push_back(id);
        }
    }
    return ids;
}

bool AIChatBridge::isTurnRetryable(const QVariantMap& turn) const {
    if (turn.isEmpty()) {
        return false;
    }
    if (turn.value("retryable").toBool()) {
        return true;
    }
    const QString status = turn.value("status").toString().trimmed();
    if (status == "success") {
        return true;
    }
    return status == "failed_network" || status == "failed_protocol" ||
           status == "failed_action" || status == "failed_partial" ||
           status == "failed_round_limit";
}

QVariantMap AIChatBridge::finalizeActiveTurn(const QString& status,
                                             const QString& error,
                                             const QVariantMap& actionExecution) {
    QVariantMap extra;
    if (m_activeTurnId.isEmpty()) {
        return extra;
    }

    QVariantMap turn = m_turnLedger.value(m_activeTurnId);
    if (turn.isEmpty()) {
        turn.insert("turnId", m_activeTurnId);
    }
    turn.insert("status", status);
    if (!error.trimmed().isEmpty()) {
        turn.insert("error", error.trimmed());
    }
    if (!actionExecution.isEmpty()) {
        turn.insert("actionExecution", actionExecution);
    }

    bool retryable = false;
    if (status.startsWith("failed")) {
        retryable = true;
    }
    turn.insert("retryable", retryable);
    m_turnLedger.insert(m_activeTurnId, turn);

    if (retryable) {
        m_latestRetryableTurnId = m_activeTurnId;
    } else if (m_latestRetryableTurnId == m_activeTurnId) {
        m_latestRetryableTurnId.clear();
    }
    m_latestCompletedTurnId = m_activeTurnId;

    extra.insert("turnId", m_activeTurnId);
    extra.insert("turnStatus", status);
    extra.insert("retryable", retryable);
    if (!error.trimmed().isEmpty()) {
        extra.insert("turnError", error.trimmed());
    }
    if (!actionExecution.isEmpty()) {
        extra.insert("actionExecution", actionExecution);
    }

    m_activeTurnId.clear();
    return extra;
}

bool AIChatBridge::rollbackUndoSteps(qulonglong undoSteps,
                                     const QString& turnId,
                                     int& undoneCount,
                                     QString& error) {
    undoneCount = 0;
    error.clear();

    if (undoSteps == 0) {
        return true;
    }

    qulonglong current =
        static_cast<qulonglong>(TransactionManager::instance().getUndoStackSize());
    if (undoSteps > current) {
        error = QString("requested undo steps %1 exceeds current undo stack size %2")
                    .arg(undoSteps)
                    .arg(current);
        return false;
    }

    for (qulonglong i = 0; i < undoSteps; ++i) {
        QVariantMap meta;
        meta.insert("source", "ai_retry_rollback");
        meta.insert("turnId", turnId);
        meta.insert("traceId", QUuid::createUuid().toString(QUuid::WithoutBraces));
        const bool ok =
            ActionManager::getInstance()->triggerActionByAI("edit.undo", QVariantMap{}, meta);
        if (!ok) {
            error = QString("edit.undo failed at step %1/%2").arg(i + 1).arg(undoSteps);
            return false;
        }
        undoneCount += 1;
    }

    return true;
}

QVariantMap AIChatBridge::loadAiRuntimeConfigFile() const {
    QFile file(":/qt/qml/GPlatform/config/ai_provider.json");
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN("[AI Config] failed to open resource config: :/qt/qml/GPlatform/config/ai_provider.json");
        return QVariantMap{};
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN("[AI Config] invalid JSON in ai_provider.json: {}",
                 parseError.errorString().toStdString());
        return QVariantMap{};
    }
    return doc.object().toVariantMap();
}

GPlatform::AI::IAgentBackend* AIChatBridge::resolveConfiguredBackend() const {
    const QVariantMap root = loadAiRuntimeConfigFile();
    if (root.isEmpty()) {
        LOG_ERROR("[AI Config] cannot resolve Agent backend from an empty configuration");
        return nullptr;
    }

    const QString providerName = root.value("model_provider").toString().trimmed();
    if (providerName.isEmpty()) {
        LOG_ERROR("[AI Config] model_provider is required");
        return nullptr;
    }
    const QVariantMap providers = root.value("model_providers").toMap();
    const QVariantMap providerNode = providers.value(providerName).toMap();
    if (providerNode.isEmpty()) {
        LOG_ERROR("[AI Config] model provider '{}' is not defined",
                  providerName.toStdString());
        return nullptr;
    }
    const QString backendId =
        providerNode.value("transport").toString().trimmed().toLower();
    if (backendId.isEmpty()) {
        LOG_ERROR("[AI Config] transport is required for model provider '{}'",
                  providerName.toStdString());
        return nullptr;
    }

    GPlatform::AI::IAgentBackend* backend =
        GPlatform::AI::AgentBackendCatalog::instance().backend(backendId.toStdString());
    if (!backend) {
        LOG_ERROR("[AI Config] unknown Agent backend '{}'", backendId.toStdString());
        return nullptr;
    }

    const GPlatform::AI::AgentDescriptor descriptor = backend->descriptor();
    const GPlatform::AI::AgentCapabilities capabilities = backend->capabilities();
    const bool supportsRequiredCapabilities =
        GPlatform::AI::hasCapability(capabilities,
                                     GPlatform::AI::AgentCapability::ToolCalls) &&
        GPlatform::AI::hasCapability(capabilities,
                                     GPlatform::AI::AgentCapability::ToolResults) &&
        GPlatform::AI::hasCapability(capabilities,
                                     GPlatform::AI::AgentCapability::Cancellation) &&
        GPlatform::AI::hasCapability(capabilities,
                                     GPlatform::AI::AgentCapability::SessionReset);
    if (!supportsRequiredCapabilities) {
        LOG_ERROR("[AI Config] Agent backend '{}' lacks required chat capabilities",
                  descriptor.id);
        return nullptr;
    }
    LOG_INFO("[AI Config] selected Agent backend '{}' ({})",
             descriptor.id,
             descriptor.displayName);
    return backend;
}

std::vector<GPlatform::AI::AgentToolDefinition>
AIChatBridge::buildToolDefinitions() const {
    std::vector<GPlatform::AI::AgentToolDefinition> tools;
    auto appendTool = [&tools](const char* name,
                               const char* description,
                               const QJsonObject& schema) {
        const QByteArray schemaBytes =
            QJsonDocument(schema).toJson(QJsonDocument::Compact);
        tools.push_back(GPlatform::AI::AgentToolDefinition{
            name,
            description,
            std::string(schemaBytes.constData(),
                        static_cast<std::size_t>(schemaBytes.size()))});
    };

    appendTool(
        "action_list",
        "List all AI-available actions and descriptors.",
        QJsonObject{{"type", "object"},
                    {"additionalProperties", false},
                    {"properties", QJsonObject{}}});

    appendTool(
        "action_describe",
        "Describe one action contract by actionCode.",
        QJsonObject{{"type", "object"},
                    {"additionalProperties", false},
                    {"required", QJsonArray{"actionCode"}},
                    {"properties",
                     QJsonObject{{"actionCode", QJsonObject{{"type", "string"}}}}}});

    appendTool(
        "action_invoke",
        "Invoke one action with params. You MUST have called "
        "action_describe(actionCode) in the current turn or a previous turn "
        "visible in conversation history before invoking. ALL action parameters "
        "MUST be placed inside the params object.",
        QJsonObject{
            {"type", "object"},
            {"additionalProperties", false},
            {"required", QJsonArray{"actionCode", "params"}},
            {"properties",
             QJsonObject{
                 {"actionCode", QJsonObject{{"type", "string"}}},
                 {"params",
                  QJsonObject{
                      {"type", "object"},
                      {"additionalProperties", true},
                      {"description",
                       "All business parameters for the action, as defined by "
                       "action_describe. Use an empty object when no params are needed."}}}}}});

    appendTool(
        "db_query",
        "Query DB objects and return direct property dictionary snapshots.",
        QJsonObject{
            {"type", "object"},
            {"additionalProperties", false},
            {"properties",
             QJsonObject{{"objectId", QJsonObject{{"type", "number"}}},
                         {"typeId", QJsonObject{{"type", "number"}}},
                         {"typeName", QJsonObject{{"type", "string"}}},
                         {"nameContains", QJsonObject{{"type", "string"}}},
                         {"limit", QJsonObject{{"type", "number"}}}}}});

    appendTool(
        "db_patch",
        "Patch DB object FIELD values by fieldName-to-value mapping.",
        QJsonObject{
            {"type", "object"},
            {"additionalProperties", false},
            {"required", QJsonArray{"objectId", "changes"}},
            {"properties",
             QJsonObject{
                 {"objectId", QJsonObject{{"type", "number"}}},
                 {"changes",
                  QJsonObject{{"type", "object"}, {"additionalProperties", true}}}}}});

    return tools;
}

QString AIChatBridge::buildSystemPrompt() const {
    const QString snapshotJson = QString::fromUtf8(
        QJsonDocument::fromVariant(buildCapabilitySnapshot()).toJson(QJsonDocument::Compact));

    // The prompt is required configuration. Invalid input must stop the turn.
    QFile promptFile(":/qt/qml/GPlatform/config/ai_system_prompt.json");
    if (promptFile.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = promptFile.readAll();
        promptFile.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QVariantMap root = doc.object().toVariantMap();

            QString prompt = root.value("identity").toString() + "\n\n";

            const QVariantList rules = root.value("rules").toList();
            for (const QVariant& sectionVar : rules) {
                const QVariantMap section = sectionVar.toMap();
                prompt += "## " + section.value("section").toString() + "\n";
                const QVariantList items = section.value("items").toList();
                for (int i = 0; i < items.size(); ++i) {
                    prompt += QString::number(i + 1) + ") " + items[i].toString() + "\n";
                }
                prompt += "\n";
            }

            prompt += "## Runtime Capability Index\n";
            prompt += root.value("capability_index_intro").toString() + "\n";
            prompt += snapshotJson;
            return prompt;
        }

        LOG_ERROR("[AI Prompt] invalid JSON in ai_system_prompt.json: {}",
                  parseError.errorString().toStdString());
    } else {
        LOG_ERROR("[AI Prompt] failed to open ai_system_prompt.json");
    }
    return {};
}

QVariantMap AIChatBridge::buildCapabilitySnapshot() const {
    QVariantMap snapshot;
    snapshot.insert("source", "runtime");
    // Lightweight but discriminative index for model-side pre-selection.
    // Full schema is available via action_describe(actionCode).
    snapshot.insert("actions", ActionManager::getInstance()->listActionSummariesForAI());

    // Parametric geometry: expose available operator names only.
    // Full operator schema is available via the parametric capability query tool.
    QVariantList parametricOps;
    const auto capabilityResult = GPlatform::Parametric::GeometryCommandBridge::instance().executeQuery(
        GPlatform::Parametric::CommandNames::capabilityList());
    if (capabilityResult.success) {
        const QVariant delta = capabilityResult.delta;
        if (delta.canConvert<QVariantList>()) {
            for (const QVariant& entry : delta.toList()) {
                const QVariantMap m = entry.toMap();
                const QString opName = m.value("name").toString();
                if (!opName.isEmpty()) {
                    parametricOps.push_back(opName);
                }
            }
        } else if (delta.canConvert<QVariantMap>()) {
            const QVariantMap deltaMap = delta.toMap();
            for (auto it = deltaMap.constBegin(); it != deltaMap.constEnd(); ++it) {
                if (!it.key().isEmpty()) {
                    parametricOps.push_back(it.key());
                }
            }
        }
    }
    snapshot.insert("parametricOps", parametricOps);

    if (!m_lastActionExecution.isEmpty()) {
        snapshot.insert("lastActionExecution", m_lastActionExecution);
    }
    return snapshot;
}

QString AIChatBridge::latestUserMessage() const {
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        const QVariantMap message = m_messages.at(i).toMap();
        if (message.value("role").toString() != "user") {
            continue;
        }
        const QString text = message.value("text").toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}
