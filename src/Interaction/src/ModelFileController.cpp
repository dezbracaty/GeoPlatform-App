#include "ModelFileController.hpp"
#include "InteractionRegistration.hpp"
#include "SelectionBridge.hpp"
#include "ModelCreationUtil.hpp"
#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <PrintBedDB.hpp>
#include <MenuData.hpp>
#include <MenuItem.hpp>
#include <MenuGroup.hpp>
#include <TaskStateNotifier.hpp>
#include <QUrl>
#include <QThread>

REGISTER_INTERACTION_QML_SINGLETON_CUSTOM(ModelFileController, "ModelFileController", &ModelFileController::create)
ModelFileController* ModelFileController::instance() {
    static auto* value = [] {
        auto* controller = new ModelFileController;
        QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);
        return controller;
    }();
    return value;
}
ModelFileController* ModelFileController::create(QQmlEngine*, QJSEngine*) { return instance(); }
ModelFileController::ModelFileController() {
    connect(SelectionBridge::instance(), &SelectionBridge::activeTargetChanged, this, &ModelFileController::capabilitiesChanged);
    const auto updateMenus=[this] {
        std::function<void(TMenuItem*)> update=[&](TMenuItem* item) {
            const auto action=item->actionCode();
            if(action.startsWith("model.export.selection."))item->enabled(canExportSelection());
            else if(action.startsWith("model.export.plate."))item->enabled(canExportPlate());
            else if(action=="file.save" || action=="file.saveAs")item->enabled(canSaveProject());
            else if(action.startsWith("model.export.owner."))item->enabled(!exportTargets("owner").empty());
            for(auto* child:item->getItems())update(child);
        };
        for(const auto* name:{"Menu","NoSelection","SingleModel","MultipleModels","ModelPart"})
            if(const auto* group=MenuData::getInstance()->getGroup(name))for(auto* item:group->items())update(item);
    };
    connect(this,&ModelFileController::capabilitiesChanged,this,updateMenus);
    connect(MenuData::getInstance(),&MenuData::environmentChanged,this,updateMenus);
    updateMenus();
    // One process-lifetime projection, like the other document singleton bridges.
    DocumentManager::instance()->addChangeListener([this](const auto& change) {
        if (change.dbType != TypeID::MODEL_INSTANCE_DB && change.dbType != TypeID::PRINT_BED_DB && change.dbType != TypeID::SLICING_CONFIG_DB) return;
        if (QThread::currentThread() == thread()) emit capabilitiesChanged();
        else QMetaObject::invokeMethod(this, [this] { emit capabilitiesChanged(); }, Qt::QueuedConnection);
    });
}
QString ModelFileController::projectFile() const { return m_projectPath.isEmpty() ? QString{} : QUrl::fromLocalFile(m_projectPath).toString(); }
void ModelFileController::setProjectPath(const QString& path) {
    if (m_projectPath == path) return;
    m_projectPath = path; emit projectFileChanged();
}
QVariantList ModelFileController::exportTargets(const QString& scope) const {
    auto* document = DocumentManager::instance();
    QVariantList result;
    if (scope == "selection" || scope == "owner") {
        auto* selection = SelectionBridge::instance();
        auto ids = selection->getSelectedIds();
        if (scope == "owner" && selection->modifierTarget()) ids = {selection->activeInstanceId()};
        for (auto id : ids) if (document->getDB<ModelInstanceDB>(id)) result.append(QString::number(id.getValue()));
    } else {
        const auto bed = ModelCreationUtil::resolvePrintBed();
        for (const auto& db : document->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB)) {
            const auto model = std::dynamic_pointer_cast<ModelInstanceDB>(db);
            if (model && (scope == "project" || (bed && model->getParentPrintBedDBId() == bed->getDBInstanceID())))
                result.append(QString::number(model->getDBInstanceID().getValue()));
        }
    }
    return result;
}
bool ModelFileController::canExportSelection() const { return !exportTargets("selection").empty(); }
bool ModelFileController::canExportPlate() const { return !exportTargets("plate").empty(); }

bool ModelFileController::canSaveProject() const { return !exportTargets("project").empty(); }
