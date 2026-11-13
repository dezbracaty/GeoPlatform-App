#include "PrintBedDBSync.hpp"
#include "VtkDBSyncFactory.hpp"
#include "Foundation/Log.h"
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyData.h>
#include <vtkLineSource.h>
#include <vtkPlaneSource.h>
#include <vtkConeSource.h>
#include <vtkFeatureEdges.h>
#include <vtkTubeFilter.h>
#include <vtkPointData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkMatrix4x4.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkShaderProperty.h>
#include <vtkUniforms.h>
#include <vtkCellArray.h>
#include <vtkInformation.h>
#include <vtkOpenGLActor.h>
#include <vtkPNGReader.h>
#include <vtkPoints.h>
#include <cmath>
#include <filesystem>

namespace {
constexpr double kAxisX[3] = {1.0, 0.0, 0.0};
constexpr double kAxisY[3] = {0.0, 1.0, 0.0};
constexpr double kAxisZ[3] = {0.0, 0.0, 1.0};
constexpr double kOrcaBedGroundZ = -0.04;
constexpr double kOrcaBedModelZOffset = -0.41 + kOrcaBedGroundZ;
constexpr double kBedTextureZ = 0.080;
constexpr double kGridZ = kBedTextureZ + 0.120;

std::string resolveBedModelResource(const PrintBedDB& printBedDB) {
    return printBedDB.getBedModelPath();
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec) && !ec;
}

std::filesystem::path findResourceFile(const std::string& resourceName, const std::string&) {
    if (resourceName.empty()) {
        return {};
    }
    const std::filesystem::path directPath(resourceName);
    return fileExists(directPath) ? directPath.lexically_normal() : std::filesystem::path{};
}

void addGridLine(vtkPoints* points, vtkCellArray* lines,
                 double x1, double y1, double x2, double y2) {
    const vtkIdType first = points->InsertNextPoint(x1, y1, kGridZ);
    const vtkIdType second = points->InsertNextPoint(x2, y2, kGridZ);
    lines->InsertNextCell(2);
    lines->InsertCellPoint(first);
    lines->InsertCellPoint(second);
}

vtkSmartPointer<vtkActor> makeGridActor(vtkSmartPointer<vtkPolyData> geometry) {
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(geometry);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->SetPickable(false);
    return actor;
}

void configureBedTextureProperty(vtkActor* actor) {
    if (!actor) return;
    auto* property = actor->GetProperty();
    property->SetColor(1.0, 1.0, 1.0);
    property->SetOpacity(1.0);
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);
    property->SetEdgeVisibility(false);
    property->LightingOff();
    actor->ForceTranslucentOn();
    auto keys = vtkSmartPointer<vtkInformation>::New();
    keys->Set(vtkOpenGLActor::GLDepthMaskOverride(), 0);
    actor->SetPropertyKeys(keys);
    actor->SetPickable(false);
}

void configureBedProperty(vtkProperty* property) {
    if (!property) return;
    property->SetColor(PrintBedViewDefaults::BED_COLOR_R,
                       PrintBedViewDefaults::BED_COLOR_G,
                       PrintBedViewDefaults::BED_COLOR_B);
    property->SetOpacity(1.0);
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);
    property->SetSpecularPower(1.0);
    property->SetEdgeVisibility(false);
    property->SetRepresentationToSurface();
    property->LightingOff();
}

void configureFlashForgeBedProperty(vtkProperty* property) {
    if (!property) return;
    property->SetColor(0.3255, 0.337, 0.337);
    property->SetOpacity(1.0);
    property->SetAmbient(0.52);
    property->SetDiffuse(0.48);
    property->SetSpecular(0.06);
    property->SetSpecularPower(14.0);
    property->SetEdgeVisibility(false);
    property->SetRepresentationToSurface();
    property->LightingOn();
}

void configureOrcaHotbedShader(vtkActor* actor, const PrintBedDB& printBedDB) {
    if (!actor) return;

    auto* shaderProperty = actor->GetShaderProperty();
    if (!shaderProperty) return;

    const Vector3 center = printBedDB.getCenter();
    const float halfW = printBedDB.getWidth() * 0.5f;
    const float halfH = printBedDB.getHeight() * 0.5f;
    const float xyData[4] = {
        center.x - halfW,
        center.y - halfH,
        center.x + halfW,
        center.y + halfH
    };
    const float zData[2] = {-1.0f, printBedDB.getPrintHeight()};
    const float uniformColor[4] = {0.3255f, 0.3370f, 0.3370f, 1.0f};

    shaderProperty->ClearAllVertexShaderReplacements();
    shaderProperty->ClearAllFragmentShaderReplacements();

    shaderProperty->AddVertexShaderReplacement(
        "//VTK::PositionVC::Dec",
        true,
        "//VTK::PositionVC::Dec\n"
        "out vec4 orcaWorldPosVSOutput;\n",
        false);
    shaderProperty->AddVertexShaderReplacement(
        "//VTK::PositionVC::Impl",
        true,
        "//VTK::PositionVC::Impl\n"
        "  orcaWorldPosVSOutput = vertexMC;\n",
        false);
    shaderProperty->AddFragmentShaderReplacement(
        "//VTK::PositionVC::Dec",
        true,
        "//VTK::PositionVC::Dec\n"
        "in vec4 orcaWorldPosVSOutput;\n",
        false);
    shaderProperty->AddFragmentShaderReplacement(
        "//VTK::CustomUniforms::Dec",
        true,
        "uniform vec4 orcaUniformColor;\n"
        "uniform float orcaEmissionFactor;\n"
        "uniform int orcaPrintVolumeType;\n"
        "uniform vec4 orcaPrintVolumeXYData;\n"
        "uniform vec2 orcaPrintVolumeZData;\n",
        false);
    shaderProperty->AddFragmentShaderReplacement(
        "//VTK::Light::Impl",
        false,
        "  const float ORCA_INTENSITY_CORRECTION = 0.6;\n"
        "  const vec3 ORCA_LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);\n"
        "  const float ORCA_LIGHT_TOP_DIFFUSE = 0.8 * ORCA_INTENSITY_CORRECTION;\n"
        "  const float ORCA_LIGHT_TOP_SPECULAR = 0.125 * ORCA_INTENSITY_CORRECTION;\n"
        "  const float ORCA_LIGHT_TOP_SHININESS = 20.0;\n"
        "  const vec3 ORCA_LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);\n"
        "  const float ORCA_LIGHT_FRONT_DIFFUSE = 0.3 * ORCA_INTENSITY_CORRECTION;\n"
        "  const float ORCA_INTENSITY_AMBIENT = 0.3;\n"
        "  vec3 orcaNormal = normalize(normalVCVSOutput);\n"
        "  vec2 orcaIntensity = vec2(0.0);\n"
        "  float orcaNdotL = max(dot(orcaNormal, ORCA_LIGHT_TOP_DIR), 0.0);\n"
        "  orcaIntensity.x = ORCA_INTENSITY_AMBIENT + orcaNdotL * ORCA_LIGHT_TOP_DIFFUSE;\n"
        "  orcaIntensity.y = ORCA_LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(vertexVC.xyz), reflect(-ORCA_LIGHT_TOP_DIR, orcaNormal)), 0.0), ORCA_LIGHT_TOP_SHININESS);\n"
        "  orcaNdotL = max(dot(orcaNormal, ORCA_LIGHT_FRONT_DIR), 0.0);\n"
        "  orcaIntensity.x += orcaNdotL * ORCA_LIGHT_FRONT_DIFFUSE;\n"
        "  vec3 orcaBaseColor = orcaUniformColor.rgb;\n"
        "  float orcaAlpha = orcaUniformColor.a;\n"
        "  vec3 orcaPvMin = vec3(0.0);\n"
        "  vec3 orcaPvMax = vec3(0.0);\n"
        "  if (orcaPrintVolumeType == 0) {\n"
        "    orcaPvMin = orcaWorldPosVSOutput.xyz - vec3(orcaPrintVolumeXYData.x, orcaPrintVolumeXYData.y, orcaPrintVolumeZData.x);\n"
        "    orcaPvMax = orcaWorldPosVSOutput.xyz - vec3(orcaPrintVolumeXYData.z, orcaPrintVolumeXYData.w, orcaPrintVolumeZData.y);\n"
        "  } else if (orcaPrintVolumeType == 1) {\n"
        "    float orcaDeltaRadius = orcaPrintVolumeXYData.z - distance(orcaWorldPosVSOutput.xy, orcaPrintVolumeXYData.xy);\n"
        "    orcaPvMin = vec3(orcaDeltaRadius, 0.0, orcaWorldPosVSOutput.z - orcaPrintVolumeZData.x);\n"
        "    orcaPvMax = vec3(0.0, 0.0, orcaWorldPosVSOutput.z - orcaPrintVolumeZData.y);\n"
        "  }\n"
        "  if (any(lessThan(orcaPvMin, vec3(0.0))) || any(greaterThan(orcaPvMax, vec3(0.0)))) {\n"
        "    orcaBaseColor = mix(orcaBaseColor, vec3(1.0), 0.3333);\n"
        "  }\n"
        "  gl_FragData[0] = vec4(vec3(orcaIntensity.y) + orcaBaseColor * (orcaIntensity.x + orcaEmissionFactor), orcaAlpha);\n",
        false);

    // Orca Bed3D::render_model sets print_volume.type=-1 for the normal
    // single shared bed path. Rectangle/circle checks are only enabled for
    // multi-extruder shared-volume rendering.
    shaderProperty->GetFragmentCustomUniforms()->SetUniform4f("orcaUniformColor", uniformColor);
    shaderProperty->GetFragmentCustomUniforms()->SetUniformf("orcaEmissionFactor", 0.0f);
    shaderProperty->GetFragmentCustomUniforms()->SetUniformi("orcaPrintVolumeType", -1);
    shaderProperty->GetFragmentCustomUniforms()->SetUniform4f("orcaPrintVolumeXYData", xyData);
    shaderProperty->GetFragmentCustomUniforms()->SetUniform2f("orcaPrintVolumeZData", zData);
}

