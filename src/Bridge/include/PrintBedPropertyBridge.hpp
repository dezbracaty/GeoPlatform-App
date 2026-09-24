#pragma once

#include <QObject>
#include <QProperty>
#include <QBindable>
#include <QString>
#include <QtQml/qqml.h>
#include <QVector3D>
#include <memory>
#include "PropertyBridgeBase.hpp"
#include "BaseID.hpp"
#include "PrintBedDB.hpp"

/**
 * @brief PrintBedPropertyBridge - QML 和 PrintBedDB 之间的属性桥梁
 *
 * 这个类暴露 PrintBedDB 的属性到 QML，允许用户在界面中调整：
 * - 打印床尺寸（宽度、深度、厚度）
 * - 网格显示（间距、颜色）
 * - 材质属性（颜色、金属度、粗糙度）
 * - 材质预设（镜面金属、半金属、塑料）
 *
 * 所有属性支持：
 * - Qt6 property binding（自动 UI 更新）
 * - Transaction 事务支持（撤销/重做）
 * - DocumentManager 通知（多向同步）
 */
class PrintBedPropertyBridge : public PropertyBridgeBase {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // === 打印床几何属性 ===
    Q_PROPERTY(float width READ width WRITE setWidth
               BINDABLE bindableWidth NOTIFY widthChanged)
    Q_PROPERTY(float height READ height WRITE setHeight
               BINDABLE bindableHeight NOTIFY heightChanged)
    Q_PROPERTY(float thickness READ thickness WRITE setThickness
               BINDABLE bindableThickness NOTIFY thicknessChanged)
    Q_PROPERTY(float printHeight READ printHeight WRITE setPrintHeight
               BINDABLE bindablePrintHeight NOTIFY printHeightChanged)
    Q_PROPERTY(QString bedSizeText READ bedSizeText NOTIFY bedSizeTextChanged)

