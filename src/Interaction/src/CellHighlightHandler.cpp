#include "CellHighlightHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(
    CellHighlightHandler,
    "interaction.cell_highlight",
    "interaction.remove_cell_highlight")
#include "TransientPolyDataActorDB.hpp"
#include "GeometryExtractor.hpp"
#include "InteractionRuntime.hpp"
#include "PickService.hpp"
#include "ActionContext.hpp"
#include "ActionHandlerRegistry.hpp"
#include "Foundation/Log.h"
#include <TransDBTypes.hpp>
#include <DocumentManager.hpp>
#include "ActorDB.hpp"
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <QTimer>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

namespace {
vtkSmartPointer<vtkPolyData> polyDataFromTriangles(
    const std::vector<GeomTriangle>& triangles) {
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (const auto& triangle : triangles) {
        const vtkIdType base = points->GetNumberOfPoints();
        points->InsertNextPoint(triangle.vertex1.x, triangle.vertex1.y,
                                triangle.vertex1.z);
        points->InsertNextPoint(triangle.vertex2.x, triangle.vertex2.y,
                                triangle.vertex2.z);
        points->InsertNextPoint(triangle.vertex3.x, triangle.vertex3.y,
                                triangle.vertex3.z);
        const vtkIdType ids[3]{base, base + 1, base + 2};
        cells->InsertNextCell(3, ids);
    }
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->SetPolys(cells);
    return polyData;
}
}

CellHighlightHandler::CellHighlightHandler(QObject* parent)
    : IActionHandlerBase(parent) {
    auto& picks = interactionPickService();
    connect(&picks, &PickService::pickCompleted,
            this,
            [this](const PickSnapshot& snapshot) {
                if (!m_isActive || snapshot.channel != PickChannel::Cell ||
                    snapshot.viewId != m_inputView ||
                    snapshot.requestId != m_pendingPickRequestId) {
                    return;
                }
                m_pendingPickRequestId = 0;
                if (snapshot.status ==
                        GPlatform::Rendering::PickStatus::Hit ||
                    snapshot.status ==
                        GPlatform::Rendering::PickStatus::Miss) {
                    processPickResult(snapshot.result);
                }
            },
            Qt::AutoConnection);
    LOG_DEBUG("CellHighlightHandler created");
}

CellHighlightHandler::~CellHighlightHandler() {
    LOG_DEBUG("CellHighlightHandler destroyed");
}

void CellHighlightHandler::onEnter(std::shared_ptr<ActionContext> context) {
    // 获取触发的 action code
    QString actionCode = context->getActionCode();
    LOG_DEBUG("CellHighlightHandler::onEnter() - Action: {}", actionCode.toStdString());

    // 如果是移除操作，直接调用正常的退出流程
    if (actionCode == "interaction.remove_cell_highlight") {
        LOG_DEBUG("Remove cell highlight action triggered");
        onExit();
        return;
    }

    setActive(true);
    m_isActive = true;

    LOG_INFO("CellHighlightHandler activated for cell highlighting");

    m_trianglePreview = trans::TransDB::create<TransientPolyDataActorDB>();
    if (m_trianglePreview) {
        LOG_INFO("Created transient actor for cell highlight [ID={}]", m_trianglePreview->getDBInstanceID().getValue());
    } else {
        LOG_ERROR("Failed to create transient actor for cell highlight!");
    }

    // 2. 配置为高亮样式
    if (m_trianglePreview) {
        const Vector3 highlightColor(1.0f, 1.0f, 0.0f);
        if (auto material = m_trianglePreview->getMaterial()) {
            material->setColor(highlightColor);
            material->setDiffuseColor(highlightColor);
            material->setOpacity(0.7f);
            material->setMetallic(0.0f);
            material->setRoughness(0.3f);
            material->setRepresentation(2);
        }
        LOG_DEBUG("Configured highlight style: color=yellow, opacity=0.7");
    } else {
        LOG_ERROR("Cell highlight preview is null, cannot configure style!");
    }

    // 3. 初始化缓存的pick结果
    m_cachedPickResult = PickResult(); // 空的pick结果
    LOG_DEBUG("Initialized cached pick result");

}

void CellHighlightHandler::onExit() {
    LOG_INFO("CellHighlightHandler::onExit() - Starting cleanup process");

    m_isActive = false;
    setActive(false);
    interactionPickService().cancel(m_pendingPickRequestId);
    m_pendingPickRequestId = 0;
    m_inputView = INVALID_DB_ID;

    // 清理三角面片显示DB实例
    if (m_trianglePreview) {
        auto dbInstanceID = m_trianglePreview->getDBInstanceID();

        LOG_DEBUG("Starting cleanup for highlight DB instance: ID={}", dbInstanceID.getValue());

        // 从 DocumentManager 中删除临时 DB 实例
        // 这会触发 ChangeType::DELETED 通知，让 DBRenderAdapter 清理渲染资源
        m_trianglePreview->removeMaterial();
        DocumentManager::instance()->unregisterDBInstance(dbInstanceID);
        LOG_DEBUG("Unregistered DB instance from DocumentManager: ID={}", dbInstanceID.getValue());

        // 智能指针自动管理生命周期
        m_trianglePreview.reset();
        LOG_DEBUG("Released cell highlight preview reference");
    } else {
        LOG_WARN("Cell highlight preview is null during cleanup");
    }

    LOG_INFO("CellHighlightHandler::onExit() - Cleanup completed");
}

