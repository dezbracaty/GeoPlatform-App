#include "QuickActionController.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_QML_SINGLETON_CUSTOM(
    QuickActionController, "QuickActionController", &QuickActionController::create)
#include "SelectionBridge.hpp"
#include "PickService.hpp"
#include "InteractionRuntime.hpp"
#include "ViewportInputRouter.hpp"
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <algorithm>
#include "Foundation/Log.h"
#include <stdexcept>

// SmoothUI headers
#include "MenuData.hpp"
#include "MenuGroup.hpp"
#include "MenuItem.hpp"

QuickActionController* QuickActionController::s_instance = nullptr;

QuickActionController::QuickActionController(QObject* parent)
    : QObject(parent)
{
    LOG_DEBUG("QuickActionController created");
}

QuickActionController* QuickActionController::getInstance()
{
    if (!s_instance) {
        s_instance = new QuickActionController();
    }
    return s_instance;
}

QuickActionController* QuickActionController::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    // Return the singleton instance
    auto instance = getInstance();
    instance->initialize();
    return instance;
}

void QuickActionController::initialize()
{
    if (m_menuData) {
        return; // Already initialized
    }

    // Get MenuData singleton instance
    m_menuData = MenuData::getInstance();
    LOG_DEBUG("QuickActionController: Got MenuData instance");

    // Get SelectionBridge instance
    m_selectionBridge = SelectionBridge::instance();
    if (m_selectionBridge) {
        // Connect to selection changed signals
        connect(m_selectionBridge, &SelectionBridge::selectionChanged,
                this, &QuickActionController::onSelectionChanged);
        connect(m_selectionBridge, &SelectionBridge::activeTargetChanged, this, &QuickActionController::onSelectionChanged);
        LOG_DEBUG("QuickActionController: Connected to SelectionBridge signals");
    } else {
        LOG_WARN("QuickActionController: Failed to get SelectionBridge instance");
    }

    // Initialize current context
    m_currentContext = determineCurrentContext();
    LOG_INFO("QuickActionController: Initialized with context '{}'", m_currentContext.toStdString());
}

QString QuickActionController::currentContext() const
{
    return m_currentContext;
}

QString QuickActionController::getCurrentContext()
{
    return determineCurrentContext();
}

QString QuickActionController::determineCurrentContext() const
{
    if (!m_selectionBridge) {
        return "NoSelection";
    }

    if (m_selectionBridge->modifierTarget() && m_menuData && m_menuData->currentEnvironment()=="normal") return "ModelPart";

    // Simple context determination based on selection
    if (hasSelection()) {
        int count = getSelectionCount();
        if (count == 1) {
            return "SingleModel";
        } else if (count > 1) {
            return "MultipleModels";
        }
    }

    return "NoSelection";
}

bool QuickActionController::hasSelection() const
{
    return m_selectionBridge ? m_selectionBridge->hasSelection() : false;
}

int QuickActionController::getSelectionCount() const
{
    return m_selectionBridge ? m_selectionBridge->selectionCount() : 0;
}

TMenuGroup* QuickActionController::getMenuGroupForContext(const QString& contextType)
{
    if (!m_menuData) {
        LOG_WARN("QuickActionController: MenuData not initialized");
        return nullptr;
    }

    // Use contextType directly as group name (e.g., "NoSelection", "SingleModel", "MultipleModels")
    QString groupName = contextType;

    // Get menu group from MenuData - 直接返回 TMenuGroup*
    // 注意：getGroup 返回 const TMenuGroup*，但 QML 需要非 const 指针才能调用方法
    TMenuGroup* menuGroup = const_cast<TMenuGroup*>(m_menuData->getGroup(groupName));
    if (!menuGroup) {
        LOG_WARN("QuickActionController: Menu group '{}' not found", groupName.toStdString());
        return nullptr;
    }

    LOG_DEBUG("QuickActionController: Got menu group for context '{}' with {} items",
             contextType.toStdString(), menuGroup->items().size());

    return menuGroup;
}

void QuickActionController::onSelectionChanged()
{
    QString newContext = determineCurrentContext();
    if (newContext != m_currentContext) {
        m_currentContext = newContext;
        LOG_INFO("QuickActionController: Context changed to '{}'", m_currentContext.toStdString());
        emit contextChanged();
    }
}

void QuickActionController::handleRightClick(const QPointF& position)
{
    handleRightClickForView(position, ViewportInputRouter::getInstance()->lastInputViewId());
}

void QuickActionController::handleRightClickForView(const QPointF& position, std::uint64_t viewId)
{
    initialize();
    // Resolve only an actual context click, after pan/drag has been ruled out.
    // Preserve a multi-selection when its existing member is right-clicked.
    if (m_menuData->currentEnvironment() == "normal" && viewId != 0) {
        const auto hit=interactionPickService().pickCpuForView(DBInstanceID(viewId),position.toPoint());
        using GPlatform::Rendering::PickStatus;
        if (hit.status==PickStatus::Miss) m_selectionBridge->clearSelection();
        else if (hit.status==PickStatus::Hit && !hit.payload.subTarget) {
            auto* document=DocumentManager::instance();
            if (document->getDB<ModelInstanceDB>(hit.payload.objectId)) {
                auto part=hit.payload.partId ? document->getDB<ModelPartDB>(*hit.payload.partId) : nullptr;
                if (part && part->getRole()==int(ModelPartRole::ParameterModifier))
                    m_selectionBridge->setSelectedModifier(hit.payload.objectId,*hit.payload.partId);
                else {
                    const auto& ids=m_selectionBridge->getSelectedIds();
                    if (std::find(ids.begin(),ids.end(),hit.payload.objectId)==ids.end())
                        m_selectionBridge->setSelectedObjects({hit.payload.objectId});
                }
            } else m_selectionBridge->clearSelection();
        } else return; // Invalid/stale viewport or tool-owned hit is not a model context.
    }
    onSelectionChanged();
    emit showQuickActionMenu(position.x(), position.y(), "");
}

void QuickActionController::handleKeyboardInput(const QString& text, const QPointF& position)
{
    LOG_INFO("QuickActionController: Keyboard input '{}' detected at position ({}, {})",
             text.toStdString(), position.x(), position.y());

    // Emit signal to QML to show quick action menu with initial text
    emit showQuickActionMenu(position.x(), position.y(), text);
}
