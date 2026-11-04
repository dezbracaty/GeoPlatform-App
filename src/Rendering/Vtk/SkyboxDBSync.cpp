#include "SkyboxDBSync.hpp"
#include "VtkDBSyncFactory.hpp"
#include <SkyboxDB.hpp>
#include <ThemeDB.hpp>
#include <StudioBackgroundResource.hpp>
#include <QFile>
#include <QTemporaryFile>
#include <vtkHDRReader.h>
#include <vtkImageShiftScale.h>
#include <vtkOpenGLRenderer.h>
#include <vtkPNGReader.h>
#include <vtkNew.h>
#include <cmath>
#include <algorithm>

namespace {

constexpr auto kPillarsEnvironmentUri =
    "builtin://gplatform/showcase/pillars-2k";
constexpr auto kPillarsEnvironmentResource =
    ":/startup-tests/renderers/pillars_2k.hdr";

bool resolveHdrPath(const std::string& sourcePath,
                    QTemporaryFile& embeddedFile,
                    QString& resolvedPath) {
    if (sourcePath != kPillarsEnvironmentUri &&
        !sourcePath.starts_with(":/")) {
        resolvedPath = QString::fromStdString(sourcePath);
        return QFile::exists(resolvedPath);
    }

    QFile source(sourcePath == kPillarsEnvironmentUri
        ? QString::fromUtf8(kPillarsEnvironmentResource)
        : QString::fromStdString(sourcePath));
    if (!source.open(QIODevice::ReadOnly) || !embeddedFile.open()) {
        return false;
    }
    const QByteArray payload = source.readAll();
    if (payload.isEmpty() || embeddedFile.write(payload) != payload.size() ||
        !embeddedFile.flush()) {
        return false;
    }
    resolvedPath = embeddedFile.fileName();
    return true;
}

} // namespace

// === 实现 IDBSync 接口 ===

void SkyboxDBSync::initialize(
    const DBInstanceID& id,
    vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {

    m_id = id;
    m_vtkRenderer = renderer;
    m_renderThread = renderThread;

    LOG_DEBUG("SkyboxDBSync::initialize - ID: {}", id.getValue());

    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) {
        LOG_ERROR("SkyboxDBSync::initialize - Invalid SkyboxDB pointer");
        return;
    }

    rebuildSkybox();
}

void SkyboxDBSync::cleanup(const DBInstanceID& id) {
    LOG_DEBUG("SkyboxDBSync::cleanup - Removing skybox for ID: {}", id.getValue());

    if (m_renderThread) {
        // 获取 GEOMETRY 层的渲染器
        auto geometryRenderer = rendererForLayer(RenderLayer::GEOMETRY);

        auto self = shared_from_this();
        dispatchRenderCommand([self, geometryRenderer](auto*, auto*) {
            if (self->m_skybox && self->m_vtkRenderer) {
                self->m_vtkRenderer->RemoveActor(self->m_skybox);

                // 禁用 BACKGROUND 层 IBL
                self->m_vtkRenderer->UseImageBasedLightingOff();
                self->m_vtkRenderer->SetEnvironmentTexture(nullptr);
            }

            // 禁用 GEOMETRY 层 IBL
            if (geometryRenderer && geometryRenderer != self->m_vtkRenderer) {
                geometryRenderer->UseImageBasedLightingOff();
                geometryRenderer->SetEnvironmentTexture(nullptr);
            }

            self->m_skybox = nullptr;
            self->m_environmentTexture = nullptr;
            self->m_hdrImage = nullptr;
            self->m_backgroundTexture = nullptr;
            self->m_backgroundImage = nullptr;
        });
    }
}

void SkyboxDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const std::string& propertyName) {

    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) {
        return;
    }

    LOG_DEBUG("SkyboxDBSync::update - Property: {}", propertyName);

    if (propertyName == "PresetType" || propertyName == "HdrTexturePath" ||
        propertyName == "LightingTexturePath" ||
        propertyName == "LightingIntensity") {
        scheduleSkyboxRebuild();
    } else if (propertyName == "BackgroundTexturePath") {
        scheduleBackgroundUpdate();
    } else if (propertyName == "BackgroundColor") {
        if (skyboxDB->getPresetTypeEnum() == SkyboxDB::PresetType::SOLID_COLOR) {
            scheduleSkyboxRebuild();
        }
    } else if (propertyName == "SkyColorTop" || propertyName == "SkyColorBottom" ||
               propertyName == "GroundColor") {
        updateSkyboxColors();
    } else if (propertyName == "UseImageBasedLighting") {
        updateIBLSettings();
    } else if (propertyName == "Visible") {
        setVisible(skyboxDB->getVisible());
    }
}

void SkyboxDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {
    // Prop 参数版本：使用 PROP_##name() 语法进行属性比较

    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) {
        return;
    }

    LOG_DEBUG("SkyboxDBSync::update - Property: {}", prop.name());

    // 🎯 使用 PROP_##name() 语法进行属性比较
    if (prop == SkyboxDB::PROP_PresetType() ||
        prop == SkyboxDB::PROP_HdrTexturePath() ||
        prop == SkyboxDB::PROP_LightingTexturePath() ||
        prop == SkyboxDB::PROP_LightingIntensity()) {
        scheduleSkyboxRebuild();
    } else if (prop == SkyboxDB::PROP_BackgroundTexturePath()) {
        scheduleBackgroundUpdate();
    } else if (prop == SkyboxDB::PROP_BackgroundColor()) {
        if (skyboxDB->getPresetTypeEnum() == SkyboxDB::PresetType::SOLID_COLOR) {
            scheduleSkyboxRebuild();
        }
    } else if (prop == SkyboxDB::PROP_SkyColorTop() ||
               prop == SkyboxDB::PROP_SkyColorBottom() ||
               prop == SkyboxDB::PROP_GroundColor()) {
        updateSkyboxColors();
    } else if (prop == SkyboxDB::PROP_UseImageBasedLighting()) {
        updateIBLSettings();
    } else if (prop == SkyboxDB::PROP_Visible()) {
        setVisible(skyboxDB->getVisible());
    }
}

void SkyboxDBSync::setVisible(bool visible) {
    if (m_skybox) {
        dispatchVTKOperation([this, visible]() {
            if (m_skybox) m_skybox->SetVisibility(visible ? 1 : 0);
        });
    }
}

// === 天空盒创建和更新方法 ===

