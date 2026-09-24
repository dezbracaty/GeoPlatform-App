#include "../include/PrintBedPropertyBridge.hpp"
#include "BridgeRegistration.hpp"
#include <DocumentManager.hpp>
#include <MaterialDB.hpp>
#include <TransactionManager.hpp>
#include "Foundation/Log.h"

PrintBedPropertyBridge::PrintBedPropertyBridge(QObject* parent)
    : PropertyBridgeBase(parent) {
    LOG_INFO("PrintBedPropertyBridge created");
    findAndListenToPrintBed();
}

PrintBedPropertyBridge::~PrintBedPropertyBridge() {
    LOG_INFO("PrintBedPropertyBridge destroyed");
}

QObject* PrintBedPropertyBridge::create(QQmlEngine* engine, QJSEngine* scriptEngine) {
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return new PrintBedPropertyBridge();
}

void PrintBedPropertyBridge::findAndListenToPrintBed() {
    // 在 DocumentManager 中查找 PrintBedDB 实例
    auto& docManager = *DocumentManager::instance();

    // 使用 getDBInstancesByType 获取 PrintBedDB 类型的所有实例
    auto printBeds = docManager.getDBInstancesByType(TypeID::PRINT_BED_DB);

    if (!printBeds.empty()) {
        // 获取第一个 PrintBed 实例
        auto printBed = std::dynamic_pointer_cast<PrintBedDB>(printBeds[0]);
        if (printBed) {
            m_printBedId = printBed->getDBInstanceID();
            startListeningTo(m_printBedId);
            updateAllProperties();
            LOG_INFO("PrintBedPropertyBridge found and listening to PrintBed ID: {}", m_printBedId.getValue());
            return;
        }
    }

    LOG_WARN("PrintBedPropertyBridge: No PrintBedDB instance found in DocumentManager");
}

std::shared_ptr<PrintBedDB> PrintBedPropertyBridge::getPrintBedDB() const {
    if (!m_printBedId.isValid()) {
        return nullptr;
    }

    auto& docManager = *DocumentManager::instance();
    if (auto dbInstance = docManager.getDBInstance(m_printBedId)) {
        return std::dynamic_pointer_cast<PrintBedDB>(dbInstance);
    }
    return nullptr;
}

void PrintBedPropertyBridge::updateAllProperties() {
    auto printBed = getPrintBedDB();
    if (!printBed) {
        clearProperties();
        return;
    }

    // 几何属性
    m_width = printBed->getWidth();
    m_height = printBed->getHeight();
    emit bedSizeTextChanged();
    m_thickness = printBed->getThickness();
    m_printHeight = printBed->getPrintHeight();

    // 网格显示属性
    m_showGrid = printBed->getShowGrid();
    m_gridSpacing = printBed->getGridSpacing();

    auto gridColor = printBed->getGridColor();
    m_gridColor = QVector3D(gridColor.x, gridColor.y, gridColor.z);

    m_showBounds = printBed->getShowBounds();

    auto boundColor = printBed->getBoundColor();
    m_boundColor = QVector3D(boundColor.x, boundColor.y, boundColor.z);

    // 材质属性（来自 Material）
    if (auto material = printBed->getMaterial()) {
        auto color = material->getDiffuseColor();
        m_platformColor = QVector3D(color.x, color.y, color.z);
        m_metallic = material->getMetallic();
        m_roughness = material->getRoughness();
    }

    // 透明度（来自 Material）
    if (auto material = printBed->getMaterial()) {
        m_opacity = material->getOpacity();
    }

    // 材质预设（目前设为默认值，可以通过分析当前材质参数推断）
    m_materialPreset = static_cast<int>(PrintBedDB::MaterialPreset::PLASTIC);

    // 渲染模式（来自 Material）
    if (auto material = printBed->getMaterial()) {
        m_renderingMode = material->getRenderingMode();
        m_ambient = material->getAmbient();
        m_diffuse = material->getDiffuse();
        m_specular = material->getSpecular();
        m_specularPower = material->getSpecularPower();

        auto ambientColor = material->getAmbientColor();
        m_ambientColor = QVector3D(ambientColor.x, ambientColor.y, ambientColor.z);

        auto specularColor = material->getSpecularColor();
        m_specularColor = QVector3D(specularColor.x, specularColor.y, specularColor.z);

        m_edgeVisibility = material->getEdgeVisibility();

        auto edgeColor = material->getEdgeColor();
        m_edgeColor = QVector3D(edgeColor.x, edgeColor.y, edgeColor.z);

        m_lineWidth = material->getLineWidth();
    }

    LOG_INFO("PrintBedPropertyBridge updated all properties from PrintBed ID: {}", m_printBedId.getValue());
}