bool CellHighlightHandler::onMouseMoveEvent(QMouseEvent* event) {
    if (!m_isActive) {
        return false;
    }

    const QPoint mousePos(event->position().x(), event->position().y());
    m_inputView = DBInstanceID(inputViewId());
    auto& picks = interactionPickService();
    if (!picks.supportsFeature(
            m_inputView, GPlatform::Rendering::PickFeature::Primitive)) {
        if (m_trianglePreview) {
            m_trianglePreview->setGeometry({});
        }
        return false;
    }
    if (m_pendingPickRequestId != 0) {
        picks.cancel(m_pendingPickRequestId);
    }
    m_pendingPickRequestId = picks.requestForView(
        m_inputView, mousePos, PickChannel::Cell,
        GPlatform::Rendering::PickDetail::Primitive,
        PickDelivery::LatestOnly);

    return false; // 不阻止事件传播，允许其他Handler处理
}

void CellHighlightHandler::processPickResult(const PickResult& pickResult) {
    if (!m_trianglePreview) {
        LOG_ERROR("Cell highlight preview is null; cannot process pick result");
        return;
    }

    updateCellHighlight(pickResult);
}

void CellHighlightHandler::updateCellHighlight(const PickResult& pickResult) {
    if (!pickResult.primitiveIndex || !pickResult.objectId.isValid()) {
        // Pick无效，清空显示
        m_trianglePreview->setGeometry({});
        // 清空缓存的pick结果
        m_cachedPickResult = PickResult();
        return;
    }

    // 检查pick结果是否发生变化
    if (isPickResultChanged(pickResult)) {
        LOG_DEBUG("Pick result changed: updating transform from picked DB [ID={}]",
                 pickResult.objectId.getValue());

        // 更新transform（从pick到的DB获取）
        updateTransformFromPickedDB(pickResult);

        // 更新缓存的pick结果
        m_cachedPickResult = pickResult;
    }

    // 从Pick结果提取单个面片几何数据
    auto triangles = GeometryExtractor::extractSingleCell(pickResult);

    if (!triangles.empty()) {
        m_trianglePreview->setGeometry(polyDataFromTriangles(triangles));
        LOG_DEBUG("Updated cell highlight: actor={}, cell={}, triangles={}",
                 pickResult.objectId.getValue(), *pickResult.primitiveIndex,
                 triangles.size());
    } else {
        // 提取失败，清空显示
        m_trianglePreview->setGeometry({});
        LOG_WARN("Failed to extract cell geometry, cleared highlight");
    }
}


bool CellHighlightHandler::isPickResultChanged(const PickResult& newResult) const {
    return newResult.objectId != m_cachedPickResult.objectId ||
           newResult.partId != m_cachedPickResult.partId ||
           newResult.primitiveIndex != m_cachedPickResult.primitiveIndex;
}

void CellHighlightHandler::updateTransformFromPickedDB(const PickResult& pickResult) {
    if (!pickResult.objectId.isValid() || !m_trianglePreview) {
        LOG_WARN("Cannot update transform: invalid pick result or null preview");
        return;
    }

    // 从DocumentManager获取pick到的DB实例
    auto documentManager = DocumentManager::instance();
    auto pickedDB = documentManager->getDBInstance(pickResult.objectId);

    if (!pickedDB) {
        LOG_ERROR("Failed to get picked DB instance: ID={}",
                  pickResult.objectId.getValue());
        return;
    }

    // 尝试转换为ActorDB以获取transform
    auto actorDB = std::dynamic_pointer_cast<ActorDB>(pickedDB);
    if (!actorDB) {
        LOG_ERROR("Picked DB is not an ActorDB: ID={}",
                  pickResult.objectId.getValue());
        return;
    }

    Transform pickedTransform = actorDB->getTransform();
    if (const auto instance =
            std::dynamic_pointer_cast<ModelInstanceDB>(actorDB);
        instance && pickResult.partId && pickResult.partId->isValid()) {
        const auto part = documentManager->getDB<ModelPartDB>(
            *pickResult.partId);
        if (!part || part->object() != instance->object()) {
            LOG_WARN(
                "Cannot update highlight transform: Part does not belong to picked Instance");
            return;
        }
        pickedTransform.setMatrix(
            instance->getTransformMatrix() *
            part->getLocalTransform().getMatrix());
    }

    // 设置transform（架构应该自动处理通知和同步）
    m_trianglePreview->setTransform(pickedTransform);

    LOG_DEBUG("Applied transform from picked DB [ID={}] to highlight DB [ID={}]",
             pickResult.objectId.getValue(),
             m_trianglePreview->getDBInstanceID().getValue());
}