vtkSmartPointer<vtkActor> makeTubeAxis(const double start[3], const double end[3], const double color[3], double radius) {
    auto line = vtkSmartPointer<vtkLineSource>::New();
    line->SetPoint1(start);
    line->SetPoint2(end);

    auto tube = vtkSmartPointer<vtkTubeFilter>::New();
    tube->SetInputConnection(line->GetOutputPort());
    tube->SetRadius(radius);
    tube->SetNumberOfSides(16);
    tube->CappingOn();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(tube->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color[0], color[1], color[2]);
    actor->GetProperty()->LightingOff();
    actor->SetPickable(false);
    return actor;
}

vtkSmartPointer<vtkActor> makeConeTip(const double center[3], const double direction[3], const double color[3],
                                      double radius, double height) {
    auto cone = vtkSmartPointer<vtkConeSource>::New();
    cone->SetCenter(center);
    cone->SetDirection(direction);
    cone->SetRadius(radius);
    cone->SetHeight(height);
    cone->SetResolution(24);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(cone->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color[0], color[1], color[2]);
    actor->GetProperty()->LightingOff();
    actor->SetPickable(false);
    return actor;
}
}

bool PrintBedDBSync::createFlashForgeBedModel(const PrintBedDB& printBedDB) {
    const std::string modelResource = resolveBedModelResource(printBedDB);
    const auto modelPath = findResourceFile(modelResource, printBedDB.getBedVendor());
    if (modelPath.empty()) {
        LOG_WARN("PrintBedDBSync::createFlashForgeBedModel - FlashForge bed STL not found: {} (printerModel='{}')",
                 modelResource, printBedDB.getBedPrinterModel());
        return false;
    }

    m_bedModelReader = vtkSmartPointer<vtkSTLReader>::New();
    const std::string modelPathString = modelPath.string();
    m_bedModelReader->SetFileName(modelPathString.c_str());
    m_bedModelReader->Update();

    auto polyData = m_bedModelReader->GetOutput();
    if (!validateGeometry(polyData, "FlashForge bed STL")) {
        m_bedModelReader = nullptr;
        return false;
    }

    m_bedModelTransform = vtkSmartPointer<vtkTransform>::New();
    m_bedModelTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    m_bedModelTransformFilter->SetInputConnection(m_bedModelReader->GetOutputPort());
    m_bedModelTransformFilter->SetTransform(m_bedModelTransform);

    updateBedVisualGeometry(printBedDB);
    m_bedModelTransformFilter->Update();

    // STLReader supplies geometry without normals. Use explicit face normals
    // instead of the fragment derivative fallback at the bed's thin edges.
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputConnection(m_bedModelTransformFilter->GetOutputPort());
    normals->ComputePointNormalsOff();
    normals->ComputeCellNormalsOn();
    normals->ConsistencyOff();
    normals->AutoOrientNormalsOff();
    normals->SplittingOff();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(normals->GetOutputPort());

    if (!m_actor) m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(mapper);
    configureFlashForgeBedProperty(m_actor->GetProperty());
    configureOrcaHotbedShader(m_actor, printBedDB);
    m_actor->SetPickable(false);
    m_source = m_bedModelTransformFilter;

    double rawBounds[6] = {0, 0, 0, 0, 0, 0};
    double transformedBounds[6] = {0, 0, 0, 0, 0, 0};
    polyData->GetBounds(rawBounds);
    m_bedModelTransformFilter->GetOutput()->GetBounds(transformedBounds);
    LOG_INFO("PrintBedDBSync::createFlashForgeBedModel - Loaded Orca bed STL '{}' for printerModel='{}' rawBounds X[{:.2f},{:.2f}] Y[{:.2f},{:.2f}] Z[{:.2f},{:.2f}] transformedBounds X[{:.2f},{:.2f}] Y[{:.2f},{:.2f}] Z[{:.2f},{:.2f}]",
             modelPathString, printBedDB.getBedPrinterModel(),
             rawBounds[0], rawBounds[1], rawBounds[2], rawBounds[3], rawBounds[4], rawBounds[5],
             transformedBounds[0], transformedBounds[1], transformedBounds[2], transformedBounds[3], transformedBounds[4], transformedBounds[5]);
    return true;
}

void PrintBedDBSync::updateBedVisualGeometry(const PrintBedDB& printBedDB) {
    const Vector3 center = printBedDB.getCenter();

    if (m_bedModelReader && m_bedModelTransform) {
        // Orca Bed3D::update_model_offset keeps the STL model's authored
        // dimensions and moves its local origin to the printable-area center.
        m_bedModelTransform->Identity();
        m_bedModelTransform->Translate(center.x, center.y, kOrcaBedModelZOffset);
        m_bedModelTransform->Modified();
    }
}

