#pragma once

#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <QHash>

/**
 * @brief AI Action Descriptor 构建辅助工具
 *
 * 使用示例：
 *
 * // 简单 action
 * {{
 *     "selection.clear", makeDescriptor(
 *         "selection.clear",
 *         "Clear Selection",
 *         "Clear all currently selected models.",
 *         "write"
 *     )
 * }}
 *
 * // 带输入参数
 * {{
 *     "selection.pick", makeDescriptor(
 *         "selection.pick",
 *         "Select Model",
 *         "Select one or more models by db id.",
 *         "write",
 *         {
 *             {"modelId", "number", false},
 *             {"addToSelection", "bool", false},
 *         }
 *     )
 * }}
 *
 * // 带 tags
 * {{
 *     "custom.action", makeDescriptor(
 *         "custom.action",
 *         "Custom Action",
 *         "Description",
 *         "write",
 *         {},  // 无输入
 *         {"tag1", "tag2"}
 *     )
 * }}
 *
 * // 自定义 schema（用于复杂场景）
 * {{
 *     "complex.action", makeDescriptor(
 *         "complex.action",
 *         "Complex Action",
 *         "Description",
 *         "write",
 *         customSchema()
 *     )
 * }}
 */

/**
 * @brief 构建输入 schema 的字段定义
 */
struct SchemaField {
    const char* name;
    const char* type;       // "string", "number", "bool", "array", "object"
    bool required;
    QVariant defaultValue;  // 可选默认值
    QVariantList enumValues; // 可选枚举值

    // 支持所有 5 个参数的构造函数（用于 initializer_list 初始化）
    SchemaField(const char* n, const char* t, bool req = false,
                QVariant def = QVariant(), QVariantList en = QVariantList())
        : name(n), type(t), required(req), defaultValue(def), enumValues(en) {}
};

inline QVariantMap makeInputSchema(std::initializer_list<SchemaField> fields,
                                   bool allowUnknown = false) {
    QVariantMap schema;
    schema["allowUnknownParams"] = allowUnknown;

    QVariantMap properties;
    QVariantList requiredList;

    for (const auto& field : fields) {
        QVariantMap prop;
        prop["type"] = field.type;
        if (field.defaultValue.isValid()) {
            prop["default"] = field.defaultValue;
        }
        if (!field.enumValues.isEmpty()) {
            prop["enum"] = field.enumValues;
        }
        properties[field.name] = prop;

        if (field.required) {
            requiredList.append(field.name);
        }
    }

    schema["properties"] = properties;
    if (!requiredList.isEmpty()) {
        schema["required"] = requiredList;
    }

    return schema;
}

// ============ makeDescriptor 重载 ============

// 基本 descriptor（const char* 版本）
inline QVariantMap makeDescriptor(const char* actionCode,
                                   const char* title,
                                   const char* description,
                                   const char* sideEffectLevel,
                                   const QVariantMap& inputSchema) {
    QVariantMap desc;
    desc["actionCode"] = actionCode;
    desc["title"] = title;
    desc["description"] = description;
    desc["sideEffectLevel"] = sideEffectLevel;
    desc["async"] = false;
    desc["supportsDryRun"] = false;
    desc["idempotent"] = false;
    desc["inputSchema"] = inputSchema;
    return desc;
}

// 基本 descriptor（QString 版本）
inline QVariantMap makeDescriptor(const QString& actionCode,
                                   const QString& title,
                                   const QString& description,
                                   const QString& sideEffectLevel,
                                   const QVariantMap& inputSchema) {
    QVariantMap desc;
    desc["actionCode"] = actionCode;
    desc["title"] = title;
    desc["description"] = description;
    desc["sideEffectLevel"] = sideEffectLevel;
    desc["async"] = false;
    desc["supportsDryRun"] = false;
    desc["idempotent"] = false;
    desc["inputSchema"] = inputSchema;
    return desc;
}

// 带 tags 的 descriptor（const char* 版本）
inline QVariantMap makeDescriptor(const char* actionCode,
                                   const char* title,
                                   const char* description,
                                   const char* sideEffectLevel,
                                   const QVariantMap& inputSchema,
                                   std::initializer_list<const char*> tags) {
    QVariantMap desc = makeDescriptor(actionCode, title, description, sideEffectLevel, inputSchema);
    QVariantList tagList;
    for (const char* tag : tags) {
        tagList.append(tag);
    }
    desc["tags"] = tagList;
    return desc;
}

// 带 tags 的 descriptor（QString 版本）
inline QVariantMap makeDescriptor(const QString& actionCode,
                                   const QString& title,
                                   const QString& description,
                                   const QString& sideEffectLevel,
                                   const QVariantMap& inputSchema,
                                   std::initializer_list<const char*> tags) {
    QVariantMap desc = makeDescriptor(actionCode, title, description, sideEffectLevel, inputSchema);
    QVariantList tagList;
    for (const char* tag : tags) {
        tagList.append(tag);
    }
    desc["tags"] = tagList;
    return desc;
}

// SchemaField 版本（无 tags）
inline QVariantMap makeDescriptor(const char* actionCode,
                                   const char* title,
                                   const char* description,
                                   const char* sideEffectLevel,
                                   std::initializer_list<SchemaField> fields,
                                   bool allowUnknown = false) {
    return makeDescriptor(actionCode, title, description, sideEffectLevel,
                         makeInputSchema(fields, allowUnknown));
}

// SchemaField 版本（QString，无 tags）
inline QVariantMap makeDescriptor(const QString& actionCode,
                                   const QString& title,
                                   const QString& description,
                                   const QString& sideEffectLevel,
                                   std::initializer_list<SchemaField> fields,
                                   bool allowUnknown = false) {
    return makeDescriptor(actionCode, title, description, sideEffectLevel,
                         makeInputSchema(fields, allowUnknown));
}

// SchemaField 版本（带 tags）
inline QVariantMap makeDescriptor(const char* actionCode,
                                   const char* title,
                                   const char* description,
                                   const char* sideEffectLevel,
                                   std::initializer_list<SchemaField> fields,
                                   std::initializer_list<const char*> tags,
                                   bool allowUnknown = false) {
    auto desc = makeDescriptor(actionCode, title, description, sideEffectLevel,
                               makeInputSchema(fields, allowUnknown));
    QVariantList tagList;
    for (const char* tag : tags) {
        tagList.append(tag);
    }
    desc["tags"] = tagList;
    return desc;
}

// SchemaField 版本（QString，带 tags）
inline QVariantMap makeDescriptor(const QString& actionCode,
                                   const QString& title,
                                   const QString& description,
                                   const QString& sideEffectLevel,
                                   std::initializer_list<SchemaField> fields,
                                   std::initializer_list<const char*> tags,
                                   bool allowUnknown = false) {
    auto desc = makeDescriptor(actionCode, title, description, sideEffectLevel,
                               makeInputSchema(fields, allowUnknown));
    QVariantList tagList;
    for (const char* tag : tags) {
        tagList.append(tag);
    }
    desc["tags"] = tagList;
    return desc;
}
