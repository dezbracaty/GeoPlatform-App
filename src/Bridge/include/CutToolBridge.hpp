#pragma once

#include "BridgeBase.hpp"

#include <QJSEngine>
#include <QQmlEngine>
#include <QString>
#include <qqmlregistration.h>

class CutToolBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool planeModified READ planeModified NOTIFY planeModifiedChanged)
    Q_PROPERTY(bool keepUpper READ keepUpper NOTIFY optionsChanged)
    Q_PROPERTY(bool keepLower READ keepLower NOTIFY optionsChanged)
    Q_PROPERTY(bool placeUpperOnCut READ placeUpperOnCut NOTIFY optionsChanged)
    Q_PROPERTY(bool placeLowerOnCut READ placeLowerOnCut NOTIFY optionsChanged)
    Q_PROPERTY(bool flipUpper READ flipUpper NOTIFY optionsChanged)
    Q_PROPERTY(bool flipLower READ flipLower NOTIFY optionsChanged)
    Q_PROPERTY(bool cutToParts READ cutToParts NOTIFY optionsChanged)
    Q_PROPERTY(bool canPerformCut READ canPerformCut NOTIFY optionsChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(double positionZ READ positionZ NOTIFY positionZChanged)
    Q_PROPERTY(QString buildVolumeText READ buildVolumeText
               NOTIFY buildVolumeTextChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    static CutToolBridge* instance();
    static CutToolBridge* create(QQmlEngine*, QJSEngine*);

    bool active() const noexcept { return m_active; }
    bool busy() const noexcept { return m_busy; }
    bool planeModified() const noexcept { return m_planeModified; }
    bool keepUpper() const noexcept { return m_keepUpper; }
    bool keepLower() const noexcept { return m_keepLower; }
    bool placeUpperOnCut() const noexcept { return m_placeUpperOnCut; }
    bool placeLowerOnCut() const noexcept { return m_placeLowerOnCut; }
    bool flipUpper() const noexcept { return m_flipUpper; }
    bool flipLower() const noexcept { return m_flipLower; }
    bool cutToParts() const noexcept { return m_cutToParts; }
    bool canPerformCut() const noexcept {
        return m_active && !m_busy && (m_keepUpper || m_keepLower);
    }
    const QString& errorText() const noexcept { return m_errorText; }
    double positionZ() const noexcept { return m_positionZ; }
    const QString& buildVolumeText() const noexcept {
        return m_buildVolumeText;
    }

    Q_INVOKABLE bool enterSelectedModel();
    Q_INVOKABLE void requestPositionZ(double positionZ);
    Q_INVOKABLE void flipPlane();
    Q_INVOKABLE void resetPlane();
    Q_INVOKABLE void setKeepUpper(bool value);
    Q_INVOKABLE void setKeepLower(bool value);
    Q_INVOKABLE void setPlaceUpperOnCut(bool value);
    Q_INVOKABLE void setPlaceLowerOnCut(bool value);
    Q_INVOKABLE void setFlipUpper(bool value);
    Q_INVOKABLE void setFlipLower(bool value);
    Q_INVOKABLE void setCutToParts(bool value);
    Q_INVOKABLE void performCut();
    Q_INVOKABLE void cancel();

    void projectSession(bool active, double positionZ = 0.0,
                        QString buildVolumeText = {});
    void projectPlaneState(double positionZ, bool planeModified);
    void projectOptions(bool keepUpper, bool keepLower,
                        bool placeUpperOnCut, bool placeLowerOnCut,
                        bool flipUpper, bool flipLower, bool cutToParts);
    void projectBuildVolume(QString buildVolumeText);
    void projectBusy(bool busy);
    void projectError(QString errorText);

signals:
    void activeChanged();
    void busyChanged();
    void planeModifiedChanged();
    void optionsChanged();
    void errorTextChanged();
    void positionZChanged();
    void buildVolumeTextChanged();
    void positionZRequested(double positionZ);
    void flipRequested();
    void resetRequested();
    void keepUpperRequested(bool value);
    void keepLowerRequested(bool value);
    void placeUpperOnCutRequested(bool value);
    void placeLowerOnCutRequested(bool value);
    void flipUpperRequested(bool value);
    void flipLowerRequested(bool value);
    void cutToPartsRequested(bool value);
    void performRequested();
    void cancelRequested();

private:
    explicit CutToolBridge(QObject* parent = nullptr);

    bool m_active{false};
    bool m_busy{false};
    bool m_planeModified{false};
    bool m_keepUpper{true};
    bool m_keepLower{true};
    bool m_placeUpperOnCut{false};
    bool m_placeLowerOnCut{false};
    bool m_flipUpper{false};
    bool m_flipLower{false};
    bool m_cutToParts{false};
    double m_positionZ{0.0};
    QString m_buildVolumeText;
    QString m_errorText;
};