void PrintBedDBSync::createActor() {
    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::createActor - Failed to cast to PrintBedDB");
        return;
    }

    LOG_DEBUG("PrintBedDBSync::createActor - Creating Orca-style print bed actors for ID: {}", m_id.toString());

    const bool actorVisible = printBedDB->getVisible();
    if (m_actor) m_actor->SetShaderProperty(vtkSmartPointer<vtkShaderProperty>::New());

    // ========== 1. 创建平台 Actor（基类 m_actor） ==========
    const bool hasFlashForgeModel = createFlashForgeBedModel(*printBedDB);
    if (hasFlashForgeModel) {
        // The real bed owns its full visual projection, including its texture
        // and grid decorations.
    }

    if (!hasFlashForgeModel) {
        m_platformSource = vtkSmartPointer<vtkPlaneSource>::New();

        Vector3 center = printBedDB->getCenter();
        float width = printBedDB->getWidth();
        float height = printBedDB->getHeight();
        const double halfW = width * 0.5;
        const double halfH = height * 0.5;
        const double bedZ = -printBedDB->getThickness() * 0.5;

        m_platformSource->SetOrigin(center.x - halfW, center.y - halfH, bedZ);
        m_platformSource->SetPoint1(center.x + halfW, center.y - halfH, bedZ);
        m_platformSource->SetPoint2(center.x - halfW, center.y + halfH, bedZ);
        m_platformSource->SetXResolution(1);
        m_platformSource->SetYResolution(1);
        m_platformSource->Update();

        auto platformMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        platformMapper->SetInputConnection(m_platformSource->GetOutputPort());

        if (!m_actor) m_actor = vtkSmartPointer<vtkActor>::New();
        m_actor->SetMapper(platformMapper);
        configureBedProperty(m_actor->GetProperty());
        m_source = m_platformSource;

        LOG_DEBUG("PrintBedDBSync::createActor - Fallback platform actor created: {}x{}mm at Z={}",
                  width, height, bedZ);
    }

    // Draw the surface before its grid, including equal-depth edge fragments.
    // Keep this actor's position when a resource reload replaces its mapper.
    if (m_vtkRenderer) m_vtkRenderer->AddActor(m_actor);
    rebuildBedDecorations(*printBedDB);


    // 创建可打印区域盒子Actor（根据ShowBounds属性决定是否显示）
    if (printBedDB->getShowBounds()) {
        // LOG_INFO("PrintBedDBSync::createActor - Creating print volume actor (ShowBounds=true)");
        auto printVolumeGeometry = createPrintVolumeGeometry();
        if (printVolumeGeometry) {
            // LOG_INFO("PrintBedDBSync::createActor - Print volume geometry created successfully");
            // 为可打印区域创建独立的映射器和Actor
            auto printVolumeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            printVolumeMapper->SetInputData(printVolumeGeometry);

            m_printVolumeActor = vtkSmartPointer<vtkActor>::New();
            m_printVolumeActor->SetMapper(printVolumeMapper);
            m_printVolumeActor->SetVisibility(actorVisible ? 1 : 0);

            // 设置可打印区域属性：光墙效果
            auto printVolumeProperty = m_printVolumeActor->GetProperty();
            printVolumeProperty->SetColor(0.8f, 0.8f, 0.8f);
            printVolumeProperty->SetOpacity(0.08f);
            printVolumeProperty->SetRepresentationToWireframe();
            printVolumeProperty->BackfaceCullingOff();         // 关闭背面剔除，双面都可见
            printVolumeProperty->SetAmbient(1.0f);
            printVolumeProperty->SetDiffuse(0.0f);
            printVolumeProperty->SetSpecular(0.0f);
            printVolumeProperty->SetEdgeVisibility(false);
            printVolumeProperty->SetLineWidth(1.0f);
            printVolumeProperty->LightingOff();

            // 设置光墙不可被 pick，避免干扰内部模型的选择
            m_printVolumeActor->SetPickable(false);

            // 立即添加可打印区域 Actor 到渲染器（我们已经在渲染线程中）
            if (m_vtkRenderer) {
                m_vtkRenderer->AddActor(m_printVolumeActor);
                LOG_DEBUG("PrintBedDBSync::createActor - Print volume actor created and added with light wall effect (pickable=false)");
            } else {
                LOG_ERROR("PrintBedDBSync::createActor - Cannot add print volume actor: renderer not available");
            }
        } else {
            LOG_ERROR("PrintBedDBSync::createActor - Failed to create print volume geometry!");
            m_printVolumeActor = nullptr;
        }
    } else {
        LOG_DEBUG("PrintBedDBSync::createActor - Print volume disabled (ShowBounds=false)");
        m_printVolumeActor = nullptr;
    }

    syncAuxiliaryTransforms();
    LOG_DEBUG("PrintBedDBSync::createActor - Created Orca Bed3D platform actors for print bed");
}

void PrintBedDBSync::syncAuxiliaryTransforms() {
    const auto bed = documentDB<PrintBedDB>(m_id);
    if (!bed) return;
    const auto source = bed->getTransformMatrix();
    auto matrix = vtkSmartPointer<vtkMatrix4x4>::New();
    for (int row=0;row<4;++row)
        for (int column=0;column<4;++column)
            matrix->SetElement(row,column,source(row,column));
    const auto apply = [&](vtkActor* actor) { if (actor) actor->SetUserMatrix(matrix); };
    for (const auto& actor : {m_gridActor,m_majorGridActor,m_bedTextureActor,m_printVolumeActor}) apply(actor);
    for (const auto& actor : m_axisActors) apply(actor);
    for (const auto& actor : m_collisionWallActors) apply(actor);
    for (const auto& actor : m_collisionWallBorderActors) apply(actor);
}

void PrintBedDBSync::onTransformChanged(const std::string&) {
    // ActorDBSync moves the primary bed. Its independent decorations must
    // consume the same local-to-scene transform on this render thread.
    syncAuxiliaryTransforms();
}

void PrintBedDBSync::initialize(const DBInstanceID& id,
                                vtkSmartPointer<vtkRenderer> renderer,
                                std::shared_ptr<RenderThread> renderThread) {
    // 调用基类方法，初始化平台 Actor (m_actor)
    // 基类会在渲染线程中调用 createActor()，在那里会创建并添加所有 Actors
    ActorDBSync::initialize(id, renderer, renderThread);

    // 注意：附属 Actor 和可打印区域 Actor 的添加已经在 createActor() 中完成
    // 这里不需要再次添加，避免竞态条件
}

void PrintBedDBSync::cleanup(const DBInstanceID& id) {
    // 清理碰撞墙面
    clearCollisionWalls();
    clearBedDecorations();

    // ========== 移除附属 Actor ==========
    if (!m_axisActors.empty() && m_vtkRenderer && m_renderThread) {
        auto self = shared_from_this();
        auto axisActors = m_axisActors;
        auto renderer = m_vtkRenderer;
        dispatchRenderCommand([self, axisActors, renderer](auto*, auto*) {
            for (const auto& axisActor : axisActors) {
                if (axisActor && renderer) {
                    renderer->RemoveActor(axisActor);
                }
            }
        });
        m_axisActors.clear();
    }

    // ========== 移除可打印区域 Actor ==========
    if (m_printVolumeActor && m_vtkRenderer && m_renderThread) {
        auto self = shared_from_this();
        auto printVolumeActor = m_printVolumeActor;
        auto renderer = m_vtkRenderer;
        dispatchRenderCommand([self, printVolumeActor, renderer](auto*, auto*) {
            if (printVolumeActor && renderer) {
                renderer->RemoveActor(printVolumeActor);
                LOG_DEBUG("PrintBedDBSync::cleanup - Print volume actor removed from renderer");
            }
        });
        m_printVolumeActor = nullptr;
    }

    // 调用基类方法，清理平台 Actor (m_actor)
    ActorDBSync::cleanup(id);
}

void PrintBedDBSync::updateGeometry( const std::string& prop) {
    // LOG_INFO("PrintBedDBSync::updateGeometry - Property: '{}', DB ID: {}", prop, db->getDBInstanceID().toString());

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::updateGeometry - Failed to cast to PrintBedDB");
        return;
    }

    if (!m_renderThread) {
        LOG_WARN("PrintBedDBSync::updateGeometry - No render thread available");
        return;
    }

    // 选择性几何更新优化
    UpdateType updateType = determineUpdateType(prop);
    LOG_DEBUG("PrintBedDBSync::updateGeometry - Property: '{}', UpdateType: {}",
              prop, static_cast<int>(updateType));

    switch (updateType) {
        case UpdateType::PLATFORM_ONLY:
            updatePlatformGeometry();
            break;
        case UpdateType::PRINT_VOLUME_ONLY:
            updatePrintVolumeGeometry();
            break;
        case UpdateType::COLLISION_ONLY:
            updateCollisionWalls();
            break;
        case UpdateType::BED_RESOURCE_RELOAD:
            reloadBedVisualResources();
            break;
        case UpdateType::FULL_REBUILD:
        default:
            // LOG_INFO("PrintBedDBSync::updateGeometry - Performing full rebuild for property: '{}'", prop);
            updatePrintBedGeometry();
            // 也需要更新碰撞墙面（在完全重建时）
            updateCollisionWalls();
            break;
    }
}

bool PrintBedDBSync::isGeometryProperty(std::string_view prop) const {
    return prop == "Length" || prop == "Width" || prop == "Thickness" ||
        prop == "Origin" || prop == "Center" || prop == "Selected" ||
        prop == "TemplateDBId" || prop == "PrintHeight" ||
        prop == "ShowBounds" || prop == "ShowGrid" ||
        prop == "GridSpacing" || prop == "MajorGridSpacing" ||
        prop == "GridColor" || prop == "SelectedGridColor" ||
        prop == "BoundColor" || prop == "LogoTexturePath" ||
        prop == "BedModelPath" || prop == "BedTexturePath" ||
        prop == "BedVendor" || prop == "BedPrinterModel" ||
        prop == "CollisionMask";
}

