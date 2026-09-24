#include "CopyPasteHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(CopyPasteHandler, "edit.copy", "edit.paste")
#include "ActionHandlerRegistry.hpp"
#include <DocumentManager.hpp>
#include "SelectionBridge.hpp"
#include "ViewportInputRouter.hpp"
#include "ViewportCoordinateSystem.hpp"
#include "WindowDB.hpp"
#include "ModelPositionUtil.hpp"
#include "Foundation/Log.h"
#include <ActorDB.hpp>
#include <DBCopyService.hpp>
#include <transdb.h>
#include <QCursor>
#include <QGuiApplication>
#include <QWindow>
#include <exception>

// 静态剪贴板定义
std::vector<DBInstanceID> CopyPasteHandler::s_copiedObjectIds;

CopyPasteHandler::CopyPasteHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void CopyPasteHandler::onEnter(std::shared_ptr<ActionContext> context) {
    StandardActionHandler::onEnter(context);

    QString actionCode = context->getActionCode();

    // LOG_DEBUG("CopyPasteHandler::onEnter() - 动作代码: {}", actionCode.toStdString());

    if (actionCode == "edit.copy") {
        // LOG_DEBUG("执行复制操作");
        copySelectedModels();
    } else if (actionCode == "edit.paste") {
        // LOG_DEBUG("执行粘贴操作");
        pasteModels();
    } else {
        LOG_WARN("Unknown copy-paste action: {}", actionCode.toStdString());
    }
}

void CopyPasteHandler::copySelectedModels() {
    auto* selectionBridge = SelectionBridge::instance();
    if (!selectionBridge) {
        LOG_WARN("SelectionBridge not available");
        return;
    }

    if (!selectionBridge->hasSelection()) {
        LOG_DEBUG("No models selected for copying");
        return;
    }

    // 直接保存选中对象的ID - 就这么简单！
    s_copiedObjectIds = selectionBridge->getSelectedIds();

    LOG_INFO("已复制 {} 个对象到剪贴板", s_copiedObjectIds.size());
}

void CopyPasteHandler::pasteModels() {
    if (s_copiedObjectIds.empty()) {
        LOG_DEBUG("剪贴板为空，无法粘贴");
        return;
    }

    // 获取鼠标光线追踪的世界坐标
    Vector3 targetPosition = getMouseWorldPosition();

    auto* docManager = DocumentManager::instance();
    if (!docManager) {
        LOG_WARN("DocumentManager not available");
        return;
    }

    std::vector<DBInstanceID> sourceRootIds;
    sourceRootIds.reserve(s_copiedObjectIds.size());
    for (const auto& id : s_copiedObjectIds) {
        auto source = docManager->getDBInstance(id);
        if (!source) {
            LOG_WARN("无法找到对象 ID: {}", id.getValue());
            continue;
        }
        if (!std::dynamic_pointer_cast<ActorDB>(source)) {
            LOG_DEBUG("跳过非 ActorDB 对象: {} (ID: {})",
                      source->getDisplayName(), id.getValue());
            continue;
        }
        sourceRootIds.push_back(id);
    }
    if (sourceRootIds.empty()) {
        LOG_DEBUG("剪贴板中没有可粘贴的 ActorDB 对象");
        return;
    }

    LOG_INFO("在位置 ({}, {}, {}) 粘贴 {} 个对象",
             targetPosition.x, targetPosition.y, targetPosition.z,
             sourceRootIds.size());

    // 在事务中执行粘贴操作
    {
        TransactionGuard guard("粘贴对象");
        try {
            DBCopyService copyService(*docManager);
            const DBCopyResult copyResult = copyService.copy(sourceRootIds);
            if (!copyResult.success) {
                LOG_ERROR("复制对象图失败: {}", copyResult.error);
                guard.rollback();
                return;
            }

            for (const auto& newRootId : copyResult.rootIds) {
                auto cloned = std::dynamic_pointer_cast<ActorDB>(
                    docManager->getDBInstance(newRootId));
                if (!cloned) {
                    LOG_ERROR("复制结果根对象 {} 不是 ActorDB", newRootId.getValue());
                    guard.rollback();
                    return;
                }

                cloned->resetRotation();
                cloned->setScaleUniform(1.0f);
                Vector3 adjustedPosition =
                    ModelPositionUtil::adjustPositionToBed(targetPosition, cloned);
                cloned->setPosition(adjustedPosition);
            }

            guard.commit();
            LOG_INFO("粘贴完成，成功创建 {} 个根对象和 {} 个总 DB 节点",
                     copyResult.rootIds.size(), copyResult.idMap.size());
        } catch (const std::exception& error) {
            LOG_ERROR("粘贴对象图时发生异常: {}", error.what());
            guard.rollback();
        } catch (...) {
            LOG_ERROR("粘贴对象图时发生未知异常");
            guard.rollback();
        }
    }
}