void PrintBedPropertyBridge::updateSpecificProperty(const std::string& propertyName) {
    auto printBed = getPrintBedDB();
    if (!printBed) return;

    // 几何属性
    if (propertyName == "Width") {
        m_width = printBed->getWidth();
        emit bedSizeTextChanged();
    }
    else if (propertyName == "Height") {
        m_height = printBed->getHeight();
        emit bedSizeTextChanged();
    }
    else if (propertyName == "Thickness") {
        m_thickness = printBed->getThickness();
    }
    else if (propertyName == "PrintHeight") {
        m_printHeight = printBed->getPrintHeight();
    }
    // 网格显示属性
    else if (propertyName == "ShowGrid") {
        m_showGrid = printBed->getShowGrid();
    }
    else if (propertyName == "GridSpacing") {
        m_gridSpacing = printBed->getGridSpacing();
    }
    else if (propertyName == "GridColor") {
        auto gridColor = printBed->getGridColor();
        m_gridColor = QVector3D(gridColor.x, gridColor.y, gridColor.z);
    }
    else if (propertyName == "ShowBounds") {
        m_showBounds = printBed->getShowBounds();
    }
    else if (propertyName == "BoundColor") {
        auto boundColor = printBed->getBoundColor();
        m_boundColor = QVector3D(boundColor.x, boundColor.y, boundColor.z);
    }
    // 材质属性（来自 Material）
    else if (propertyName == "Material.DiffuseColor" || propertyName == "DiffuseColor") {
        if (auto material = printBed->getMaterial()) {
            auto color = material->getDiffuseColor();
            m_platformColor = QVector3D(color.x, color.y, color.z);
        }
    }
    else if (propertyName == "Material.Metallic" || propertyName == "Metallic") {
        if (auto material = printBed->getMaterial()) {
            m_metallic = material->getMetallic();
        }
    }
    else if (propertyName == "Material.Roughness" || propertyName == "Roughness") {
        if (auto material = printBed->getMaterial()) {
            m_roughness = material->getRoughness();
        }
    }
    // 透明度属性（来自 Material）
    else if (propertyName == "Opacity" || propertyName == "Material.Opacity") {
        if (auto material = printBed->getMaterial()) {
            m_opacity = material->getOpacity();
        }
    }
    // VTK 光照参数（来自 Material）
    else if (propertyName == "Material.Ambient" || propertyName == "Ambient") {
        if (auto material = printBed->getMaterial()) {
            m_ambient = material->getAmbient();
        }
    }
    else if (propertyName == "Material.Diffuse" || propertyName == "Diffuse") {
        if (auto material = printBed->getMaterial()) {
            m_diffuse = material->getDiffuse();
        }
    }
    else if (propertyName == "Material.Specular" || propertyName == "Specular") {
        if (auto material = printBed->getMaterial()) {
            m_specular = material->getSpecular();
        }
    }
    else if (propertyName == "Material.SpecularPower" || propertyName == "SpecularPower") {
        if (auto material = printBed->getMaterial()) {
            m_specularPower = material->getSpecularPower();
        }
    }
    // Phong 颜色参数（来自 Material）
    else if (propertyName == "Material.AmbientColor" || propertyName == "AmbientColor") {
        if (auto material = printBed->getMaterial()) {
            auto ambientColor = material->getAmbientColor();
            m_ambientColor = QVector3D(ambientColor.x, ambientColor.y, ambientColor.z);
            LOG_DEBUG("PrintBedPropertyBridge: AmbientColor updated to RGB({:.2f},{:.2f},{:.2f})",
                      ambientColor.x, ambientColor.y, ambientColor.z);
        }
    }
    else if (propertyName == "Material.SpecularColor" || propertyName == "SpecularColor") {
        if (auto material = printBed->getMaterial()) {
            auto specularColor = material->getSpecularColor();
            m_specularColor = QVector3D(specularColor.x, specularColor.y, specularColor.z);
            LOG_DEBUG("PrintBedPropertyBridge: SpecularColor updated to RGB({:.2f},{:.2f},{:.2f})",
                      specularColor.x, specularColor.y, specularColor.z);
        }
    }
    // 渲染属性参数（来自 Material）
    else if (propertyName == "Material.EdgeVisibility" || propertyName == "EdgeVisibility") {
        if (auto material = printBed->getMaterial()) {
            m_edgeVisibility = material->getEdgeVisibility();
        }
    }
    else if (propertyName == "Material.EdgeColor" || propertyName == "EdgeColor") {
        if (auto material = printBed->getMaterial()) {
            auto edgeColor = material->getEdgeColor();
            m_edgeColor = QVector3D(edgeColor.x, edgeColor.y, edgeColor.z);
        }
    }
    else if (propertyName == "Material.LineWidth" || propertyName == "LineWidth") {
        if (auto material = printBed->getMaterial()) {
            m_lineWidth = material->getLineWidth();
        }
    }
    else if (propertyName == "Material.RenderingMode" || propertyName == "RenderingMode") {
        if (auto material = printBed->getMaterial()) {
            m_renderingMode = material->getRenderingMode();
            LOG_DEBUG("PrintBedPropertyBridge: RenderingMode updated to {}", m_renderingMode.value());
        }
    }
    else {
        LOG_DEBUG("PrintBedPropertyBridge: Unknown property changed: '{}'", propertyName);
    }
}