void PrintBedDBSync::reloadBedVisualResources() {
    if (!m_renderThread || !m_vtkRenderer) {
        LOG_WARN("PrintBedDBSync::reloadBedVisualResources - Renderer is not ready");
        return;
    }
    if (!m_actor) {
        // Initial DB property replay happens before the first actor is built.
        // createActor() will already read the latest resource path, so queuing
        // another rebuild here would create the bed twice during startup.
        LOG_DEBUG("PrintBedDBSync::reloadBedVisualResources - Initial actor is not ready; first build will use the current resource");
        return;
    }

    auto self = std::static_pointer_cast<PrintBedDBSync>(shared_from_this());
    dispatchRenderCommand([self](auto*, auto*) {
        auto printBedDB = self->documentDB<PrintBedDB>(self->m_id);
        if (!printBedDB || !self->m_vtkRenderer) {
            return;
        }

        if (self->m_printVolumeActor) {
            self->m_vtkRenderer->RemoveActor(self->m_printVolumeActor);
        }

        self->m_source = nullptr;
        self->m_platformSource = nullptr;
        self->m_bedModelReader = nullptr;
        self->m_bedModelTransform = nullptr;
        self->m_bedModelTransformFilter = nullptr;
        self->m_printVolumeActor = nullptr;

        self->createActor();
        if (self->m_actor) {
            self->m_vtkRenderer->AddActor(self->m_actor);
            self->applyTransformFromDB();
            self->m_actor->SetVisibility(printBedDB->getVisible() ? 1 : 0);
            self->m_actor->SetPickable(false);
        }
        LOG_INFO("PrintBedDBSync::reloadBedVisualResources - Reloaded bed model '{}' for machine '{}'",
                 printBedDB->getBedModelPath(), printBedDB->getBedPrinterModel());
    });
}

void PrintBedDBSync::onMaterialChanged( const std::string& prop) {
    if (!m_renderThread) {
        return;
    }

    LOG_DEBUG("PrintBedDBSync::onMaterialChanged - Material changed: {}", prop);

    auto self = std::static_pointer_cast<PrintBedDBSync>(shared_from_this());
    dispatchRenderCommand([self](auto*, auto*) {
        auto printBedDB = self->documentDB<PrintBedDB>(self->m_id);
        if (!printBedDB || !self->m_actor) {
            return;
        }

        if (self->m_bedModelTransformFilter) {
            configureFlashForgeBedProperty(self->m_actor->GetProperty());
            configureOrcaHotbedShader(self->m_actor, *printBedDB);
        } else {
            configureBedProperty(self->m_actor->GetProperty());
        }
    });
}

void PrintBedDBSync::onRenderingContextApplied(const RenderingContext& context) {
    if (!m_actor)
        return;

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        return;
    }

    LOG_DEBUG("PrintBedDBSync::onRenderingContextApplied - Applying print bed context");

    // 基类已经处理了标准上下文应用，这里可以添加打印床特有的上下文逻辑

    switch (context.getMode()) {
        case RenderingMode::WIREFRAME:
            m_actor->GetProperty()->SetRepresentationToWireframe();
            break;
        case RenderingMode::SIMPLIFIED:
            break;
        default:
            if (m_bedModelTransformFilter) {
                configureFlashForgeBedProperty(m_actor->GetProperty());
                configureOrcaHotbedShader(m_actor, *printBedDB);
            } else {
                configureBedProperty(m_actor->GetProperty());
            }
            break;
    }
}

void PrintBedDBSync::synchronizeView() {
    const auto bed = documentDB<PrintBedDB>(m_id);
    if (!bed || !m_actor || !m_vtkRenderer) return;
    auto* camera = m_vtkRenderer->GetActiveCamera();
    if (!camera) return;
    const double* forward = camera->GetDirectionOfProjection();
    m_viewPolicy.update({forward[0], forward[1], forward[2]}, bed->getTransformMatrix());
    const bool surfaceVisible = bed->getVisible() && !m_viewPolicy.bottom();
    m_actor->SetVisibility(surfaceVisible);
    if (m_bedTextureActor) m_bedTextureActor->SetVisibility(surfaceVisible);
    for (const auto& grid : {m_gridActor, m_majorGridActor}) {
        if (!grid) continue;
        grid->SetVisibility(bed->getVisible() && bed->getShowGrid());
        grid->GetProperty()->SetOpacity(m_viewPolicy.gridOpacity());
    }
}

void PrintBedDBSync::onVisibilityChanged(bool visible) {
    const int vtkVisible = visible ? 1 : 0;

    for (auto& axisActor : m_axisActors) {
        if (axisActor) {
            axisActor->SetVisibility(vtkVisible);
        }
    }
    if (m_printVolumeActor) {
        m_printVolumeActor->SetVisibility(vtkVisible);
    }
    if (m_bedTextureActor) m_bedTextureActor->SetVisibility(vtkVisible);
    const auto bed = documentDB<PrintBedDB>(m_id);
    const int gridVisible = visible && (!bed || bed->getShowGrid()) ? 1 : 0;
    if (m_gridActor) m_gridActor->SetVisibility(gridVisible);
    if (m_majorGridActor) m_majorGridActor->SetVisibility(gridVisible);
    for (auto& wallActor : m_collisionWallActors) {
        if (wallActor) {
            wallActor->SetVisibility(vtkVisible);
        }
    }
    for (auto& borderActor : m_collisionWallBorderActors) {
        if (borderActor) {
            borderActor->SetVisibility(vtkVisible);
        }
    }
}

void PrintBedDBSync::onOtherChangeType(ChangeType changeType, const std::string& prop) {
    if (changeType == ChangeType::PROPERTY_CHANGED) {
        // 将 PROPERTY_CHANGED 类型的属性变更转发给 updateGeometry
        // 这样就能利用现有的优化更新机制
        LOG_DEBUG("PrintBedDBSync::onOtherChangeType - Processing PROPERTY_CHANGED for prop: {}", prop);
        updateGeometry(prop);
    } else {
        // 其他未处理的类型，记录日志
        LOG_DEBUG("PrintBedDBSync::onOtherChangeType - Unhandled change type: {} for prop: {}",
                  static_cast<int>(changeType), prop);
    }
}