void SkyboxDBSync::createGradientSkybox() {
    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) return;

    // 获取颜色配置
    Vector3 skyTop = skyboxDB->getSkyColorTop();
    Vector3 skyBottom = skyboxDB->getSkyColorBottom();
    Vector3 ground = skyboxDB->getGroundColor();

    // 创建 CubeMap 纹理
    m_environmentTexture = vtkSmartPointer<vtkTexture>::New();
    m_environmentTexture->CubeMapOn();
    m_environmentTexture->UseSRGBColorSpaceOn();

    // ============================================
    // VTK CubeMap 面索引（OpenGL 标准 - GL_TEXTURE_CUBE_MAP）：
    // 0: GL_TEXTURE_CUBE_MAP_POSITIVE_X (+X, 右)
    // 1: GL_TEXTURE_CUBE_MAP_NEGATIVE_X (-X, 左)
    // 2: GL_TEXTURE_CUBE_MAP_POSITIVE_Y (+Y, 上) ← VTK默认的"上"
    // 3: GL_TEXTURE_CUBE_MAP_NEGATIVE_Y (-Y, 下) ← VTK默认的"下"
    // 4: GL_TEXTURE_CUBE_MAP_POSITIVE_Z (+Z, 后)
    // 5: GL_TEXTURE_CUBE_MAP_NEGATIVE_Z (-Z, 前)
    //
    // 我们使用 Z-up 坐标系，但 VTK Skybox 使用 Y-up
    // SetFloorPlane(0, 0, 1, 0) 只是设置地平面法向量，不改变 CubeMap 采样
    // 所以我们需要按照 Y-up 来设置纹理：
    // - +Y (索引2) = 天空顶部
    // - -Y (索引3) = 地面
    // - ±X, ±Z (索引0,1,4,5) = 侧面
    // ============================================

    // 颜色提取
    unsigned char topR = static_cast<unsigned char>(skyTop.x);
    unsigned char topG = static_cast<unsigned char>(skyTop.y);
    unsigned char topB = static_cast<unsigned char>(skyTop.z);

    unsigned char horizonR = static_cast<unsigned char>(skyBottom.x);
    unsigned char horizonG = static_cast<unsigned char>(skyBottom.y);
    unsigned char horizonB = static_cast<unsigned char>(skyBottom.z);

    unsigned char groundR = static_cast<unsigned char>(ground.x);
    unsigned char groundG = static_cast<unsigned char>(ground.y);
    unsigned char groundB = static_cast<unsigned char>(ground.z);

    // 四个侧面（±X, ±Z）使用完整的三段渐变：天空→地平线→地面
    // 渐变方向：从图像顶部(y=0)到底部(y=511)
    // 侧面顶部边缘颜色 = skyTop（与+Y顶面匹配）
    // 侧面底部边缘颜色 = ground（与-Y底面匹配）

    // +X 侧面 (右)
    m_environmentTexture->SetInputData(0, createSideFaceGradient(skyTop, skyBottom, ground, 0.5f));
    // -X 侧面 (左)
    m_environmentTexture->SetInputData(1, createSideFaceGradient(skyTop, skyBottom, ground, 0.5f));

    // +Y 顶面 (天空)：纯天空顶部色，与所有侧面的顶边匹配
    m_environmentTexture->SetInputData(2, createSolidCubeFace(topR, topG, topB));

    // -Y 底面 (地面)：纯地面色，与所有侧面的底边匹配
    m_environmentTexture->SetInputData(3, createSolidCubeFace(groundR, groundG, groundB));

    // +Z 侧面 (后)
    m_environmentTexture->SetInputData(4, createSideFaceGradient(skyTop, skyBottom, ground, 0.5f));
    // -Z 侧面 (前)
    m_environmentTexture->SetInputData(5, createSideFaceGradient(skyTop, skyBottom, ground, 0.5f));

    m_environmentTexture->InterpolateOn();
    m_environmentTexture->RepeatOff();
    m_environmentTexture->EdgeClampOn();
    m_environmentTexture->MipmapOn();

    // 创建 Skybox Actor
    m_skybox = vtkSmartPointer<vtkSkybox>::New();
    m_skybox->SetTexture(m_environmentTexture);
    m_skybox->SetFloorPlane(0, 0, 1, 0);  // Z轴向上
    m_skybox->SetProjection(vtkSkybox::Cube);

    LOG_INFO("SkyboxDBSync: Created seamless gradient skybox - Top RGB({},{},{}), Horizon RGB({},{},{}), Ground RGB({},{},{})",
             topR, topG, topB, horizonR, horizonG, horizonB, groundR, groundG, groundB);
}

bool SkyboxDBSync::createHdrSkybox() {
    const auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) {
        return false;
    }

    const std::string& legacyPath = skyboxDB->getHdrTexturePath();
    const std::string& configuredLightingPath =
        skyboxDB->getLightingTexturePath();
    const std::string lightingPath = configuredLightingPath.empty()
        ? legacyPath
        : configuredLightingPath;

    QTemporaryFile embeddedFile;
    QString resolvedPath;
    if (!resolveHdrPath(lightingPath, embeddedFile, resolvedPath)) {
        LOG_ERROR(
            "SkyboxDBSync: unable to resolve HDR environment '{}'",
            lightingPath);
        return false;
    }

    vtkNew<vtkHDRReader> reader;
    const QByteArray nativePath = QFile::encodeName(resolvedPath);
    reader->SetFileName(nativePath.constData());
    reader->Update();
    vtkImageData* output = reader->GetOutput();
    if (!output || output->GetNumberOfPoints() == 0 ||
        output->GetNumberOfScalarComponents() < 3) {
        LOG_ERROR(
            "SkyboxDBSync: VTK failed to decode HDR environment '{}'",
            lightingPath);
        return false;
    }

    // Filament's intensity is relative to 30,000 lux in the shared preset.
    // Preserve that default appearance in VTK while still honoring relative
    // intensity changes from SkyboxDB.
    vtkNew<vtkImageShiftScale> intensityScale;
    intensityScale->SetInputConnection(reader->GetOutputPort());
    intensityScale->SetScale(
        std::max(0.0f, skyboxDB->getLightingIntensity()) / 30000.0f);
    intensityScale->SetOutputScalarTypeToFloat();
    intensityScale->ClampOverflowOff();
    intensityScale->Update();

    // Readers own their output and built-in resources are materialized only
    // for this call. Keep independent images inside the backend.
    m_hdrImage = vtkSmartPointer<vtkImageData>::New();
    m_hdrImage->DeepCopy(intensityScale->GetOutput());

    m_environmentTexture = vtkSmartPointer<vtkTexture>::New();
    m_environmentTexture->SetColorModeToDirectScalars();
    m_environmentTexture->InterpolateOn();
    m_environmentTexture->MipmapOn();
    m_environmentTexture->SetInputData(m_hdrImage);

    m_skybox = vtkSmartPointer<vtkSkybox>::New();
    m_skybox->SetTexture(m_environmentTexture);
    m_skybox->SetProjection(vtkSkybox::Sphere);
    // vtkSkybox defaults to a Y-up floor, while the application and all VTK
    // scene geometry are Z-up. Keep the visible panorama in the same frame as
    // PrintBedDB; otherwise the bed appears to face a wall/ceiling.
    m_skybox->SetFloorPlane(0.0, 0.0, 1.0, 0.0);
    m_skybox->SetFloorRight(1.0, 0.0, 0.0);

    updateHdrBackground();

    const int* dimensions = m_hdrImage->GetDimensions();
    LOG_INFO(
        "SkyboxDBSync: loaded HDR environment '{}' dimensions={}x{} components={}",
        lightingPath, dimensions[0], dimensions[1],
        m_hdrImage->GetNumberOfScalarComponents());
    return true;
}