void PrintBedPropertyBridge::clearProperties() {
    m_width = 0.0f;
    m_height = 0.0f;
    emit bedSizeTextChanged();
    m_thickness = 0.0f;
    m_printHeight = 0.0f;
    m_showGrid = false;
    m_gridSpacing = 0.0f;
    m_gridColor = QVector3D();
    m_showBounds = false;
    m_boundColor = QVector3D();
    m_platformColor = QVector3D();
    m_metallic = 0.0f;
    m_roughness = 0.0f;
    m_opacity = 1.0f;
    m_materialPreset = 0;
    m_renderingMode = 1;  // MaterialDB::RenderingMode::PBR
    m_ambient = 0.0f;
    m_diffuse = 0.0f;
    m_specular = 0.0f;
    m_specularPower = 1.0f;
    m_ambientColor = QVector3D();
    m_specularColor = QVector3D();
    m_edgeVisibility = false;
    m_edgeColor = QVector3D();
    m_lineWidth = 1.0f;
}

void PrintBedPropertyBridge::onPropertyChanged(const DBInstanceID& id,
                                               const std::string& propertyName,
                                               const std::any& newValue) {
    if (id != m_printBedId) return;

    LOG_DEBUG("PrintBedPropertyBridge::onPropertyChanged - Property '{}' changed for ID: {}",
              propertyName, id.toString());

    // 只更新变化的属性
    updateSpecificProperty(propertyName);
}

void PrintBedPropertyBridge::onObjectCreated(const DBInstanceID& id) {
    // 如果创建了新的 PrintBed，监听它
    auto& docManager = *DocumentManager::instance();
    if (auto dbInstance = docManager.getDBInstance(id)) {
        if (auto printBed = std::dynamic_pointer_cast<PrintBedDB>(dbInstance)) {
            if (!m_printBedId.isValid()) {
                m_printBedId = id;
                startListeningTo(m_printBedId);
                updateAllProperties();
                LOG_INFO("PrintBedPropertyBridge started listening to new PrintBed ID: {}", m_printBedId.getValue());
            }
        }
    }
}

void PrintBedPropertyBridge::onObjectDeleted(const DBInstanceID& id) {
    if (id == m_printBedId) {
        m_printBedId = DBInstanceID(0);  // Set to invalid ID
        clearProperties();
        LOG_WARN("PrintBedPropertyBridge: PrintBed was deleted");
    }
}

// ============================================================================
// Property Getters
// ============================================================================