void PrintBedDBSync::updatePrintBedGeometry() {
    if (!m_renderThread) {
        LOG_WARN("PrintBedDBSync::updatePrintBedGeometry - Render thread not initialized");
        return;
    }
    if (!m_platformSource && !m_bedModelTransformFilter) {
        LOG_DEBUG("PrintBedDBSync::updatePrintBedGeometry - Platform actor not created yet, skipping early update");
        return;
    }

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::updatePrintBedGeometry - Failed to get PrintBedDB");
        return;
    }

    auto self = shared_from_this();
    dispatchRenderCommand([self, printBedDB](auto*, auto*) {
        auto printBedSync = std::dynamic_pointer_cast<PrintBedDBSync>(self);
        if (!printBedSync) return;

        // ========== 更新平台几何体 ==========
        Vector3 center = printBedDB->getCenter();
        float width = printBedDB->getWidth();
        float height = printBedDB->getHeight();
        float thickness = printBedDB->getThickness();
        const double halfW = width * 0.5;
        const double halfH = height * 0.5;
        const double bedZ = -thickness * 0.5;

        printBedSync->updateBedVisualGeometry(*printBedDB);
        if (printBedSync->m_platformSource) {
            printBedSync->m_platformSource->SetOrigin(center.x - halfW, center.y - halfH, bedZ);
            printBedSync->m_platformSource->SetPoint1(center.x + halfW, center.y - halfH, bedZ);
            printBedSync->m_platformSource->SetPoint2(center.x - halfW, center.y + halfH, bedZ);
            printBedSync->m_platformSource->Update();
        }
        if (printBedSync->m_actor) {
            if (printBedSync->m_bedModelTransformFilter) {
                configureFlashForgeBedProperty(printBedSync->m_actor->GetProperty());
                configureOrcaHotbedShader(printBedSync->m_actor, *printBedDB);
            } else {
                configureBedProperty(printBedSync->m_actor->GetProperty());
            }
        }
        // ========== 更新可打印区域 Actor ==========
        if (printBedDB->getShowBounds()) {
            auto printVolumeGeometry = printBedSync->createPrintVolumeGeometry();
            if (printBedSync->validateGeometry(printVolumeGeometry, "print volume")) {
                if (!printBedSync->m_printVolumeActor) {
                    // 创建新的可打印区域Actor
                    auto printVolumeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                    printVolumeMapper->SetInputData(printVolumeGeometry);

                    printBedSync->m_printVolumeActor = vtkSmartPointer<vtkActor>::New();
                    printBedSync->m_printVolumeActor->SetMapper(printVolumeMapper);
                    printBedSync->m_printVolumeActor->SetVisibility(printBedDB->getVisible() ? 1 : 0);

                    // 设置可打印区域属性：光墙效果
                    auto printVolumeProperty = printBedSync->m_printVolumeActor->GetProperty();
                    printVolumeProperty->SetColor(0.8f, 0.8f, 0.8f);
                    // 使用固定透明度，不受 Material::Opacity 影响
                    printVolumeProperty->SetOpacity(0.08f);
                    printVolumeProperty->SetRepresentationToWireframe();
                    printVolumeProperty->BackfaceCullingOff();
                    printVolumeProperty->SetAmbient(1.0f);
                    printVolumeProperty->SetDiffuse(0.0f);
                    printVolumeProperty->SetSpecular(0.0f);
                    printVolumeProperty->SetEdgeVisibility(false);
                    printVolumeProperty->SetLineWidth(1.0f);
                    printVolumeProperty->LightingOff();
                    printBedSync->m_printVolumeActor->SetPickable(false);

                    if (printBedSync->m_vtkRenderer) {
                        printBedSync->m_vtkRenderer->AddActor(printBedSync->m_printVolumeActor);
                    }
                } else {
                    // 更新现有可打印区域Actor的几何体和属性
                    auto mapper = vtkPolyDataMapper::SafeDownCast(printBedSync->m_printVolumeActor->GetMapper());
                    if (mapper) {
                        mapper->SetInputData(printVolumeGeometry);
                    }
                    // 更新透明度 - 使用固定透明度，不受 Material::Opacity 影响
                    printBedSync->m_printVolumeActor->GetProperty()->SetOpacity(0.08f);
                }
            }
        } else {
            // 从渲染器中移除可打印区域Actor
            if (printBedSync->m_printVolumeActor && printBedSync->m_vtkRenderer) {
                printBedSync->m_vtkRenderer->RemoveActor(printBedSync->m_printVolumeActor);
                printBedSync->m_printVolumeActor = nullptr;
            }
        }

        printBedSync->rebuildBedDecorations(*printBedDB);

        LOG_DEBUG("PrintBedDBSync::updatePrintBedGeometry - Updated geometry: {}x{}x{}mm at ({},{},{})",
                  printBedDB->getWidth(), printBedDB->getHeight(), printBedDB->getThickness(),
                  printBedDB->getCenter().x, printBedDB->getCenter().y, printBedDB->getCenter().z);
        printBedSync->syncAuxiliaryTransforms();
    });
}

vtkSmartPointer<vtkPolyData> PrintBedDBSync::createPlatformGeometry() {
    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::createPlatformGeometry - Failed to get PrintBedDB");
        return nullptr;
    }

    auto planeSource = vtkSmartPointer<vtkPlaneSource>::New();

    Vector3 center = printBedDB->getCenter();
    float width = printBedDB->getWidth();
    float height = printBedDB->getHeight();
    float thickness = printBedDB->getThickness();
    const double halfW = width * 0.5;
    const double halfH = height * 0.5;
    const double bedZ = -thickness * 0.5;

    planeSource->SetOrigin(center.x - halfW, center.y - halfH, bedZ);
    planeSource->SetPoint1(center.x + halfW, center.y - halfH, bedZ);
    planeSource->SetPoint2(center.x - halfW, center.y + halfH, bedZ);
    planeSource->SetXResolution(1);
    planeSource->SetYResolution(1);
    planeSource->Update();

    return planeSource->GetOutput();
}

std::vector<vtkSmartPointer<vtkActor>> PrintBedDBSync::createAxisActors() {
    std::vector<vtkSmartPointer<vtkActor>> actors;
    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        return actors;
    }

    Vector3 center = printBedDB->getCenter();
    const double halfW = printBedDB->getWidth() * 0.5;
    const double halfH = printBedDB->getHeight() * 0.5;
    const double axisOrigin[3] = {center.x - halfW, center.y - halfH, PrintBedViewDefaults::AXIS_Z};
    // Orca Bed3D::Axes uses smooth_cylinder(16, stem_length / 75, stem_length)
    // and no arrow tip; stem_length = 0.1 * build_volume.max_size().
    const double axisLen = std::max(printBedDB->getWidth(), printBedDB->getHeight()) * 0.10;
    const double tubeRadius = axisLen / 75.0;
    const double xStemEnd[3] = {axisOrigin[0] + axisLen, axisOrigin[1], axisOrigin[2]};
    const double yStemEnd[3] = {axisOrigin[0], axisOrigin[1] + axisLen, axisOrigin[2]};
    const double zStemEnd[3] = {axisOrigin[0], axisOrigin[1], axisOrigin[2] + axisLen};

    actors.push_back(makeTubeAxis(axisOrigin, xStemEnd, kAxisX, tubeRadius));
    actors.push_back(makeTubeAxis(axisOrigin, yStemEnd, kAxisY, tubeRadius));
    actors.push_back(makeTubeAxis(axisOrigin, zStemEnd, kAxisZ, tubeRadius));
    return actors;
}


vtkSmartPointer<vtkPolyData> PrintBedDBSync::createPrintVolumeGeometry() {
    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::createPrintVolumeGeometry - Failed to get PrintBedDB");
        return nullptr;
    }

    // 创建可打印区域盒子 - 在Z=0以上的半透明盒子
    auto cubeSource = vtkSmartPointer<vtkCubeSource>::New();

    Vector3 center = printBedDB->getCenter();
    float width = printBedDB->getWidth();
    float height = printBedDB->getHeight();
    float printHeight = printBedDB->getPrintHeight();

    // 可打印区域位置：从 Z=0 到 Z=printHeight
    float volumeCenterZ = printHeight / 2.0f;

    cubeSource->SetXLength(width);
    cubeSource->SetYLength(height);
    cubeSource->SetZLength(printHeight);
    cubeSource->SetCenter(center.x, center.y, volumeCenterZ);
    cubeSource->Update();

    return cubeSource->GetOutput();
}

// === 优化方法实现 ===

PrintBedDBSync::UpdateType PrintBedDBSync::determineUpdateType(const std::string& prop) {
    if (prop.empty()) {
        return UpdateType::FULL_REBUILD;
    }

    // 平台厚度只影响平台自身
    if (prop == "Thickness") {
        return UpdateType::PLATFORM_ONLY;
    }

    // 尺寸/中心会影响平台、网格、可打印区域以及碰撞墙，必须联动刷新
    if (prop == "Width" || prop == "Height" || prop == "Center") {
        return UpdateType::FULL_REBUILD;
    }

    // 可打印区域相关属性
    if (prop == "ShowBounds" || prop == "PrintHeight") {
        return UpdateType::PRINT_VOLUME_ONLY;
    }

    if (prop == "BedModelPath" || prop == "BedTexturePath" ||
        prop == "TemplateDBId") {
        return UpdateType::BED_RESOURCE_RELOAD;
    }

    if (prop == "CollisionMask") {
        return UpdateType::COLLISION_ONLY;
    }

    // 影响多个组件的属性
    if (prop == "Opacity") {
        return UpdateType::FULL_REBUILD;
    }

    // 未知属性，进行完全重建以确保安全
    return UpdateType::FULL_REBUILD;
}

