#pragma once

#include "StandardActionHandler.hpp"
#include <memory>

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
    void createPrimitive(const QString& actionCode, const QVariantMap& params);
    bool executeParametricCommand(const QString& commandType,
                                  const QVariantMap& params,
                                  QVariantMap& outResult,
                                  QString& outError);
    void clearAll();

    // PrintBed property update methods
    void updatePrintBedGrid(const QVariantMap& params);
    void updatePrintBedBounds(const QVariantMap& params);

    void smartFitCamera();
};