float PrintBedPropertyBridge::width() const { return m_width; }
float PrintBedPropertyBridge::height() const { return m_height; }
float PrintBedPropertyBridge::thickness() const { return m_thickness; }
float PrintBedPropertyBridge::printHeight() const { return m_printHeight; }
QString PrintBedPropertyBridge::bedSizeText() const {
    if (m_width <= 0.0f || m_height <= 0.0f) {
        return QString();
    }
    return QStringLiteral("%1×%2mm")
        .arg(QString::number(m_width, 'f', 0))
        .arg(QString::number(m_height, 'f', 0));
}
bool PrintBedPropertyBridge::showGrid() const { return m_showGrid; }
float PrintBedPropertyBridge::gridSpacing() const { return m_gridSpacing; }
QVector3D PrintBedPropertyBridge::gridColor() const { return m_gridColor; }
bool PrintBedPropertyBridge::showBounds() const { return m_showBounds; }
QVector3D PrintBedPropertyBridge::boundColor() const { return m_boundColor; }
QVector3D PrintBedPropertyBridge::platformColor() const { return m_platformColor; }
float PrintBedPropertyBridge::metallic() const { return m_metallic; }
float PrintBedPropertyBridge::roughness() const { return m_roughness; }
float PrintBedPropertyBridge::opacity() const { return m_opacity; }
int PrintBedPropertyBridge::materialPreset() const { return m_materialPreset; }
int PrintBedPropertyBridge::renderingMode() const { return m_renderingMode; }
float PrintBedPropertyBridge::ambient() const { return m_ambient; }
float PrintBedPropertyBridge::diffuse() const { return m_diffuse; }
float PrintBedPropertyBridge::specular() const { return m_specular; }
float PrintBedPropertyBridge::specularPower() const { return m_specularPower; }
QVector3D PrintBedPropertyBridge::ambientColor() const { return m_ambientColor; }
QVector3D PrintBedPropertyBridge::specularColor() const { return m_specularColor; }
bool PrintBedPropertyBridge::edgeVisibility() const { return m_edgeVisibility; }
QVector3D PrintBedPropertyBridge::edgeColor() const { return m_edgeColor; }
float PrintBedPropertyBridge::lineWidth() const { return m_lineWidth; }

// ============================================================================
// Property Setters (with Transaction Support)
// ============================================================================

void PrintBedPropertyBridge::setWidth(float width) {
    if (qFuzzyCompare(m_width, width)) return;

    executeInTransaction("设置打印床宽度", [this, width]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setWidth(width);
        }
    });
}

void PrintBedPropertyBridge::setHeight(float height) {
    if (qFuzzyCompare(m_height, height)) return;

    executeInTransaction("设置打印床深度", [this, height]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setHeight(height);
        }
    });
}

void PrintBedPropertyBridge::setThickness(float thickness) {
    if (qFuzzyCompare(m_thickness, thickness)) return;

    executeInTransaction("设置平台厚度", [this, thickness]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setThickness(thickness);
        }
    });
}

void PrintBedPropertyBridge::setPrintHeight(float printHeight) {
    if (qFuzzyCompare(m_printHeight, printHeight)) return;

    executeInTransaction("设置可打印高度", [this, printHeight]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setPrintHeight(printHeight);
        }
    });
}

void PrintBedPropertyBridge::setShowGrid(bool show) {
    if (m_showGrid == show) return;

    executeInTransaction("切换网格显示", [this, show]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setShowGrid(show);
        }
    });
}

void PrintBedPropertyBridge::setGridSpacing(float spacing) {
    if (qFuzzyCompare(m_gridSpacing, spacing)) return;

    executeInTransaction("设置网格间距", [this, spacing]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setGridSpacing(spacing);
        }
    });
}

void PrintBedPropertyBridge::setGridColor(const QVector3D& color) {
    if (m_gridColor == color) return;

    executeInTransaction("设置网格颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setGridColor(Vector3(color.x(), color.y(), color.z()));
        }
    });
}

void PrintBedPropertyBridge::setShowBounds(bool show) {
    if (m_showBounds == show) return;

    executeInTransaction("切换边界显示", [this, show]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setShowBounds(show);
        }
    });
}

