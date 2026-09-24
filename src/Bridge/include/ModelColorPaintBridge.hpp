#pragma once

#include "BridgeBase.hpp"
#include <QJSEngine>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlregistration.h>

class ModelColorPaintBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool finishing READ finishing NOTIFY commitStateChanged)
    Q_PROPERTY(bool commitPending READ commitPending NOTIFY commitStateChanged)
    Q_PROPERTY(int currentColorIndex READ currentColorIndex WRITE setCurrentColorIndex NOTIFY settingsChanged)
    Q_PROPERTY(double brushSize READ brushSize WRITE setBrushSize NOTIFY settingsChanged)
    // toolType is the semantic API. brushShape remains as a compatibility alias
    // for existing actions and startup validation.
    Q_PROPERTY(QString toolType READ brushShape WRITE setBrushShape NOTIFY settingsChanged)
    Q_PROPERTY(QString brushShape READ brushShape WRITE setBrushShape NOTIFY settingsChanged)
    Q_PROPERTY(bool edgeDetection READ edgeDetection WRITE setEdgeDetection NOTIFY settingsChanged)
    Q_PROPERTY(double smartFillAngle READ smartFillAngle WRITE setSmartFillAngle NOTIFY settingsChanged)
    Q_PROPERTY(double heightRange READ heightRange WRITE setHeightRange NOTIFY settingsChanged)
    Q_PROPERTY(double gapArea READ gapArea WRITE setGapArea NOTIFY settingsChanged)
    Q_PROPERTY(bool gapPreviewPending READ gapPreviewPending NOTIFY gapPreviewChanged)
    Q_PROPERTY(int gapCandidateCount READ gapCandidateCount NOTIFY gapPreviewChanged)
    Q_PROPERTY(bool verticalOnly READ verticalOnly WRITE setVerticalOnly NOTIFY settingsChanged)
    Q_PROPERTY(bool horizontalOnly READ horizontalOnly WRITE setHorizontalOnly NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList filamentSlots READ filamentSlots NOTIFY filamentSlotsChanged)
    Q_PROPERTY(int defaultFilamentSlot READ defaultFilamentSlot WRITE setDefaultFilamentSlot
               NOTIFY defaultFilamentSlotChanged)
    QML_ELEMENT
    QML_SINGLETON

public:
    static ModelColorPaintBridge* instance();
    static ModelColorPaintBridge* create(QQmlEngine*, QJSEngine*);

    bool active() const { return m_active; }
    bool finishing() const { return m_finishing; }
    bool commitPending() const { return m_commitPending; }
    int currentColorIndex() const { return m_currentColorIndex; }
    double brushSize() const { return m_brushSize; }
    QString brushShape() const { return m_brushShape; }
    bool edgeDetection() const { return m_edgeDetection; }
    double smartFillAngle() const { return m_smartFillAngle; }
    double heightRange() const { return m_heightRange; }
    double gapArea() const { return m_gapArea; }
    bool gapPreviewPending() const { return m_gapPreviewPending; }
    int gapCandidateCount() const { return m_gapCandidateCount; }
    bool verticalOnly() const { return m_verticalOnly; }
    bool horizontalOnly() const { return m_horizontalOnly; }
    QVariantList palette() const { return m_palette; }
    QVariantList filamentSlots() const;
    int defaultFilamentSlot() const { return m_defaultFilamentSlot; }

    void setActive(bool active);
    void setCommitState(bool finishing, bool pending);
    void setCurrentColorIndex(int index);
    void setBrushSize(double size);
    void setBrushShape(const QString& shape);
    void setEdgeDetection(bool enabled);
    void setSmartFillAngle(double angle);
    void setHeightRange(double height);
    void setGapArea(double area);
    void setGapPreviewStatus(bool pending, int candidateCount);
    void setVerticalOnly(bool enabled);
    void setHorizontalOnly(bool enabled);
    void setPalette(const QVariantList& palette);
    void setDefaultFilamentSlot(int slot);
    void setDefaultFilamentSlotProjection(int slot);

    Q_INVOKABLE bool enterSelectedModel();
    Q_INVOKABLE bool leavePainting();
    Q_INVOKABLE bool applySettings(const QVariantMap& settings);
    Q_INVOKABLE bool setPaletteColor(int index, const QString& color);
    Q_INVOKABLE bool clearPainting();
    Q_INVOKABLE bool performGapFill();
    Q_INVOKABLE bool remapFilaments(const QVariantList& mapping);

signals:
    void activeChanged();
    void settingsChanged();
    void paletteChanged();
    void filamentSlotsChanged();
    void defaultFilamentSlotChanged();
    void gapPreviewChanged();
    void commitStateChanged();
    void finishRequested();
    void applySettingsRequested(const QVariantMap& settings);
    void clearPaintingRequested();
    void performGapFillRequested();
    void remapFilamentsRequested(const QVariantList& mapping);
    void defaultFilamentSlotRequested(int slot);

private:
    explicit ModelColorPaintBridge(QObject* parent = nullptr);
    void syncPaletteFromFilamentSlots();

    bool m_active{false};
    bool m_finishing{false};
    bool m_commitPending{false};
    int m_currentColorIndex{1};
    double m_brushSize{8.0};
    QString m_brushShape{QStringLiteral("circle")};
    bool m_edgeDetection{true};
    double m_smartFillAngle{30.0};
    double m_heightRange{0.2};
    double m_gapArea{5.0};
    bool m_gapPreviewPending{false};
    int m_gapCandidateCount{0};
    bool m_verticalOnly{false};
    bool m_horizontalOnly{false};
    QVariantList m_palette;
    int m_defaultFilamentSlot{1};
};