bool SkyboxDBSync::darkTheme() {
    const auto& document = documentReadView();
    if (!m_themeDiscovered && document) {
        m_themeDiscovered = true;
        const auto themes = document->getDBs(TypeID::THEME_DB);
        if (!themes.empty() && themes.front()) {
            m_themeId = themes.front()->getDBInstanceID();
        }
    }
    const auto theme = m_themeId.isValid() ? documentDB<ThemeDB>(m_themeId) : nullptr;
    return theme && theme->theme() == ThemeDB::Mode::Dark;
}

void SkyboxDBSync::synchronizeView() {
    // This hook already runs on the render thread before drawing. Publishing
    // here also catches a theme changed while this renderer was hidden.
    updateHdrBackground();
}

void SkyboxDBSync::updateHdrBackground() {
    const auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB || !m_skybox || !m_environmentTexture ||
        skyboxDB->getPresetTypeEnum() != SkyboxDB::PresetType::HDR_TEXTURE) {
        return;
    }
    const auto& configuredPath = skyboxDB->getBackgroundTexturePath();
    const auto& legacyPath = skyboxDB->getHdrTexturePath();
    const std::string sourcePath = configuredPath.empty() ? legacyPath : configuredPath;
    const std::string backgroundKey(
        GPlatform::Rendering::resolveStudioBackgroundResource(sourcePath, darkTheme()));
    if ((m_appliedBackgroundKey && *m_appliedBackgroundKey == backgroundKey) ||
        m_failedBackgroundKeys.contains(backgroundKey)) {
        return;
    }
    const auto& configuredLightingPath = skyboxDB->getLightingTexturePath();
    const std::string lightingPath = configuredLightingPath.empty()
        ? legacyPath : configuredLightingPath;
    if (backgroundKey.empty() || backgroundKey == lightingPath) {
        m_skybox->SetTexture(m_environmentTexture);
        m_backgroundTexture = nullptr;
        m_backgroundImage = nullptr;
        m_appliedBackgroundKey = backgroundKey;
        return;
    }

    QTemporaryFile embeddedFile;
    QString resolvedPath;
    if (!resolveHdrPath(backgroundKey, embeddedFile, resolvedPath)) {
        m_failedBackgroundKeys.insert(backgroundKey);
        LOG_WARN("SkyboxDBSync: unable to resolve background '{}'", backgroundKey);
        return;
    }
    vtkNew<vtkPNGReader> reader;
    const QByteArray nativePath = QFile::encodeName(resolvedPath);
    reader->SetFileName(nativePath.constData());
    reader->Update();
    vtkImageData* output = reader->GetOutput();
    if (!output || output->GetNumberOfPoints() == 0 ||
        output->GetNumberOfScalarComponents() < 3) {
        m_failedBackgroundKeys.insert(backgroundKey);
        LOG_WARN("SkyboxDBSync: VTK failed to decode background '{}'", backgroundKey);
        return;
    }
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->DeepCopy(output);
    auto texture = vtkSmartPointer<vtkTexture>::New();
    texture->SetColorModeToDirectScalars();
    texture->UseSRGBColorSpaceOn();
    texture->InterpolateOn();
    texture->MipmapOn();
    texture->SetInputData(image);

    // Keep the HDR image and both renderers' IBL texture unchanged. Only a
    // successfully decoded visible background replaces the current texture.
    m_skybox->SetTexture(texture);
    m_backgroundTexture = texture;
    m_backgroundImage = image;
    m_appliedBackgroundKey = backgroundKey;
}