Vector3 CopyPasteHandler::getMouseWorldPosition() const {
    // 🔑 新架构：使用 ViewportCoordinateSystem 进行坐标转换

    auto* input = ViewportInputRouter::getInstance();
    const DBInstanceID viewId(input ? input->lastInputViewId() : 0);
    const QPoint windowMousePos = input
        ? input->pointerPosition(viewId.getValue()).toPoint()
        : QPoint();

    // 2. 获取DocumentManager和活动窗口DB
    auto* documentManager = DocumentManager::instance();
    if (!documentManager) {
        LOG_ERROR("DocumentManager不可用");
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    auto windowDB = std::dynamic_pointer_cast<WindowDB>(
        documentManager->getDBInstance(viewId));
    if (!windowDB) {
        LOG_ERROR("没有输入 View 对应的 WindowDB 可用于获取视口信息");
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    // 3. 获取 ViewportCoordinateSystem
    auto coordSystem = windowDB->getCoordinateSystem();
    if (!coordSystem) {
        LOG_ERROR("ViewportCoordinateSystem不可用");
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    // 4. 使用 ViewportCoordinateSystem 生成射线
    const WorldRay ray = coordSystem->rayFromScreen(windowMousePos);

    LOG_DEBUG("ViewportCoordinateSystem: 屏幕坐标 ({}, {}) -> 射线起点 ({:.3f}, {:.3f}, {:.3f}), 方向 ({:.3f}, {:.3f}, {:.3f})",
             windowMousePos.x(), windowMousePos.y(),
             ray.origin.x, ray.origin.y, ray.origin.z,
             ray.direction.x, ray.direction.y, ray.direction.z);

    // 5. 计算射线与打印床平面 (Z = 0) 的交点
    Vector3 intersectionPoint;
    if (ray.direction.z != 0.0f) {
        // 射线与平面的交点计算：rayOrigin + t * rayDirection = 平面上的点
        // 对于 Z = 0 平面：rayOrigin.z + t * rayDirection.z = 0
        // 所以：t = -rayOrigin.z / rayDirection.z
        float t = -ray.origin.z / ray.direction.z;

        if (t >= 0.0f) { // 确保交点在射线正方向上
            intersectionPoint.x = ray.origin.x + t * ray.direction.x;
            intersectionPoint.y = ray.origin.y + t * ray.direction.y;
            intersectionPoint.z = 0.0f; // 打印床表面

            LOG_DEBUG("射线与打印床相交，世界坐标: ({:.3f}, {:.3f}, {:.3f})",
                     intersectionPoint.x, intersectionPoint.y, intersectionPoint.z);
            return intersectionPoint;
        } else {
            LOG_ERROR("射线方向朝向错误方向 (t = {})", t);
        }
    } else {
        LOG_ERROR("射线方向Z分量为0，与平面平行");
    }

    // 射线与打印床平面平行或不相交，返回无效位置
    LOG_ERROR("❌ 射线与打印床平面不相交，无法确定世界坐标");
    return Vector3(0.0f, 0.0f, 0.0f);
}
