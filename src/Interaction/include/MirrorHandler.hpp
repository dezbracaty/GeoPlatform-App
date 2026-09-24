#pragma once

#include "StandardActionHandler.hpp"
#include "MirrorOperation.hpp"
#include "SelectionBridge.hpp"
#include <DocumentManager.hpp>
#include "ActorDB.hpp"
#include <memory>

/**
 * @brief 镜像操作 Handler
 *
 * 处理模型的镜像操作，支持沿 X、Y、Z 轴镜像
 */
class MirrorHandler : public StandardActionHandler {
    Q_OBJECT
public:
    explicit MirrorHandler(QObject* parent = nullptr);
    virtual ~MirrorHandler();

    // Handler 生命周期
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onEnterForAI(std::shared_ptr<ActionContext> context) override;
    void onExit() override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

private:
    bool executeAIMirror(std::shared_ptr<ActionContext> context);

    /**
     * @brief 解析 action code 并执行镜像操作
     */
    void performMirrorOperation(const QString& actionCode, const QVariantMap& params);

    /**
     * @brief 获取选中的 Actor 列表
     */
    std::vector<std::shared_ptr<ActorDB>> getSelectedActors();

    /**
     * @brief 执行镜像操作
     * @param actors 要镜像的 Actor 列表
     * @param axis 镜像轴向
     * @param keepOriginal 是否保留原始模型
     */
    void executeMirror(const std::vector<std::shared_ptr<ActorDB>>& actors,
                      MirrorOperation::MirrorAxis axis,
                      bool keepOriginal);

    /**
     * @brief 从 action code 解析镜像轴向
     */
    MirrorOperation::MirrorAxis parseAxisFromAction(const QString& actionCode);
};
