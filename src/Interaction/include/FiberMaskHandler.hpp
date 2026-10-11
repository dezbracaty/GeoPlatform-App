#pragma once
#include "StandardActionHandler.hpp"
#include "SnapService.hpp"
#include "SnapPreviewHandler.hpp"
#include <FiberMaskDB.hpp>
#include <vtkSmartPointer.h>
#include <TempDBScope.hpp>
#include <DocumentManager.hpp>
#include <libslicer/Config.hpp>
#include <QVariantMap>
#include <map>

class ModelInstanceDB;
class TransientPolyDataActorDB;
class vtkPolyData;
class FiberMaskHandler final : public StandardActionHandler {
    Q_OBJECT
public:
    explicit FiberMaskHandler(QObject* parent = nullptr);
    ~FiberMaskHandler() override;
    bool isPersistent() const override {
        return true;
    }
    bool supportsEnvironment(const QString& environment) const override;
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    bool prepareForAction(const QString& actionCode, HandlerType incomingType) override;
    void onSuspend() override {
        onExit();
    }
    bool onMousePressEvent(QMouseEvent*) override;
    bool onMouseMoveEvent(QMouseEvent*) override;
    bool onMouseReleaseEvent(QMouseEvent*) override;
    bool onKeyPressEvent(QKeyEvent*) override;
    bool onKeyReleaseEvent(QKeyEvent*) override;

private:
    enum class Stage { Place, Footprint, Depth, Review };
    FiberMaskDB::Region region() const;
    void updateRegion(const std::function<void(FiberMaskDB::Region&)>& update);
    void updatePointerScope();
    bool objectDragActive() const;
    std::shared_ptr<ModelInstanceDB> model() const;
    void request(const QString& operation, const QVariantMap& values);
    void startNew(int shape, bool copy = false);
    bool selectMask(DBInstanceID id, bool reload = false);
    bool savePendingEdit();
    bool hasPendingEdit() const;
    void publish();
    QString validationError() const;
    void rebuildPreview();
    void updateReticle(std::optional<Vector3> rawWorld = std::nullopt);
    QVariantMap sectionPreview();
    bool commit();
    bool finishEdit();
    void loadConfig();
    void updateParameter(const QVariantMap& values);
    QVariantList parameterProjection() const;
    QString parameterError() const;
    Vector3 snap(Vector3 modelPoint, const GPlatform::Interaction::SnapConstraint&,
                 Qt::KeyboardModifiers);
    std::optional<Vector3> planePoint(const QPoint&);
    std::optional<Vector3> placementPoint(const QPoint&, Qt::KeyboardModifiers);
    void refreshPointer(Qt::KeyboardModifiers);
    std::optional<float> axisParameter(const QPoint&, Vector3 origin, Vector3 axis);
    void updatePointer(const QPoint&, Qt::KeyboardModifiers);
    void updateFromFootprint(Vector3 end);
    void applyDimensions(Vector3 dimensions);
    void updateDepth(float signedDepth);
    void rebuildSnapTargets();
    void orientTopView();
    ViewportProjectionSnapshot viewProjection() const;
    float handleLength() const;
    void synchronizePreviews(bool synchronizeAll = false);
    struct Draft {
        FiberMaskDB::Region region;
        Transform::Matrix4 ownerWorld{Transform::Matrix4::Identity()};
        std::string parameterOverrides{"{}"};
        QString name;
        bool enabled{true};
    };
    void loadDraft(const FiberMaskDB& source);
    std::shared_ptr<TransientPolyDataActorDB> createPreview();
    void loadPreview(const std::shared_ptr<TransientPolyDataActorDB>& actor,
                     const FiberMaskDB& source);
    void setPreviewGeometry(const std::shared_ptr<TransientPolyDataActorDB>& actor,
                            const FiberMaskDB::Region& region, Vector3 color);
    Draft m_draft;
    std::shared_ptr<TransientPolyDataActorDB> m_preview;
    std::map<DBInstanceID, std::shared_ptr<TransientPolyDataActorDB>> m_previews;
    struct PreviewGeometry {
        FiberMaskDB::Shape shape;
        Vector3 dimensions, color;
    };
    std::map<DBInstanceID, PreviewGeometry> m_previewGeometries;
    Stage m_stage{Stage::Place};
    DBInstanceID m_modelId{}, m_editId{}, m_viewId{}, m_cameraGestureView{};
    DocumentManager::ListenerID m_listener{};
    TempDBScope m_scope;
    std::shared_ptr<TransientPolyDataActorDB> m_handle, m_reticle;
    GPlatform::Interaction::SnapPreviewHandler m_snapPreview;
    GPlatform::Interaction::SnapService m_snapService;
    std::vector<GPlatform::Interaction::SnapTarget> m_targets;
    std::optional<GPlatform::Interaction::SnapResult> m_snapResult;
    std::optional<libslicer::Config> m_config;
    std::string m_savedParameterOverrides{"{}"};
    std::vector<libslicer::SettingItem> m_parameterItems;
    // Rejected input belongs to its field, independently of pointer/geometry errors.
    QVariantMap m_invalidParameterInputs;
    QMap<QString, QString> m_parameterErrors;
    Vector3 m_anchor{}, m_end{};
    Transform m_plane;
    QPoint m_pointer;
    float m_depth{0}, m_dragStartParameter{0}, m_dragStartDepth{0};
    int m_planeIndex{0}, m_dragAxis{-1};
    bool m_dragging{false}, m_copyPlacement{false}, m_committing{false}, m_presenting{false};
    bool m_showSection{false}, m_refreshPending{false};
    bool m_reloadSelected{false};
    bool m_drawingEnabled{false}, m_maskPressed{false};
    std::uint64_t m_sessionGeneration{0};
    double m_sectionZ{0};
    std::uint64_t m_sectionRevision{0};
    vtkSmartPointer<vtkPolyData> m_sectionModel;
    QString m_error;
};