void SkyboxDBSync::scheduleBackgroundUpdate() {
    auto self = shared_from_this();
    dispatchVTKOperation([self]() {
        self->m_failedBackgroundKeys.clear();
        self->updateHdrBackground();
    });
}

void SkyboxDBSync::rebuildSkybox() {
    const auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB || !m_vtkRenderer) {
        return;
    }

    if (m_skybox) {
        m_vtkRenderer->RemoveActor(m_skybox);
    }
    m_skybox = nullptr;
    m_environmentTexture = nullptr;
    m_hdrImage = nullptr;
    m_backgroundTexture = nullptr;
    m_backgroundImage = nullptr;
    m_appliedBackgroundKey.reset();
    m_failedBackgroundKeys.clear();

    bool created = true;
    switch (skyboxDB->getPresetTypeEnum()) {
    case SkyboxDB::PresetType::GRADIENT:
        createGradientSkybox();
        break;
    case SkyboxDB::PresetType::SOLID_COLOR:
        createSolidColorBackground();
        break;
    case SkyboxDB::PresetType::HDR_TEXTURE:
        created = createHdrSkybox();
        break;
    }

    if (!created) {
        LOG_ERROR("SkyboxDBSync: failed to project SkyboxDB {}", m_id.toString());
    }
    if (m_skybox) {
        m_skybox->SetVisibility(skyboxDB->getVisible() ? 1 : 0);
        m_vtkRenderer->AddActor(m_skybox);
    }
    updateIBLSettings();
}

void SkyboxDBSync::scheduleSkyboxRebuild() {
    if (m_rebuildPending.exchange(true)) {
        return;
    }
    auto self = shared_from_this();
    dispatchVTKOperation([self]() {
        self->m_rebuildPending.store(false);
        self->rebuildSkybox();
    });
}

void SkyboxDBSync::createSolidColorBackground() {
    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) return;

    // 对于纯色背景，直接设置渲染器背景色即可
    Vector3 bgColor = skyboxDB->getBackgroundColor();

    if (m_vtkRenderer) {
        m_vtkRenderer->SetBackground(
            bgColor.x / 255.0,
            bgColor.y / 255.0,
            bgColor.z / 255.0);

        // 禁用 skybox
        if (m_skybox) {
            m_vtkRenderer->RemoveActor(m_skybox);
            m_skybox = nullptr;
        }
        m_environmentTexture = nullptr;
        m_hdrImage = nullptr;
        m_backgroundTexture = nullptr;
        m_backgroundImage = nullptr;
    }

    LOG_INFO("SkyboxDBSync: Set solid color background RGB({},{},{})",
             static_cast<int>(bgColor.x),
             static_cast<int>(bgColor.y),
             static_cast<int>(bgColor.z));
}

void SkyboxDBSync::updateSkyboxColors() {
    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) return;

    if (skyboxDB->getPresetTypeEnum() != SkyboxDB::PresetType::GRADIENT) {
        return;
    }

    scheduleSkyboxRebuild();
}

