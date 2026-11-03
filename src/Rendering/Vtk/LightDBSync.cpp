#include "LightDBSync.hpp"
#include "VtkDBSyncFactory.hpp"

void LightDBSync::initialize(
    const DBInstanceID& id,
    vtkSmartPointer<vtkRenderer> renderer,
    std::shared_ptr<RenderThread> renderThread) {

    m_id = id;
    m_vtkRenderer = renderer;
    m_renderThread = renderThread;

    auto lightDB = documentDB<LightDB>(id);
    if (!lightDB) {
        LOG_ERROR("LightDBSync::initialize - Invalid LightDB for ID: {}", id.toString());
        return;
    }

    // 🔑 关键区别：创建 vtkLight 并添加到渲染器（管理模式）
    if (m_renderThread) {
        auto self = shared_from_this();
        dispatchRenderCommand([self, id](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            // 创建 VTK 灯光对象
            self->m_light = vtkSmartPointer<vtkLight>::New();

            // 同步所有属性
            self->syncAllProperties();

            // 🔑 关键：使用 AddLight() 添加到渲染器
            if (self->m_vtkRenderer) {
                auto lightDB = self->documentDB<LightDB>(id);
                if (lightDB) {
                    LOG_INFO("💡 LightDBSync: Light '{}' added to renderer for ID: {}",
                             lightDB->getName(), id.toString());
                }
                self->m_vtkRenderer->AddLight(self->m_light);
            }
        });
    }
}

void LightDBSync::cleanup(const DBInstanceID& id) {
    if (m_renderThread && m_light) {
        auto self = shared_from_this();
        dispatchRenderCommand([self, id](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            if (self->m_light && self->m_vtkRenderer) {
                // 🔑 关键：使用 RemoveLight() 从渲染器移除
                self->m_vtkRenderer->RemoveLight(self->m_light);
                LOG_INFO("💡 LightDBSync: Light removed from renderer for ID: {}", id.toString());
            }
            self->m_light = nullptr;
        });
    }

    m_vtkRenderer = nullptr;
    m_renderThread = nullptr;
}

void LightDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const std::string& propertyName) {

    auto lightDB = documentDB<LightDB>(m_id);
    if (!lightDB || !m_light) {
        return;
    }

    LOG_DEBUG("LightDBSync::update - ID: {}, Property: '{}'", id.toString(), propertyName);

    // 根据属性名称执行对应的同步
    if (propertyName == "LightType" || propertyName.empty()) {
        syncLightType();
    }

    if (propertyName == "Color" || propertyName.empty()) {
        syncColor();
    }

    if (propertyName == "Intensity" || propertyName.empty()) {
        syncIntensity();
    }

    if (propertyName == "Enabled" || propertyName.empty()) {
        syncEnabled();
    }

    if (propertyName == "Position" || propertyName.empty()) {
        syncPosition();
    }

    if (propertyName == "FocalPoint" || propertyName.empty()) {
        syncFocalPoint();
    }
}

void LightDBSync::update(
    const DBInstanceID& id,
    ChangeType changeType,
    const trans::Prop& prop) {

    auto lightDB = documentDB<LightDB>(m_id);
    if (!lightDB || !m_light) {
        return;
    }

    LOG_DEBUG("LightDBSync::update - ID: {}, Property: '{}'", id.toString(), prop.name());

    // 使用 PROP_##name() 语法进行属性比较
    if (prop == LightDB::PROP_LightType()) {
        syncLightType();
    } else if (prop == LightDB::PROP_Color()) {
        syncColor();
    } else if (prop == LightDB::PROP_Intensity()) {
        syncIntensity();
    } else if (prop == LightDB::PROP_Enabled()) {
        syncEnabled();
    } else if (prop == LightDB::PROP_Position()) {
        syncPosition();
    } else if (prop == LightDB::PROP_FocalPoint()) {
        syncFocalPoint();
    }
}

void LightDBSync::setVisible(bool visible) {
    if (m_light) {
        dispatchVTKOperation([this, visible]() {
            // vtkLight 使用 Switch 而非 Visibility
            m_light->SetSwitch(visible ? 1 : 0);
        });
    }
}

void LightDBSync::syncLightType() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    dispatchVTKOperation([this, lightDB]() {
        switch (lightDB->getLightTypeEnum()) {
            case LightType::HEADLIGHT:
                m_light->SetLightTypeToHeadlight();
                LOG_DEBUG("💡 LightDBSync: Set as HEADLIGHT");
                break;
            case LightType::CAMERA_LIGHT:
                m_light->SetLightTypeToCameraLight();
                LOG_DEBUG("💡 LightDBSync: Set as CAMERA_LIGHT");
                break;
            case LightType::SCENE_LIGHT:
                m_light->SetLightTypeToSceneLight();
                LOG_DEBUG("💡 LightDBSync: Set as SCENE_LIGHT");
                break;
        }
    });
}

void LightDBSync::syncColor() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    auto color = lightDB->getColor();
    dispatchVTKOperation([this, color]() {
        // vtkLight::SetColor 在某些 VTK 版本只设 diffuse；显式补设 ambient/specular，
        // 否则 vtkProperty::Ambient 会乘上 light.ambient_color=(0,0,0) 而归零，
        // 导致依赖环境光呈现的材质（如打印床）比预期暗很多。
        m_light->SetDiffuseColor(color.x, color.y, color.z);
        m_light->SetAmbientColor(color.x, color.y, color.z);
        m_light->SetSpecularColor(color.x, color.y, color.z);
    });
}

void LightDBSync::syncIntensity() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    float intensity = lightDB->getIntensity();
    dispatchVTKOperation([this, intensity]() {
        m_light->SetIntensity(intensity);
    });
}

void LightDBSync::syncEnabled() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    bool enabled = lightDB->getEnabled();
    dispatchVTKOperation([this, enabled]() {
        m_light->SetSwitch(enabled ? 1 : 0);
    });
}

void LightDBSync::syncPosition() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    auto position = lightDB->getPosition();
    dispatchVTKOperation([this, position]() {
        m_light->SetPosition(position.x, position.y, position.z);
    });
}

void LightDBSync::syncFocalPoint() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    auto focalPoint = lightDB->getFocalPoint();
    dispatchVTKOperation([this, focalPoint]() {
        m_light->SetFocalPoint(focalPoint.x, focalPoint.y, focalPoint.z);
    });
}

void LightDBSync::syncAllProperties() {
    auto lightDB = documentDB<LightDB>(m_id);
    if (!m_light || !lightDB) return;

    syncLightType();
    syncColor();
    syncIntensity();
    syncEnabled();
    syncPosition();
    syncFocalPoint();
}

void LightDBSync::dispatchVTKOperation(std::function<void()> operation) {
    if (m_renderThread) {
        auto self = shared_from_this();
        dispatchRenderCommand([self, operation](vtkGenericOpenGLRenderWindow*, vtkObject*) {
            if (self->m_light) {
                operation();
            }
        });
    }
}

// 注册到工厂
REGISTER_VTK_DBSYNC(TypeID::LIGHT_DB, LightDBSync,
                        GPlatform::Rendering::RendererRoleSets::PrepareAndPreview)
