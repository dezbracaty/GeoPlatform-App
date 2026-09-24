#pragma once

#include "StandardActionHandler.hpp"
#include "PickTypes.hpp"

#include <CutPlaneWidgetDB.hpp>
#include <CutPreviewDB.hpp>
#include <TempDBScope.hpp>

#include <QPoint>

#include <cstdint>
#include <memory>
#include <optional>

class ModelInstanceDB;

class CutToolHandler final : public StandardActionHandler {
    Q_OBJECT

public:
    explicit CutToolHandler(QObject* parent = nullptr);
    ~CutToolHandler() override = default;

    bool isPersistent() const override { return true; }
    bool supportsEnvironment(const QString& environment) const override;

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    void onSuspend() override { onExit(); }

    bool onMousePressEvent(QMouseEvent* event) override;
    bool onMouseMoveEvent(QMouseEvent* event) override;
    bool onMouseReleaseEvent(QMouseEvent* event) override;

private:
    struct CutOptions {
        bool keepUpper{true};
        bool keepLower{true};
        bool placeUpperOnCut{false};
        bool placeLowerOnCut{false};
        bool flipUpper{false};
        bool flipLower{false};
        bool cutToParts{false};
    };

    struct DragState {
        bool active{false};
        CutPlaneWidgetDB::Part part{CutPlaneWidgetDB::Part::None};
        Transform startPlaneToWorld;
        Vector3 centerWorld;
        Vector3 axisWorld;
        Vector3 startRotationVectorWorld;
        float startAxisParameter{0.0f};
    };

    bool createSession(const std::shared_ptr<ModelInstanceDB>& model);
    bool startDrag(CutPlaneWidgetDB::Part part, const QPoint& mousePosition);
    void updateDrag(const QPoint& mousePosition, Qt::KeyboardModifiers modifiers);
    void finishDrag();
    void updateHover(const PickResult& result);
    void publishPlane(const Transform& planeToWorld);
    void flipPlane();
    void resetPlane();
    void setPositionZ(double positionZ);
    void projectPositionZ();
    void projectBuildVolume();
    void projectOptions();
    void setKeepUpper(bool value);
    void setKeepLower(bool value);
    void setPlaceUpperOnCut(bool value);
    void setPlaceLowerOnCut(bool value);
    void setFlipUpper(bool value);
    void setFlipLower(bool value);
    void setCutToParts(bool value);
    void performCut();

    std::optional<float> axisParameterFromMouse(
        const QPoint& mousePosition,
        const Vector3& axisOrigin,
        const Vector3& axisDirection) const;
    std::optional<Vector3> rotationVectorFromMouse(
        const QPoint& mousePosition,
        const Vector3& centerWorld,
        const Vector3& axisWorld) const;

    TempDBScope m_tempScope;
    std::shared_ptr<CutPlaneWidgetDB> m_widget;
    std::shared_ptr<CutPreviewDB> m_preview;
    DBInstanceID m_targetModelId{INVALID_DB_ID};
    DBInstanceID m_interactionViewId{INVALID_DB_ID};
    Transform m_initialPlaneToWorld;
    CutOptions m_options;
    DragState m_drag;
    bool m_cutInProgress{false};
    std::uint64_t m_sessionRevision{0};
    bool m_modelInputSuspended{false};
};
