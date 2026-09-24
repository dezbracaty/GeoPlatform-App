#pragma once

#include "StandardActionHandler.hpp"
#include <memory>

// Forward declarations
struct Vector3;
class ActorDB;

// 统一的几何体创建Handler
class CreateGeometryHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit CreateGeometryHandler(QObject* parent = nullptr);
    virtual ~CreateGeometryHandler() = default;

    // 重写onEnter，在这里执行创建逻辑
    void onEnter(std::shared_ptr<ActionContext> context) override;

protected:
    // AI 描述符静态表
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

private:
    // 创建不同类型的几何体
    // 注意：createPrintBed 已移至 SceneHandler (scene.init action)
    void createSphere(const QVariantMap& params);
    void createCube(const QVariantMap& params);
    void createCone(const QVariantMap& params);
    void createCylinder(const QVariantMap& params);
    bool executeParametricCommand(const QString& commandType,
                                  const QVariantMap& params,
                                  QVariantMap& outResult,
                                  QString& outError);
    void clearAll();

    // PrintBed property update methods
    void updatePrintBedGrid(const QVariantMap& params);
    void updatePrintBedBounds(const QVariantMap& params);

    // 辅助方法：从参数中提取位置
    Vector3 extractPosition(const QVariantMap& params);

    // 辅助方法：设置材质属性
    void setMaterialProperties(std::shared_ptr<ActorDB> actor, const QVariantMap& params, const QString& geometryType);

    // 相机控制方法
    void smartFitCamera();

    // 智能摆放方法：获取所有现有对象并查找可用位置
    Vector3 findAvailablePosition(const Vector3& objectSize, float margin = 10.0f);
};