void PrintBedDBSync::updatePlatformGeometry() {
    // LOG_INFO("PrintBedDBSync::updatePlatformGeometry - Optimized platform-only update");

    if (!m_renderThread) {
        LOG_WARN("PrintBedDBSync::updatePlatformGeometry - Render thread not initialized");
        return;
    }
    if (!m_platformSource && !m_bedModelTransformFilter) {
        LOG_DEBUG("PrintBedDBSync::updatePlatformGeometry - Platform actor not created yet, skipping early update");
        return;
    }

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::updatePlatformGeometry - Failed to get PrintBedDB");
        return;
    }

    auto self = shared_from_this();
    dispatchRenderCommand([self, printBedDB](auto*, auto*) {
        auto printBedSync = std::dynamic_pointer_cast<PrintBedDBSync>(self);
        if (!printBedSync) return;

        Vector3 center = printBedDB->getCenter();
        float width = printBedDB->getWidth();
        float height = printBedDB->getHeight();
        float thickness = printBedDB->getThickness();
        const double halfW = width * 0.5;
        const double halfH = height * 0.5;
        const double bedZ = -thickness * 0.5;

        printBedSync->updateBedVisualGeometry(*printBedDB);
        if (printBedSync->m_platformSource) {
            printBedSync->m_platformSource->SetOrigin(center.x - halfW, center.y - halfH, bedZ);
            printBedSync->m_platformSource->SetPoint1(center.x + halfW, center.y - halfH, bedZ);
            printBedSync->m_platformSource->SetPoint2(center.x - halfW, center.y + halfH, bedZ);
            printBedSync->m_platformSource->Update();
        }
        if (printBedSync->m_actor) {
            if (printBedSync->m_bedModelTransformFilter) {
                configureFlashForgeBedProperty(printBedSync->m_actor->GetProperty());
            } else {
                configureBedProperty(printBedSync->m_actor->GetProperty());
            }
        }

        LOG_DEBUG("PrintBedDBSync::updatePlatformGeometry - Platform updated: {}x{}x{}mm at Z={}",
                  width, height, thickness, bedZ);
        printBedSync->syncAuxiliaryTransforms();
    });
}

void PrintBedDBSync::updatePrintVolumeGeometry() {
    // LOG_INFO("PrintBedDBSync::updatePrintVolumeGeometry - Optimized print volume-only update");

    if (!m_renderThread) {
        LOG_WARN("PrintBedDBSync::updatePrintVolumeGeometry - No render thread available");
        return;
    }

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::updatePrintVolumeGeometry - Failed to get PrintBedDB");
        return;
    }

    auto self = shared_from_this();
    dispatchRenderCommand([self, printBedDB](auto*, auto*) {
        auto printBedSync = std::dynamic_pointer_cast<PrintBedDBSync>(self);
        if (!printBedSync || !printBedSync->m_vtkRenderer) {
            return;
        }

        // 更新可打印区域显示状态
        if (printBedDB->getShowBounds()) {
            auto printVolumeGeometry = printBedSync->createPrintVolumeGeometry();
            if (printBedSync->validateGeometry(printVolumeGeometry, "print volume")) {
                if (!printBedSync->m_printVolumeActor) {
                    // 创建新的可打印区域Actor
                    auto printVolumeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                    printVolumeMapper->SetInputData(printVolumeGeometry);

                    printBedSync->m_printVolumeActor = vtkSmartPointer<vtkActor>::New();
                    printBedSync->m_printVolumeActor->SetMapper(printVolumeMapper);
                    printBedSync->m_printVolumeActor->SetVisibility(printBedDB->getVisible() ? 1 : 0);

                    // 设置光墙效果属性 - 使用常量
                    auto property = printBedSync->m_printVolumeActor->GetProperty();
                    property->SetColor(0.8f, 0.8f, 0.8f);
                    property->SetOpacity(0.08f);
                    property->SetRepresentationToWireframe();
                    property->BackfaceCullingOff();         // 双面可见
                    property->SetAmbient(1.0f);
                    property->SetDiffuse(0.0f);
                    property->SetSpecular(0.0f);
                    property->SetEdgeVisibility(false);
                    property->SetLineWidth(1.0f);
                    property->LightingOff();

                    // 设置光墙不可被 pick，避免干扰内部模型的选择
                    printBedSync->m_printVolumeActor->SetPickable(false);

                    printBedSync->m_vtkRenderer->AddActor(printBedSync->m_printVolumeActor);
                    // LOG_INFO("PrintBedDBSync::updatePrintVolumeGeometry - Print volume actor created and added (pickable=false)");
                } else {
                    // 更新现有Actor的几何体
                    auto mapper = vtkPolyDataMapper::SafeDownCast(printBedSync->m_printVolumeActor->GetMapper());
                    if (mapper) {
                        mapper->SetInputData(printVolumeGeometry);
                        // LOG_INFO("PrintBedDBSync::updatePrintVolumeGeometry - Print volume geometry updated");
                    }
                }
            }
        } else {
            // 隐藏可打印区域
            if (printBedSync->m_printVolumeActor && printBedSync->m_vtkRenderer) {
                printBedSync->m_vtkRenderer->RemoveActor(printBedSync->m_printVolumeActor);
                printBedSync->m_printVolumeActor = nullptr;
                // LOG_INFO("PrintBedDBSync::updatePrintVolumeGeometry - Print volume actor removed");
            }
        }
        printBedSync->syncAuxiliaryTransforms();
    });
}

vtkSmartPointer<vtkPolyData> PrintBedDBSync::createGridGeometry(bool majorOnly) const {
    auto bed = documentDB<PrintBedDB>(m_id);
    auto geometry = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    if (!bed) {
        geometry->SetPoints(points);
        geometry->SetLines(lines);
        return geometry;
    }

    const Vector3 center = bed->getCenter();
    const Vector3 origin = bed->getOrigin();
    const double minX = center.x - bed->getWidth() * 0.5;
    const double maxX = center.x + bed->getWidth() * 0.5;
    const double minY = center.y - bed->getHeight() * 0.5;
    const double maxY = center.y + bed->getHeight() * 0.5;
    const double spacing = std::max(bed->getGridSpacing(), 1.0f);
    constexpr int majorEvery = 5;

    int count = 0;
    for (double x = origin.x; x >= minX - 0.001; x -= spacing, ++count) {
        if (majorOnly == ((count % majorEvery) == 0))
            addGridLine(points, lines, x, minY, x, maxY);
    }
    count = 1;
    for (double x = origin.x + spacing; x <= maxX + 0.001; x += spacing, ++count) {
        if (majorOnly == ((count % majorEvery) == 0))
            addGridLine(points, lines, x, minY, x, maxY);
    }
    count = 0;
    for (double y = origin.y; y >= minY - 0.001; y -= spacing, ++count) {
        if (majorOnly == ((count % majorEvery) == 0))
            addGridLine(points, lines, minX, y, maxX, y);
    }
    count = 1;
    for (double y = origin.y + spacing; y <= maxY + 0.001; y += spacing, ++count) {
        if (majorOnly == ((count % majorEvery) == 0))
            addGridLine(points, lines, minX, y, maxX, y);
    }

    if (!majorOnly) {
        const Polygon2 area = bed->getPrintableArea();
        if (area.size() >= 3) {
            for (std::size_t index = 0; index < area.size(); ++index) {
                const auto& start = area[index];
                const auto& end = area[(index + 1) % area.size()];
                addGridLine(points, lines, start.x, start.y, end.x, end.y);
            }
        }
    }
    geometry->SetPoints(points);
    geometry->SetLines(lines);
    return geometry;
}

void PrintBedDBSync::configureGridActor(vtkActor* actor, bool major) const {
    if (!actor) return;
    const auto bed = documentDB<PrintBedDB>(m_id);
    const Vector3 color = bed ? bed->getGridColor() : Vector3(0.9f, 0.9f, 0.9f);
    auto* property = actor->GetProperty();
    property->SetColor(color.x, color.y, color.z);
    property->SetOpacity(1.0);
    property->SetLineWidth(major ? 2.0 : 1.0);
    property->SetRepresentationToWireframe();
    property->SetAmbient(1.0);
    property->SetDiffuse(0.0);
    property->SetSpecular(0.0);
    property->LightingOff();
}