void SkyboxDBSync::updateIBLSettings() {
    auto skyboxDB = documentDB<SkyboxDB>(m_id);
    if (!skyboxDB) return;

    if (!m_vtkRenderer) {
        return;
    }

    // 获取 GEOMETRY 层的渲染器（PBR 材质物体在这一层）
    auto geometryRenderer = m_renderThread
        ? rendererForLayer(RenderLayer::GEOMETRY)
        : vtkSmartPointer<vtkRenderer>{};

    dispatchVTKOperation([this, skyboxDB, geometryRenderer]() {
        if (skyboxDB->getUseImageBasedLighting() && m_environmentTexture) {
            const bool isSrgb =
                skyboxDB->getPresetTypeEnum() !=
                SkyboxDB::PresetType::HDR_TEXTURE;
            // 🔑 关键修复：同时为 BACKGROUND 和 GEOMETRY 层启用 IBL
            // PBR 材质（如镜面金属）需要 IBL 来渲染环境反射

            // 1. BACKGROUND 层（天空盒所在层）
            m_vtkRenderer->SetEnvironmentTexture(m_environmentTexture, isSrgb);
            m_vtkRenderer->UseImageBasedLightingOn();
            m_vtkRenderer->SetEnvironmentUp(0, 0, 1);
            m_vtkRenderer->SetEnvironmentRight(1, 0, 0);
            if (auto* glRenderer = vtkOpenGLRenderer::SafeDownCast(m_vtkRenderer)) {
                glRenderer->SetUseSphericalHarmonics(false);
            }

            // 2. GEOMETRY 层（PrintBed、模型等 PBR 物体所在层）
            if (geometryRenderer && geometryRenderer != m_vtkRenderer) {
                geometryRenderer->SetEnvironmentTexture(
                    m_environmentTexture, isSrgb);
                geometryRenderer->UseImageBasedLightingOn();
                geometryRenderer->SetEnvironmentUp(0, 0, 1);
                geometryRenderer->SetEnvironmentRight(1, 0, 0);
                if (auto* glRenderer = vtkOpenGLRenderer::SafeDownCast(geometryRenderer)) {
                    glRenderer->SetUseSphericalHarmonics(false);
                }
                LOG_INFO("✅ SkyboxDBSync: IBL enabled on GEOMETRY layer for PBR materials");
            }

            LOG_DEBUG("SkyboxDBSync: IBL enabled on BACKGROUND layer");
        } else {
            // 禁用 IBL（两个层都要禁用）
            m_vtkRenderer->UseImageBasedLightingOff();
            m_vtkRenderer->SetEnvironmentTexture(nullptr);

            if (geometryRenderer && geometryRenderer != m_vtkRenderer) {
                geometryRenderer->UseImageBasedLightingOff();
                geometryRenderer->SetEnvironmentTexture(nullptr);
            }

            LOG_DEBUG("SkyboxDBSync: IBL disabled");
        }
    });
}

// === 辅助方法 ===

vtkSmartPointer<vtkImageData> SkyboxDBSync::createGradientCubeFace(
    unsigned char r1, unsigned char g1, unsigned char b1,
    unsigned char r2, unsigned char g2, unsigned char b2) {

    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(512, 512, 1);
    image->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

    unsigned char* ptr = static_cast<unsigned char*>(image->GetScalarPointer());
    for (int y = 0; y < 512; ++y) {
        for (int x = 0; x < 512; ++x) {
            // 创建从上到下的渐变
            float t = y / 511.0f;
            int idx = (y * 512 + x) * 3;
            ptr[idx + 0] = static_cast<unsigned char>(r1 * (1 - t) + r2 * t);
            ptr[idx + 1] = static_cast<unsigned char>(g1 * (1 - t) + g2 * t);
            ptr[idx + 2] = static_cast<unsigned char>(b1 * (1 - t) + b2 * t);
        }
    }
    return image;
}

vtkSmartPointer<vtkImageData> SkyboxDBSync::createSolidCubeFace(
    unsigned char r, unsigned char g, unsigned char b) {

    // 使用与渐变面相同的分辨率，保持一致性
    constexpr int resolution = 1024;
    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(resolution, resolution, 1);
    image->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

    unsigned char* ptr = static_cast<unsigned char*>(image->GetScalarPointer());
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            int idx = (y * resolution + x) * 3;
            ptr[idx + 0] = r;
            ptr[idx + 1] = g;
            ptr[idx + 2] = b;
        }
    }
    return image;
}