void PrintBedPropertyBridge::setBoundColor(const QVector3D& color) {
    if (m_boundColor == color) return;

    executeInTransaction("设置边界颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->setBoundColor(Vector3(color.x(), color.y(), color.z()));
        }
    });
}

void PrintBedPropertyBridge::setPlatformColor(const QVector3D& color) {
    if (m_platformColor == color) return;

    executeInTransaction("设置平台颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setDiffuseColor(Vector3(color.x(), color.y(), color.z()));
            }
        }
    });
}

void PrintBedPropertyBridge::setMetallic(float metallic) {
    if (qFuzzyCompare(m_metallic, metallic)) return;

    executeInTransaction("设置金属度", [this, metallic]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setMetallic(metallic);
            }
        }
    });
}

void PrintBedPropertyBridge::setRoughness(float roughness) {
    if (qFuzzyCompare(m_roughness, roughness)) return;

    executeInTransaction("设置粗糙度", [this, roughness]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setRoughness(roughness);
            }
        }
    });
}

void PrintBedPropertyBridge::setOpacity(float opacity) {
    if (qFuzzyCompare(m_opacity, opacity)) return;

    executeInTransaction("设置透明度", [this, opacity]() {
        if (auto printBed = getPrintBedDB()) {
            // 设置 Material 的 Opacity 属性
            if (auto material = printBed->getMaterial()) {
                material->setOpacity(opacity);
            }
        }
    });
}

void PrintBedPropertyBridge::setMaterialPreset(int preset) {
    if (m_materialPreset == preset) return;

    executeInTransaction("应用材质预设", [this, preset]() {
        if (auto printBed = getPrintBedDB()) {
            printBed->applyMaterialPreset(static_cast<PrintBedDB::MaterialPreset>(preset));
        }
    });
    m_materialPreset = preset;
}

void PrintBedPropertyBridge::setRenderingMode(int mode) {
    if (m_renderingMode == mode) return;

    executeInTransaction("设置渲染模式", [this, mode]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setRenderingMode(mode);
            }
        }
    });
}

void PrintBedPropertyBridge::setAmbient(float ambient) {
    if (qFuzzyCompare(m_ambient, ambient)) return;

    executeInTransaction("设置环境光系数", [this, ambient]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setAmbient(ambient);
            }
        }
    });
}

void PrintBedPropertyBridge::setDiffuse(float diffuse) {
    if (qFuzzyCompare(m_diffuse, diffuse)) return;

    executeInTransaction("设置漫反射系数", [this, diffuse]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setDiffuse(diffuse);
            }
        }
    });
}

void PrintBedPropertyBridge::setSpecular(float specular) {
    if (qFuzzyCompare(m_specular, specular)) return;

    executeInTransaction("设置镜面反射系数", [this, specular]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setSpecular(specular);
            }
        }
    });
}

void PrintBedPropertyBridge::setSpecularPower(float power) {
    if (qFuzzyCompare(m_specularPower, power)) return;

    executeInTransaction("设置镜面反射强度", [this, power]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setSpecularPower(power);
            }
        }
    });
}

void PrintBedPropertyBridge::setAmbientColor(const QVector3D& color) {
    if (m_ambientColor == color) return;

    LOG_DEBUG("PrintBedPropertyBridge::setAmbientColor - Setting color to RGB({:.2f},{:.2f},{:.2f})",
              color.x(), color.y(), color.z());

    executeInTransaction("设置环境光颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setAmbientColor(Vector3(color.x(), color.y(), color.z()));
                LOG_DEBUG("PrintBedPropertyBridge::setAmbientColor - Material updated");

                // 🔑 CRITICAL: 立即更新本地属性以触发 QML UI 刷新
                // 虽然 onPropertyChanged 会被调用,但可能存在时序问题
                m_ambientColor.setValue(color);
                LOG_DEBUG("PrintBedPropertyBridge::setAmbientColor - Local property forcibly updated for immediate UI refresh");
            }
        }
    });
}

