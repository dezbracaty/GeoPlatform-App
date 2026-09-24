#pragma once

#include "IActionHandlerBase.hpp"
#include "PickTypes.hpp"
#include "Transform.hpp"
#include <memory>

class TransientPolyDataActorDB;

/**
 * @brief 面片高亮处理器
 *
 * 使用运行时临时几何 Actor 实现面片高亮功能
 * 负责：Pick监听、几何提取、数据更新、生命周期管理
 */
class CellHighlightHandler : public IActionHandlerBase {
    Q_OBJECT

public:
    explicit CellHighlightHandler(QObject* parent = nullptr);
    virtual ~CellHighlightHandler();

    // === IActionHandlerBase接口 ===
    HandlerType getHandlerType() const override { return HandlerType::Background; }

    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    bool onMouseMoveEvent(QMouseEvent* event) override;

private:
    void processPickResult(const PickResult& pickResult);
    void updateCellHighlight(const PickResult& pickResult);

    // 缓存和transform相关方法
    bool isPickResultChanged(const PickResult& newResult) const;
    void updateTransformFromPickedDB(const PickResult& pickResult);


private:
    std::shared_ptr<TransientPolyDataActorDB> m_trianglePreview;
    bool m_isActive = false;

    // 缓存的pick结果，用于检测变化
    PickResult m_cachedPickResult;
    DBInstanceID m_inputView{INVALID_DB_ID};
    std::uint64_t m_pendingPickRequestId{0};

};