vtkSmartPointer<vtkImageData> SkyboxDBSync::createRadialGradientCubeFace(
    unsigned char centerR, unsigned char centerG, unsigned char centerB,
    unsigned char edgeR, unsigned char edgeG, unsigned char edgeB) {

    // 使用与其他面相同的分辨率
    constexpr int resolution = 1024;
    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(resolution, resolution, 1);
    image->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

    unsigned char* ptr = static_cast<unsigned char*>(image->GetScalarPointer());
    float centerX = (resolution - 1) / 2.0f;
    float centerY = (resolution - 1) / 2.0f;
    float maxDist = centerX * 1.414f; // 对角线距离

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            float dx = x - centerX;
            float dy = y - centerY;
            float dist = std::sqrt(dx * dx + dy * dy);
            float t = std::min(dist / maxDist, 1.0f);

            int idx = (y * resolution + x) * 3;
            ptr[idx + 0] = static_cast<unsigned char>(centerR * (1 - t) + edgeR * t);
            ptr[idx + 1] = static_cast<unsigned char>(centerG * (1 - t) + edgeG * t);
            ptr[idx + 2] = static_cast<unsigned char>(centerB * (1 - t) + edgeB * t);
        }
    }
    return image;
}

vtkSmartPointer<vtkImageData> SkyboxDBSync::createSideFaceGradient(
    const Vector3& topColor,
    const Vector3& bottomColor,
    const Vector3& groundColor,
    float horizonRatio) {

    // 🔑 优化1：提高纹理分辨率到 1024x1024，减少色带效应
    constexpr int resolution = 1024;
    vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(resolution, resolution, 1);
    image->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

    unsigned char* ptr = static_cast<unsigned char*>(image->GetScalarPointer());

    // 🔑 优化2：使用 Bayer Matrix 抖动，打破色带，视觉上更平滑
    // Bayer 4x4 矩阵提供均匀的抖动模式
    static const int bayerMatrix[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5}
    };

    // 使用平滑的三段渐变 + smoothstep 缓动函数
    // 这样可以让颜色过渡更加自然，避免硬边
    //
    // y=0 (顶部): topColor (与+Y顶面匹配)
    // y=horizonY: bottomColor (地平线)
    // y=resolution-1 (底部): groundColor (与-Y底面匹配)

    int horizonY = static_cast<int>((resolution - 1) * horizonRatio);

    // smoothstep 函数：提供更平滑的过渡
    auto smoothstep = [](float t) -> float {
        t = std::max(0.0f, std::min(1.0f, t));
        return t * t * (3.0f - 2.0f * t);
    };

    for (int y = 0; y < resolution; ++y) {
        float r, g, b;

        if (y <= horizonY) {
            // 天空区域：从顶部色渐变到地平线色
            float t = (horizonY > 0) ? static_cast<float>(y) / horizonY : 0.0f;
            t = smoothstep(t);  // 应用平滑过渡
            r = topColor.x * (1 - t) + bottomColor.x * t;
            g = topColor.y * (1 - t) + bottomColor.y * t;
            b = topColor.z * (1 - t) + bottomColor.z * t;
        } else {
            // 地面区域：从地平线色渐变到地面色
            float t = static_cast<float>(y - horizonY) / ((resolution - 1) - horizonY);
            t = smoothstep(t);  // 应用平滑过渡
            r = bottomColor.x * (1 - t) + groundColor.x * t;
            g = bottomColor.y * (1 - t) + groundColor.y * t;
            b = bottomColor.z * (1 - t) + groundColor.z * t;
        }

        for (int x = 0; x < resolution; ++x) {
            // 应用 Bayer Matrix 抖动，打破色带
            // 抖动强度：±0.47 (微妙，不会产生明显噪点)
            float dither = (bayerMatrix[y % 4][x % 4] - 7.5f) / 16.0f;

            int idx = (y * resolution + x) * 3;
            ptr[idx + 0] = static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, r + dither)));
            ptr[idx + 1] = static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, g + dither)));
            ptr[idx + 2] = static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, b + dither)));
        }
    }
    return image;
}

void SkyboxDBSync::dispatchVTKOperation(std::function<void()> operation) {
    if (m_renderThread) {
        auto self = shared_from_this();
        dispatchRenderCommand([self, operation](auto*, auto*) {
            operation();
        });
    } else {
        // 直接执行（可能在初始化阶段）
        operation();
    }
}

// 静态注册
REGISTER_VTK_DBSYNC(TypeID::SKYBOX_DB, SkyboxDBSync,
                        GPlatform::Rendering::RendererRoleSets::PrepareAndPreview)