    // === 网格显示属性 ===
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid
               BINDABLE bindableShowGrid NOTIFY showGridChanged)
    Q_PROPERTY(float gridSpacing READ gridSpacing WRITE setGridSpacing
               BINDABLE bindableGridSpacing NOTIFY gridSpacingChanged)
    Q_PROPERTY(QVector3D gridColor READ gridColor WRITE setGridColor
               BINDABLE bindableGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(bool showBounds READ showBounds WRITE setShowBounds
               BINDABLE bindableShowBounds NOTIFY showBoundsChanged)
    Q_PROPERTY(QVector3D boundColor READ boundColor WRITE setBoundColor
               BINDABLE bindableBoundColor NOTIFY boundColorChanged)

    // === 材质属性（来自 Material） ===
    Q_PROPERTY(QVector3D platformColor READ platformColor WRITE setPlatformColor
               BINDABLE bindablePlatformColor NOTIFY platformColorChanged)
    Q_PROPERTY(float metallic READ metallic WRITE setMetallic
               BINDABLE bindableMetallic NOTIFY metallicChanged)
    Q_PROPERTY(float roughness READ roughness WRITE setRoughness
               BINDABLE bindableRoughness NOTIFY roughnessChanged)
    Q_PROPERTY(float opacity READ opacity WRITE setOpacity
               BINDABLE bindableOpacity NOTIFY opacityChanged)

    // === 材质预设 ===
    Q_PROPERTY(int materialPreset READ materialPreset WRITE setMaterialPreset
               BINDABLE bindableMaterialPreset NOTIFY materialPresetChanged)

    // === 渲染模式 ===
    Q_PROPERTY(int renderingMode READ renderingMode WRITE setRenderingMode
               BINDABLE bindableRenderingMode NOTIFY renderingModeChanged)

    // === VTK 光照参数 ===
    Q_PROPERTY(float ambient READ ambient WRITE setAmbient
               BINDABLE bindableAmbient NOTIFY ambientChanged)
    Q_PROPERTY(float diffuse READ diffuse WRITE setDiffuse
               BINDABLE bindableDiffuse NOTIFY diffuseChanged)
    Q_PROPERTY(float specular READ specular WRITE setSpecular
               BINDABLE bindableSpecular NOTIFY specularChanged)
    Q_PROPERTY(float specularPower READ specularPower WRITE setSpecularPower
               BINDABLE bindableSpecularPower NOTIFY specularPowerChanged)

    // === Phong 颜色参数 ===
    Q_PROPERTY(QVector3D ambientColor READ ambientColor WRITE setAmbientColor
               BINDABLE bindableAmbientColor NOTIFY ambientColorChanged)
    Q_PROPERTY(QVector3D specularColor READ specularColor WRITE setSpecularColor
               BINDABLE bindableSpecularColor NOTIFY specularColorChanged)

    // === 渲染属性参数 ===
    Q_PROPERTY(bool edgeVisibility READ edgeVisibility WRITE setEdgeVisibility
               BINDABLE bindableEdgeVisibility NOTIFY edgeVisibilityChanged)
    Q_PROPERTY(QVector3D edgeColor READ edgeColor WRITE setEdgeColor
               BINDABLE bindableEdgeColor NOTIFY edgeColorChanged)
    Q_PROPERTY(float lineWidth READ lineWidth WRITE setLineWidth
               BINDABLE bindableLineWidth NOTIFY lineWidthChanged)

signals:
    // 几何属性变化信号
    void widthChanged();
    void heightChanged();
    void thicknessChanged();
    void printHeightChanged();
    void bedSizeTextChanged();

    // 网格显示属性变化信号
    void showGridChanged();
    void gridSpacingChanged();
    void gridColorChanged();
    void showBoundsChanged();
    void boundColorChanged();

    // 材质属性变化信号
    void platformColorChanged();
    void metallicChanged();
    void roughnessChanged();
    void opacityChanged();

    // 材质预设变化信号
    void materialPresetChanged();

    // 渲染模式变化信号
    void renderingModeChanged();

    // VTK 光照参数变化信号
    void ambientChanged();
    void diffuseChanged();
    void specularChanged();
    void specularPowerChanged();

    // Phong 颜色参数变化信号
    void ambientColorChanged();
    void specularColorChanged();

    // 渲染属性参数变化信号
    void edgeVisibilityChanged();
    void edgeColorChanged();
    void lineWidthChanged();

public:
    explicit PrintBedPropertyBridge(QObject* parent = nullptr);
    ~PrintBedPropertyBridge() override;

    static QObject* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    // === 几何属性 getters ===
    float width() const;
    float height() const;
    float thickness() const;
    float printHeight() const;
    QString bedSizeText() const;

    // === 网格显示属性 getters ===
    bool showGrid() const;
    float gridSpacing() const;
    QVector3D gridColor() const;
    bool showBounds() const;
    QVector3D boundColor() const;

    // === 材质属性 getters ===
    QVector3D platformColor() const;
    float metallic() const;
    float roughness() const;
    float opacity() const;

    // === 材质预设 getter ===
    int materialPreset() const;

    // === 渲染模式 getter ===
    int renderingMode() const;

    // === VTK 光照参数 getters ===
    float ambient() const;
    float diffuse() const;
    float specular() const;
    float specularPower() const;

    // === Phong 颜色参数 getters ===
    QVector3D ambientColor() const;
    QVector3D specularColor() const;

    // === 渲染属性参数 getters ===
    bool edgeVisibility() const;
    QVector3D edgeColor() const;
    float lineWidth() const;

    // === 几何属性 setters ===
    void setWidth(float width);
    void setHeight(float height);
    void setThickness(float thickness);
    void setPrintHeight(float printHeight);

    // === 网格显示属性 setters ===
    void setShowGrid(bool show);
    void setGridSpacing(float spacing);
    void setGridColor(const QVector3D& color);
    void setShowBounds(bool show);
    void setBoundColor(const QVector3D& color);

    // === 材质属性 setters ===
    void setPlatformColor(const QVector3D& color);
    void setMetallic(float metallic);
    void setRoughness(float roughness);
    void setOpacity(float opacity);

    // === 材质预设 setter ===
    void setMaterialPreset(int preset);

    // === 渲染模式 setter ===
    void setRenderingMode(int mode);

    // === VTK 光照参数 setters ===
    void setAmbient(float ambient);
    void setDiffuse(float diffuse);
    void setSpecular(float specular);
    void setSpecularPower(float power);

    // === Phong 颜色参数 setters ===
    void setAmbientColor(const QVector3D& color);
    void setSpecularColor(const QVector3D& color);

    // === 渲染属性参数 setters ===
    void setEdgeVisibility(bool visible);
    void setEdgeColor(const QVector3D& color);
    void setLineWidth(float width);

    // === Bindables ===
    QBindable<float> bindableWidth();
    QBindable<float> bindableHeight();
    QBindable<float> bindableThickness();
    QBindable<float> bindablePrintHeight();
    QBindable<bool> bindableShowGrid();
    QBindable<float> bindableGridSpacing();
    QBindable<QVector3D> bindableGridColor();
    QBindable<bool> bindableShowBounds();
    QBindable<QVector3D> bindableBoundColor();
    QBindable<QVector3D> bindablePlatformColor();
    QBindable<float> bindableMetallic();
    QBindable<float> bindableRoughness();
    QBindable<float> bindableOpacity();
    QBindable<int> bindableMaterialPreset();
    QBindable<int> bindableRenderingMode();
    QBindable<float> bindableAmbient();
    QBindable<float> bindableDiffuse();
    QBindable<float> bindableSpecular();
    QBindable<float> bindableSpecularPower();
    QBindable<QVector3D> bindableAmbientColor();
    QBindable<QVector3D> bindableSpecularColor();
    QBindable<bool> bindableEdgeVisibility();
    QBindable<QVector3D> bindableEdgeColor();
    QBindable<float> bindableLineWidth();

protected:
    // Override from PropertyBridgeBase
    void onPropertyChanged(const DBInstanceID& id,
                           const std::string& propertyName,
                           const std::any& newValue) override;
    void onObjectCreated(const DBInstanceID& id) override;
    void onObjectDeleted(const DBInstanceID& id) override;

private:
    /**
     * @brief 获取 PrintBedDB 实例
     * @return PrintBedDB 共享指针，如果不存在则返回 nullptr
     */
    std::shared_ptr<PrintBedDB> getPrintBedDB() const;

    /**
     * @brief 查找并监听 PrintBed 实例
     */
    void findAndListenToPrintBed();

    /**
     * @brief 从 PrintBedDB 更新所有属性
     */
    void updateAllProperties();

    /**
     * @brief 更新特定属性
     */
    void updateSpecificProperty(const std::string& propertyName);

    /**
     * @brief 清空所有属性（当 PrintBed 被删除时）
     */
    void clearProperties();

    // Qt6 bindable properties - 几何属性
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_width, &PrintBedPropertyBridge::widthChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_height, &PrintBedPropertyBridge::heightChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_thickness, &PrintBedPropertyBridge::thicknessChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_printHeight, &PrintBedPropertyBridge::printHeightChanged)

    // Qt6 bindable properties - 网格显示属性
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, bool, m_showGrid, &PrintBedPropertyBridge::showGridChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_gridSpacing, &PrintBedPropertyBridge::gridSpacingChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_gridColor, &PrintBedPropertyBridge::gridColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, bool, m_showBounds, &PrintBedPropertyBridge::showBoundsChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_boundColor, &PrintBedPropertyBridge::boundColorChanged)

    // Qt6 bindable properties - 材质属性
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_platformColor, &PrintBedPropertyBridge::platformColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_metallic, &PrintBedPropertyBridge::metallicChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_roughness, &PrintBedPropertyBridge::roughnessChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_opacity, &PrintBedPropertyBridge::opacityChanged)

    // Qt6 bindable properties - 材质预设
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, int, m_materialPreset, &PrintBedPropertyBridge::materialPresetChanged)

    // Qt6 bindable properties - 渲染模式
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, int, m_renderingMode, &PrintBedPropertyBridge::renderingModeChanged)

    // Qt6 bindable properties - VTK 光照参数
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_ambient, &PrintBedPropertyBridge::ambientChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_diffuse, &PrintBedPropertyBridge::diffuseChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_specular, &PrintBedPropertyBridge::specularChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_specularPower, &PrintBedPropertyBridge::specularPowerChanged)

    // Qt6 bindable properties - Phong 颜色参数
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_ambientColor, &PrintBedPropertyBridge::ambientColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_specularColor, &PrintBedPropertyBridge::specularColorChanged)

    // Qt6 bindable properties - 渲染属性参数
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, bool, m_edgeVisibility, &PrintBedPropertyBridge::edgeVisibilityChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, QVector3D, m_edgeColor, &PrintBedPropertyBridge::edgeColorChanged)
    Q_OBJECT_BINDABLE_PROPERTY(PrintBedPropertyBridge, float, m_lineWidth, &PrintBedPropertyBridge::lineWidthChanged)

    // PrintBed 实例 ID（系统中只有一个 PrintBed）
    DBInstanceID m_printBedId;
};