void PrintBedPropertyBridge::setSpecularColor(const QVector3D& color) {
    if (m_specularColor == color) return;

    LOG_DEBUG("PrintBedPropertyBridge::setSpecularColor - Setting color to RGB({:.2f},{:.2f},{:.2f})",
              color.x(), color.y(), color.z());

    executeInTransaction("设置镜面反射颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setSpecularColor(Vector3(color.x(), color.y(), color.z()));
                LOG_DEBUG("PrintBedPropertyBridge::setSpecularColor - Material updated");

                // 🔑 CRITICAL: 立即更新本地属性以触发 QML UI 刷新
                m_specularColor.setValue(color);
                LOG_DEBUG("PrintBedPropertyBridge::setSpecularColor - Local property forcibly updated for immediate UI refresh");
            }
        }
    });
}

void PrintBedPropertyBridge::setEdgeVisibility(bool visible) {
    if (m_edgeVisibility == visible) return;

    executeInTransaction("切换边缘可见性", [this, visible]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setEdgeVisibility(visible);
            }
        }
    });
}

void PrintBedPropertyBridge::setEdgeColor(const QVector3D& color) {
    if (m_edgeColor == color) return;

    executeInTransaction("设置边缘颜色", [this, color]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setEdgeColor(Vector3(color.x(), color.y(), color.z()));
            }
        }
    });
}

void PrintBedPropertyBridge::setLineWidth(float width) {
    if (qFuzzyCompare(m_lineWidth, width)) return;

    executeInTransaction("设置线宽", [this, width]() {
        if (auto printBed = getPrintBedDB()) {
            if (auto material = printBed->getMaterial()) {
                material->setLineWidth(width);
            }
        }
    });
}

// ============================================================================
// Bindables
// ============================================================================

QBindable<float> PrintBedPropertyBridge::bindableWidth() { return &m_width; }
QBindable<float> PrintBedPropertyBridge::bindableHeight() { return &m_height; }
QBindable<float> PrintBedPropertyBridge::bindableThickness() { return &m_thickness; }
QBindable<float> PrintBedPropertyBridge::bindablePrintHeight() { return &m_printHeight; }
QBindable<bool> PrintBedPropertyBridge::bindableShowGrid() { return &m_showGrid; }
QBindable<float> PrintBedPropertyBridge::bindableGridSpacing() { return &m_gridSpacing; }
QBindable<QVector3D> PrintBedPropertyBridge::bindableGridColor() { return &m_gridColor; }
QBindable<bool> PrintBedPropertyBridge::bindableShowBounds() { return &m_showBounds; }
QBindable<QVector3D> PrintBedPropertyBridge::bindableBoundColor() { return &m_boundColor; }
QBindable<QVector3D> PrintBedPropertyBridge::bindablePlatformColor() { return &m_platformColor; }
QBindable<float> PrintBedPropertyBridge::bindableMetallic() { return &m_metallic; }
QBindable<float> PrintBedPropertyBridge::bindableRoughness() { return &m_roughness; }
QBindable<float> PrintBedPropertyBridge::bindableOpacity() { return &m_opacity; }
QBindable<int> PrintBedPropertyBridge::bindableMaterialPreset() { return &m_materialPreset; }
QBindable<int> PrintBedPropertyBridge::bindableRenderingMode() { return &m_renderingMode; }
QBindable<float> PrintBedPropertyBridge::bindableAmbient() { return &m_ambient; }
QBindable<float> PrintBedPropertyBridge::bindableDiffuse() { return &m_diffuse; }
QBindable<float> PrintBedPropertyBridge::bindableSpecular() { return &m_specular; }
QBindable<float> PrintBedPropertyBridge::bindableSpecularPower() { return &m_specularPower; }
QBindable<QVector3D> PrintBedPropertyBridge::bindableAmbientColor() { return &m_ambientColor; }
QBindable<QVector3D> PrintBedPropertyBridge::bindableSpecularColor() { return &m_specularColor; }
QBindable<bool> PrintBedPropertyBridge::bindableEdgeVisibility() { return &m_edgeVisibility; }
QBindable<QVector3D> PrintBedPropertyBridge::bindableEdgeColor() { return &m_edgeColor; }
QBindable<float> PrintBedPropertyBridge::bindableLineWidth() { return &m_lineWidth; }

REGISTER_BRIDGE_QML_SINGLETON_CUSTOM(
    PrintBedPropertyBridge,
    "PrintBedPropertyBridge",
    &PrintBedPropertyBridge::create)
