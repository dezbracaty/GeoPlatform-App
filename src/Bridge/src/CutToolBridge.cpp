#include "CutToolBridge.hpp"

#include "BridgeRegistration.hpp"

#include <ActionManager.hpp>
#include <SelectionBridge.hpp>

#include <utility>

CutToolBridge::CutToolBridge(QObject* parent)
    : bridge::BridgeBase(parent) {
}

CutToolBridge* CutToolBridge::instance() {
    static auto* bridge = new CutToolBridge();
    return bridge;
}

CutToolBridge* CutToolBridge::create(QQmlEngine*, QJSEngine*) {
    auto* bridge = instance();
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    return bridge;
}

bool CutToolBridge::enterSelectedModel() {
    const DBInstanceID modelId = SelectionBridge::instance()->selectedId();
    if (!modelId.isValid()) return false;
    return ActionManager::getInstance()->triggerAction(
        QStringLiteral("model.cut"),
        {{QStringLiteral("modelId"),
          QVariant::fromValue<qulonglong>(modelId.getValue())}});
}

void CutToolBridge::requestPositionZ(double positionZ) {
    emit positionZRequested(positionZ);
}

void CutToolBridge::flipPlane() {
    emit flipRequested();
}

void CutToolBridge::resetPlane() {
    emit resetRequested();
}

void CutToolBridge::setKeepUpper(bool value) {
    emit keepUpperRequested(value);
}

void CutToolBridge::setKeepLower(bool value) {
    emit keepLowerRequested(value);
}

void CutToolBridge::setPlaceUpperOnCut(bool value) {
    emit placeUpperOnCutRequested(value);
}

void CutToolBridge::setPlaceLowerOnCut(bool value) {
    emit placeLowerOnCutRequested(value);
}

void CutToolBridge::setFlipUpper(bool value) {
    emit flipUpperRequested(value);
}

void CutToolBridge::setFlipLower(bool value) {
    emit flipLowerRequested(value);
}

void CutToolBridge::setCutToParts(bool value) {
    emit cutToPartsRequested(value);
}

void CutToolBridge::performCut() {
    emit performRequested();
}

void CutToolBridge::cancel() {
    emit cancelRequested();
}

void CutToolBridge::projectSession(bool active, double positionZ,
                                   QString buildVolumeText) {
    const bool activeChangedValue = m_active != active;
    m_active = active;
    projectPlaneState(positionZ, false);
    if (!active) {
        projectBusy(false);
        projectError({});
    }
    projectBuildVolume(std::move(buildVolumeText));
    if (activeChangedValue) {
        emit activeChanged();
        emit optionsChanged();
    }
}

void CutToolBridge::projectOptions(
    bool keepUpper, bool keepLower,
    bool placeUpperOnCut, bool placeLowerOnCut,
    bool flipUpper, bool flipLower, bool cutToParts) {
    const bool changed =
        m_keepUpper != keepUpper || m_keepLower != keepLower ||
        m_placeUpperOnCut != placeUpperOnCut ||
        m_placeLowerOnCut != placeLowerOnCut ||
        m_flipUpper != flipUpper || m_flipLower != flipLower ||
        m_cutToParts != cutToParts;
    m_keepUpper = keepUpper;
    m_keepLower = keepLower;
    m_placeUpperOnCut = placeUpperOnCut;
    m_placeLowerOnCut = placeLowerOnCut;
    m_flipUpper = flipUpper;
    m_flipLower = flipLower;
    m_cutToParts = cutToParts;
    if (changed) emit optionsChanged();
}

void CutToolBridge::projectBuildVolume(QString buildVolumeText) {
    if (m_buildVolumeText == buildVolumeText) return;
    m_buildVolumeText = std::move(buildVolumeText);
    emit buildVolumeTextChanged();
}

void CutToolBridge::projectBusy(bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
    emit optionsChanged();
}

void CutToolBridge::projectError(QString errorText) {
    if (m_errorText == errorText) return;
    m_errorText = std::move(errorText);
    emit errorTextChanged();
}

void CutToolBridge::projectPlaneState(double positionZ, bool planeModified) {
    if (!qFuzzyCompare(m_positionZ, positionZ)) {
        m_positionZ = positionZ;
        emit positionZChanged();
    }
    if (m_planeModified != planeModified) {
        m_planeModified = planeModified;
        emit planeModifiedChanged();
    }
}

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    CutToolBridge, "CutToolBridge", &CutToolBridge::create)
