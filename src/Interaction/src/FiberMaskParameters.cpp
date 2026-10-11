#include "FiberMaskHandler.hpp"
#include <SliceSettingsBridge.hpp>
#include <SlicingBackend.hpp>
#include <algorithm>

void FiberMaskHandler::loadConfig() {
    const auto snapshot = GPlatform::SlicingBackend::instance().configSnapshot();
    m_config = libslicer::Config::for_overrides(
        snapshot ? *snapshot : libslicer::Config::defaults().snapshot(),
        libslicer::ConfigScope::FiberMask);
    const auto report = [&](const libslicer::SettingsResult& result) {
        for (const auto& diagnostic : result.diagnostics)
            m_parameterErrors[QString::fromStdString(diagnostic.key)] =
                QString::fromStdString(diagnostic.message);
    };
    const auto result = m_config->load_overrides(m_draft.parameterOverrides);
    if (!result) report(result);
    else m_draft.parameterOverrides = m_config->serialize_overrides();
    m_parameterItems = m_config->settings(libslicer::ConfigScope::FiberMask);
}
QVariantList FiberMaskHandler::parameterProjection() const {
    QVariantList parameters;
    for (const auto& item : m_parameterItems) {
        const QString key = QString::fromStdString(item.key);
        const auto schema = SliceSettingsBridge::instance()->getSettingSchema(key);
        QVariantList choices;
        for (const auto& choice : item.enum_items)
            choices.append(QVariantMap{{"value", QString::fromStdString(choice.value)},
                                       {"label", QString::fromStdString(choice.label)}});
        parameters.append(
            QVariantMap{{"key", key},
                        {"label", schema.value("label", QString::fromStdString(item.label))},
                        {"value", m_invalidParameterInputs.value(key, QString::fromStdString(item.value))},
                        {"error", m_parameterErrors.value(key)},
                        {"visible", item.visible},
                        {"enabled", item.enabled},
                        {"overridden", item.overridden},
                        {"boolean", item.type == libslicer::SettingType::Boolean},
                        {"choices", choices},
                        {"unit", QString::fromStdString(item.unit)}});
    }
    return parameters;
}
QString FiberMaskHandler::parameterError() const {
    return m_parameterErrors.isEmpty() ? QString() : m_parameterErrors.first();
}
void FiberMaskHandler::updateParameter(const QVariantMap& values) {
    const auto key = values.value("key").toString();
    if (std::none_of(m_parameterItems.begin(), m_parameterItems.end(),
                     [&](const auto& item) { return item.key == key.toStdString(); }))
        return;
    auto candidate = *m_config;
    const auto value = values.value("value").toString();
    const auto result = values.value("inherit").toBool()
        ? candidate.reset(key.toStdString())
        : candidate.set(libslicer::ConfigScope::FiberMask, key.toStdString(), value.toStdString());
    if (!result) {
        m_invalidParameterInputs[key] = value;
        m_parameterErrors[key] = result.diagnostics.empty()
            ? tr("Invalid fiber parameter.")
            : QString::fromStdString(result.diagnostics.front().message);
        return;
    }
    m_draft.parameterOverrides = candidate.serialize_overrides();
    m_invalidParameterInputs.remove(key);
    m_parameterErrors.remove(key);
    loadConfig();
}