bool PrintBedDBSync::syncBedTextureActor(const PrintBedDB& bed) {
    const auto path = findResourceFile(bed.getBedTexturePath(), bed.getBedVendor());
    if (path.empty()) {
        if (m_bedTextureActor && m_vtkRenderer) m_vtkRenderer->RemoveActor(m_bedTextureActor);
        m_bedTextureActor = nullptr;
        m_bedTexturePlaneSource = nullptr;
        m_bedTextureMap = nullptr;
        m_bedTexture = nullptr;
        m_bedTexturePath.clear();
        return false;
    }
    if (m_bedTextureActor && m_bedTexturePath == path.string()) {
        updateBedTextureGeometry(bed);
        return true;
    }
    if (m_bedTextureActor && m_vtkRenderer) m_vtkRenderer->RemoveActor(m_bedTextureActor);
    createBedTextureActor(bed, path);
    if (m_bedTextureActor && m_vtkRenderer) m_vtkRenderer->AddActor(m_bedTextureActor);
    syncAuxiliaryTransforms();
    return m_bedTextureActor != nullptr;
}

void PrintBedDBSync::createBedTextureActor(
    const PrintBedDB& bed,
    const std::filesystem::path& texturePath) {
    auto reader = vtkSmartPointer<vtkPNGReader>::New();
    reader->SetFileName(texturePath.string().c_str());
    reader->Update();
    m_bedTexture = vtkSmartPointer<vtkTexture>::New();
    m_bedTexture->SetInputConnection(reader->GetOutputPort());
    m_bedTexture->InterpolateOn();
    m_bedTexture->RepeatOff();
    m_bedTexture->EdgeClampOn();
    m_bedTexturePlaneSource = vtkSmartPointer<vtkPlaneSource>::New();
    m_bedTextureMap = vtkSmartPointer<vtkTextureMapToPlane>::New();
    m_bedTextureMap->SetInputConnection(m_bedTexturePlaneSource->GetOutputPort());
    m_bedTextureMap->AutomaticPlaneGenerationOff();
    updateBedTextureGeometry(bed);
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(m_bedTextureMap->GetOutputPort());
    m_bedTextureActor = vtkSmartPointer<vtkActor>::New();
    m_bedTextureActor->SetMapper(mapper);
    m_bedTextureActor->SetTexture(m_bedTexture);
    configureBedTextureProperty(m_bedTextureActor);
    m_bedTexturePath = texturePath.string();
}

void PrintBedDBSync::updateBedTextureGeometry(const PrintBedDB& bed) {
    const Vector3 center = bed.getCenter();
    const double halfWidth = bed.getWidth() * 0.5;
    const double halfHeight = bed.getHeight() * 0.5;
    if (m_bedTexturePlaneSource) {
        m_bedTexturePlaneSource->SetOrigin(center.x - halfWidth, center.y - halfHeight, kBedTextureZ);
        m_bedTexturePlaneSource->SetPoint1(center.x + halfWidth, center.y - halfHeight, kBedTextureZ);
        m_bedTexturePlaneSource->SetPoint2(center.x - halfWidth, center.y + halfHeight, kBedTextureZ);
        m_bedTexturePlaneSource->Update();
    }
    if (m_bedTextureMap) {
        m_bedTextureMap->SetOrigin(center.x - halfWidth, center.y - halfHeight, kBedTextureZ);
        m_bedTextureMap->SetPoint1(center.x + halfWidth, center.y - halfHeight, kBedTextureZ);
        m_bedTextureMap->SetPoint2(center.x - halfWidth, center.y + halfHeight, kBedTextureZ);
        m_bedTextureMap->Update();
    }
}

void PrintBedDBSync::clearBedDecorations() {
    if (m_vtkRenderer) {
        if (m_gridActor) m_vtkRenderer->RemoveActor(m_gridActor);
        if (m_majorGridActor) m_vtkRenderer->RemoveActor(m_majorGridActor);
        if (m_bedTextureActor) m_vtkRenderer->RemoveActor(m_bedTextureActor);
    }
    m_gridActor = nullptr;
    m_majorGridActor = nullptr;
    m_bedTextureActor = nullptr;
    m_bedTexturePlaneSource = nullptr;
    m_bedTextureMap = nullptr;
    m_bedTexture = nullptr;
    m_bedTexturePath.clear();
}

void PrintBedDBSync::rebuildBedDecorations(const PrintBedDB& bed) {
    const auto axes = createAxisActors();
    if (axes.size() == m_axisActors.size()) {
        for (std::size_t index = 0; index < axes.size(); ++index)
            m_axisActors[index]->SetMapper(axes[index]->GetMapper());
    } else {
        for (const auto& actor : m_axisActors)
            if (m_vtkRenderer) m_vtkRenderer->RemoveActor(actor);
        m_axisActors = axes;
        for (const auto& actor : m_axisActors)
            if (m_vtkRenderer) m_vtkRenderer->AddActor(actor);
    }
    for (const auto& actor : m_axisActors)
        actor->SetVisibility(bed.getVisible() ? 1 : 0);
    syncBedTextureActor(bed);
    const bool gridVisible = bed.getVisible() && bed.getShowGrid();
    // Preserve render order at coincident model/grid fragments when a bed
    // property changes. Replacing these actors made the depth-test winner
    // depend on whether the update arrived before or after model creation.
    const auto updateGrid = [&](vtkSmartPointer<vtkActor>& actor, bool major) {
        auto geometry = createGridGeometry(major);
        if (!actor) {
            actor = makeGridActor(geometry);
            if (m_vtkRenderer) m_vtkRenderer->AddActor(actor);
        } else {
            auto mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
            mapper->SetInputData(geometry);
        }
        configureGridActor(actor, major);
        actor->SetVisibility(gridVisible ? 1 : 0);
    };
    updateGrid(m_gridActor, false);
    updateGrid(m_majorGridActor, true);
    if (m_bedTextureActor) m_bedTextureActor->SetVisibility(bed.getVisible() ? 1 : 0);
    syncAuxiliaryTransforms();
}

bool PrintBedDBSync::validateGeometry(vtkPolyData* geometry, const std::string& component) {
    if (!geometry) {
        LOG_ERROR("PrintBedDBSync::validateGeometry - Null geometry for {}", component);
        return false;
    }

    if (geometry->GetNumberOfPoints() == 0) {
        // 空几何体是正常的（比如没有碰撞数据时），使用 DEBUG 级别
        LOG_DEBUG("PrintBedDBSync::validateGeometry - Empty geometry for {} (no points)", component);
        return false;
    }

    if (geometry->GetNumberOfCells() == 0) {
        LOG_DEBUG("PrintBedDBSync::validateGeometry - Empty geometry for {} (no cells)", component);
        return false;
    }

    LOG_DEBUG("PrintBedDBSync::validateGeometry - Valid {} geometry: {} points, {} cells",
              component, geometry->GetNumberOfPoints(), geometry->GetNumberOfCells());
    return true;
}

// === 碰撞墙面渲染实现 ===

void PrintBedDBSync::updateCollisionWalls() {
    if (!m_vtkRenderer || !m_renderThread) {
        LOG_WARN("PrintBedDBSync::updateCollisionWalls - No renderer or thread available");
        return;
    }

    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB) {
        LOG_ERROR("PrintBedDBSync::updateCollisionWalls - Failed to get PrintBedDB");
        return;
    }

    if (!printBedDB->getCollisionEffectsEnabled()) {
        clearCollisionWalls();
        return;
    }

    auto self = shared_from_this();
    dispatchRenderCommand([self, printBedDB](auto*, auto*) {
        auto printBedSync = std::dynamic_pointer_cast<PrintBedDBSync>(self);
        if (!printBedSync) return;

        static const std::array<std::string, 6> wallNames = {
            "LEFT", "RIGHT", "FRONT", "BACK", "TOP", "BOTTOM"
        };

        const auto syncActor = [printBedSync, printBedDB](
                                   vtkSmartPointer<vtkActor>& actor,
                                   vtkSmartPointer<vtkPolyData> geometry,
                                   bool border) {
            if (!actor) {
                auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
                mapper->SetInputData(geometry);
                actor = vtkSmartPointer<vtkActor>::New();
                actor->SetMapper(mapper);
                actor->SetPickable(false);
                printBedSync->m_vtkRenderer->AddActor(actor);
            } else if (auto* mapper = vtkPolyDataMapper::SafeDownCast(
                           actor->GetMapper())) {
                mapper->SetInputData(geometry);
            }
            actor->SetVisibility(printBedDB->getVisible() ? 1 : 0);
            if (border) {
                printBedSync->applyCollisionWallBorderEffect(actor);
            } else {
                printBedSync->applyCollisionWallEffect(actor);
            }
        };

        for (int i = 0; i < 6; ++i) {
            if (!printBedDB->isWallCollisionSet(i)) {
                if (printBedSync->m_collisionWallActors[i]) {
                    printBedSync->m_vtkRenderer->RemoveActor(
                        printBedSync->m_collisionWallActors[i]);
                    printBedSync->m_collisionWallActors[i] = nullptr;
                }
                if (printBedSync->m_collisionWallBorderActors[i]) {
                    printBedSync->m_vtkRenderer->RemoveActor(
                        printBedSync->m_collisionWallBorderActors[i]);
                    printBedSync->m_collisionWallBorderActors[i] = nullptr;
                }
                continue;
            }

            auto wall = printBedSync->createCollisionWallGeometry(i);
            if (!wall || !printBedSync->validateGeometry(wall, wallNames[i])) {
                continue;
            }
            syncActor(printBedSync->m_collisionWallActors[i], wall, false);

            auto border = printBedSync->createCollisionWallBorderGeometry(wall);
            if (border && printBedSync->validateGeometry(
                              border, wallNames[i] + " border")) {
                syncActor(
                    printBedSync->m_collisionWallBorderActors[i], border, true);
            }
        }

        printBedSync->m_vtkRenderer->Modified();
        printBedSync->syncAuxiliaryTransforms();
    });
}

vtkSmartPointer<vtkPolyData> PrintBedDBSync::createCollisionWallGeometry(int wallIndex) {
    auto printBedDB = documentDB<PrintBedDB>(m_id);
    if (!printBedDB || !printBedDB->isWallCollisionSet(wallIndex)) {
        return vtkSmartPointer<vtkPolyData>::New();
    }

    const Vector3 center = printBedDB->getCenter();
    const Vector3 origin = printBedDB->getOrigin();
    const double minX = center.x - printBedDB->getWidth() * 0.5;
    const double maxX = center.x + printBedDB->getWidth() * 0.5;
    const double minY = center.y - printBedDB->getHeight() * 0.5;
    const double maxY = center.y + printBedDB->getHeight() * 0.5;
    const double minZ = origin.z;
    const double maxZ = origin.z + printBedDB->getPrintHeight();
    const double sideBandHeight = std::clamp(
        std::min(printBedDB->getWidth(), printBedDB->getHeight()) *
            PrintBedViewDefaults::COLLISION_SIDE_BAND_RATIO,
        PrintBedViewDefaults::COLLISION_SIDE_BAND_MIN_HEIGHT_MM,
        PrintBedViewDefaults::COLLISION_SIDE_BAND_MAX_HEIGHT_MM);
    const double sideMaxZ = std::min(maxZ, minZ + sideBandHeight);

    auto planeSource = vtkSmartPointer<vtkPlaneSource>::New();
    switch (wallIndex) {
        case 0: // LEFT
            planeSource->SetOrigin(minX, minY, minZ);
            planeSource->SetPoint1(minX, maxY, minZ);
            planeSource->SetPoint2(minX, minY, sideMaxZ);
            break;
        case 1: // RIGHT
            planeSource->SetOrigin(maxX, maxY, minZ);
            planeSource->SetPoint1(maxX, minY, minZ);
            planeSource->SetPoint2(maxX, maxY, sideMaxZ);
            break;
        case 2: // FRONT
            planeSource->SetOrigin(maxX, minY, minZ);
            planeSource->SetPoint1(minX, minY, minZ);
            planeSource->SetPoint2(maxX, minY, sideMaxZ);
            break;
        case 3: // BACK
            planeSource->SetOrigin(minX, maxY, minZ);
            planeSource->SetPoint1(maxX, maxY, minZ);
            planeSource->SetPoint2(minX, maxY, sideMaxZ);
            break;
        case 4: // TOP
            planeSource->SetOrigin(minX, minY, maxZ);
            planeSource->SetPoint1(maxX, minY, maxZ);
            planeSource->SetPoint2(minX, maxY, maxZ);
            break;
        case 5: // BOTTOM
            planeSource->SetOrigin(minX, maxY, minZ);
            planeSource->SetPoint1(maxX, maxY, minZ);
            planeSource->SetPoint2(minX, minY, minZ);
            break;
        default:
            return vtkSmartPointer<vtkPolyData>::New();
    }

    planeSource->Update();
    return planeSource->GetOutput();
}

vtkSmartPointer<vtkPolyData> PrintBedDBSync::createCollisionWallBorderGeometry(
    vtkPolyData* wallGeometry) {
    if (!wallGeometry) return vtkSmartPointer<vtkPolyData>::New();

    auto edges = vtkSmartPointer<vtkFeatureEdges>::New();
    edges->SetInputData(wallGeometry);
    edges->BoundaryEdgesOn();
    edges->FeatureEdgesOff();
    edges->ManifoldEdgesOff();
    edges->NonManifoldEdgesOff();

    auto tube = vtkSmartPointer<vtkTubeFilter>::New();
    tube->SetInputConnection(edges->GetOutputPort());
    tube->SetRadius(PrintBedViewDefaults::COLLISION_BORDER_WIDTH_MM * 0.5f);
    tube->SetNumberOfSides(8);
    tube->CappingOn();
    tube->Update();
    return tube->GetOutput();
}

void PrintBedDBSync::applyCollisionWallEffect(vtkSmartPointer<vtkActor> actor) {
    if (!actor) return;

    auto property = actor->GetProperty();
    property->SetColor(PrintBedViewDefaults::COLLISION_COLOR_R,
                       PrintBedViewDefaults::COLLISION_COLOR_G,
                       PrintBedViewDefaults::COLLISION_COLOR_B);
    property->SetOpacity(PrintBedViewDefaults::COLLISION_FILL_OPACITY);
    property->SetAmbient(1.0f);
    property->SetDiffuse(0.0f);
    property->SetSpecular(0.0f);
    property->BackfaceCullingOff();
    property->SetRepresentationToSurface();
    property->SetEdgeVisibility(false);
    property->LightingOff();
    actor->ForceTranslucentOn();
}

void PrintBedDBSync::applyCollisionWallBorderEffect(
    vtkSmartPointer<vtkActor> actor) {
    if (!actor) return;

    auto* property = actor->GetProperty();
    property->SetColor(PrintBedViewDefaults::COLLISION_COLOR_R,
                       PrintBedViewDefaults::COLLISION_COLOR_G,
                       PrintBedViewDefaults::COLLISION_COLOR_B);
    property->SetOpacity(PrintBedViewDefaults::COLLISION_BORDER_OPACITY);
    property->SetAmbient(1.0f);
    property->SetDiffuse(0.0f);
    property->SetSpecular(0.0f);
    property->LightingOff();
    actor->ForceTranslucentOn();
}

void PrintBedDBSync::clearCollisionWalls() {
    if (!m_vtkRenderer || !m_renderThread) return;

    auto self = shared_from_this();
    dispatchRenderCommand([self](auto*, auto*) {
        auto printBedSync = std::dynamic_pointer_cast<PrintBedDBSync>(self);
        if (!printBedSync) return;

        for (auto& actor : printBedSync->m_collisionWallActors) {
            if (actor) {
                printBedSync->m_vtkRenderer->RemoveActor(actor);
                actor = nullptr;
            }
        }
        for (auto& actor : printBedSync->m_collisionWallBorderActors) {
            if (actor) {
                printBedSync->m_vtkRenderer->RemoveActor(actor);
                actor = nullptr;
            }
        }

        LOG_DEBUG("PrintBedDBSync::clearCollisionWalls - All collision walls cleared");
    });
}

// 静态注册
REGISTER_VTK_DBSYNC(TypeID::PRINT_BED_DB, PrintBedDBSync,
                        GPlatform::Rendering::RendererRoleSets::PrepareAndPreview)
