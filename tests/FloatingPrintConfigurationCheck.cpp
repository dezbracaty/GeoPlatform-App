#include "ApplicationModuleRegistration.hpp"
#include "SliceSettingsBridge.hpp"
#include "SlicingBackend.hpp"
#include "ModelPropertyBridge.hpp"
#include "SelectionBridge.hpp"
#include "SettingsHelper.h"
#include "AppInfo.h"
#include "SlicingConfigDB.hpp"
#include "DocumentManager.hpp"
#include "TransactionManager.hpp"
#include "TestModelGraphFixture.hpp"
#include "TransDBSerialization.hpp"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTest>
#include <QSignalSpy>
#include <QtQml/QQmlExtensionPlugin>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

Q_IMPORT_QML_PLUGIN(SmoothUIPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_implPlugin)

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void logGeometry(const char* phase, const QQuickWindow& window, const QQuickItem& bounds,
                 const QQuickItem& panel) {
    const auto rect = [](const QRectF& value) {
        return QJsonObject{{"x", value.x()}, {"y", value.y()},
                           {"width", value.width()}, {"height", value.height()}};
    };
    QJsonObject record{{"phase", phase}, {"window", rect(window.geometry())},
                       {"content", rect(window.contentItem()->boundingRect())},
                       {"bounds", rect(bounds.boundingRect())},
                       {"panel", rect(QRectF(panel.position(), panel.size()))},
                       {"devicePixelRatio", window.devicePixelRatio()}};
    if (const auto* screen = window.screen()) {
        record.insert("screen", rect(screen->geometry()));
        record.insert("availableScreen", rect(screen->availableGeometry()));
        record.insert("logicalDpi", screen->logicalDotsPerInch());
    }
    std::cout << "Popup geometry: "
              << QJsonDocument(record).toJson(QJsonDocument::Compact).constData() << std::endl;
}
void verifyLayout(QQuickWindow& window, QQuickItem& bounds, QQuickItem& panel) {
    const auto settle = [&](const QSizeF& size, const char* phase) {
        bounds.setSize(size);
        QCoreApplication::processEvents();
        logGeometry(phase, window, bounds, panel);
    };
    // Check initial placement and resize reactions without repairing them via
    // the component's positioning function in the test.
    logGeometry("nominal-layout", window, bounds, panel);
    require(std::abs(panel.width()-344) < 1 && std::abs(panel.height()-649) < 1,
            "Popup does not have reviewed 344 x 649 geometry at the nominal logical viewport");
    require(std::abs(panel.x() - (bounds.width()-panel.width()-16)) < 1,
            "Popup is not initially placed on the right with a 16 px margin");
    for (const QSizeF size : {QSizeF(800, 700), QSizeF(320, 700)}) {
        settle(size, "smaller-layout");
        require(panel.width() > 0 && panel.width() <= 344 && panel.height() > 42 && panel.height() < 649,
                "Popup did not adapt to the smaller logical viewport");
        require(panel.x() >= 16 && panel.x()+panel.width() <= bounds.width()-16+1 &&
                panel.y() >= 0 && panel.y()+panel.height() <= bounds.height()+1,
                "Popup extends outside the smaller logical viewport");
    }
}
QQuickItem* find(QQuickItem* parent, const QString& name) {
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems())
        if (auto* result = find(child, name)) return result;
    return nullptr;
}
void click(QQuickWindow& window, QQuickItem* root, const QString& name) {
    // Finish pending layout before resolving coordinates. A native window may
    // be occluded on a shared desktop, so frameSwapped is not a layout barrier.
    const auto polish = [](auto&& self, QQuickItem* item) -> void {
        item->ensurePolished();
        for (auto* child : item->childItems()) self(self, child);
        item->ensurePolished();
    };
    QCoreApplication::processEvents();
    polish(polish, window.contentItem());
    auto* target = find(root, name);
    require(target && target->isVisible() && target->isEnabled() && target->width() > 0 && target->height() > 0,
            qPrintable("Unavailable control: " + name));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint());
    QCoreApplication::processEvents();
}
void edit(QQuickWindow& window, QQuickItem* root, const QString& name, const char* text) {
    auto* target = find(root, name);
    require(target && target->isVisible() && target->isEnabled(), qPrintable("Unavailable editor: " + name));
    target->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent typed(QEvent::KeyPress, 0, Qt::NoModifier, QString::fromLatin1(text));
    QCoreApplication::sendEvent(&window, &typed);
    QTest::keyClick(&window, Qt::Key_Return);
    QCoreApplication::processEvents();
}
QJsonArray array(QObject* object, const QString& code) {
    QQmlExpression expression(qmlContext(object), object, "JSON.stringify(" + code + ")");
    const auto result = expression.evaluate().toString();
    require(!expression.hasError(), qPrintable(expression.error().toString()));
    return QJsonDocument::fromJson(result.toUtf8()).array();
}
void verifyCompleteProcessParameters(SliceSettingsBridge* settings, QQuickItem* editor, bool fiber) {
    for (const bool advanced : {false, true}) {
        editor->setProperty("showAdvancedMode", advanced);
        QSet<QString> expected, actual;
        for (const auto& page : settings->getTabPages("process", advanced)) {
            const auto id = page.toMap().value("id").toString();
            if (!fiber && id.contains("continuous-fiber")) continue;
            for (const auto& section : settings->getPageSchema("process", id, advanced))
                for (const auto& row : section.toMap().value("rows").toList())
                    expected.insert(row.toMap().value("key").toString());
        }
        for (const auto& category : array(editor, "compactPages.map(function(page) { return page.id })")) {
            editor->setProperty("compactCategory", category.toString());
            const auto keys = array(editor, "compactSections.reduce(function(keys, section) { return keys.concat(section.rows.map(function(row) { return row.key })) }, [])");
            for (const auto& key : keys) {
                require(!actual.contains(key.toString()), qPrintable("Duplicate process field: " + key.toString()));
                actual.insert(key.toString());
            }
        }
        require(actual == expected, qPrintable(QString("Incomplete process parameters: missing=%1 extra=%2")
                .arg(QStringList((expected-actual).values()).join(','), QStringList((actual-expected).values()).join(','))));
        std::cout << (fiber ? "Fiber" : "PLA") << (advanced ? " advanced: " : " basic: ")
                  << actual.size() << " process fields, complete and unique\n";
    }
    editor->setProperty("showAdvancedMode", false);
    editor->setProperty("compactCategory", "quality");
    QCoreApplication::processEvents();
}
}
int main(int argc, char** argv) {
    qputenv("GPLATFORM_JOURNAL_SESSION", "1");
    QStandardPaths::setTestModeEnabled(true);
    QQuickStyle::setStyle("SmoothUI");
    QGuiApplication app(argc, argv);
    const bool layoutOnly = app.arguments().contains("--layout");
    app.setOrganizationName("GPlatformTests");
    app.setApplicationName("FloatingPrintConfigurationCheck");
    SettingsHelper::getInstance()->init(argv);
    registerApplicationModules();
    AppInfo::getInstance()->setLocale("zh_CN");
    AppInfo::getInstance()->initializeTranslations();
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(POPUP_QML_ROOT "/global/PrintWorkspaceStyle.qml")),
                             "GPlatform", 1, 0, "PrintWorkspaceStyle");
    try {
        auto* settings = SliceSettingsBridge::instance();
        const auto initialLayerHeight = settings->getSetting("layer_height").toDouble();
        require(initialLayerHeight > 0, "No real active configuration");
        auto* document = DocumentManager::instance();
        auto configs = document->getDBInstancesByType(TypeID::SLICING_CONFIG_DB);
        require(configs.size() == 1, "Missing active slicing DB");
        const auto config = std::dynamic_pointer_cast<GPlatform::SlicingConfigDB>(configs.front());
        require(config != nullptr, "Invalid slicing DB");
        TestModelGraphFixture model;
        {
            TransactionGuard transaction("Floating config model fixture");
            model = createTestModelGraph(testModelTriangle(), "Popup real model");
            require(static_cast<bool>(model), "Cannot create real model graph");
        }
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(POPUP_IMPORTS));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(POPUP_QML_ROOT "/components/FloatingPrintConfiguration.qml")));
        require(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        require(object != nullptr, qPrintable(component.errorString()));
        auto* panel = qobject_cast<QQuickItem*>(object.get());
        require(panel != nullptr, "Panel did not create an Item");
        QQuickWindow window;
        window.setGeometry(0, 0, 1382, 894);
        // Layout acceptance uses a fixed logical scene, independent of the
        // window manager's screen-size limits. Interaction uses the real window.
        QQuickItem layoutBounds(window.contentItem());
        layoutBounds.setSize(QSizeF(1382, 894));
        auto* bounds = layoutOnly ? &layoutBounds : window.contentItem();
        panel->setParentItem(bounds);
        panel->setProperty("boundsItem", QVariant::fromValue(bounds));
        panel->setProperty("workspaceReady", true);
        window.show(); window.requestActivate(); QTest::qWait(200);
        logGeometry(layoutOnly ? "layout-initial" : "interaction-initial", window, *bounds, *panel);
        if (layoutOnly) {
            verifyLayout(window, *bounds, *panel);
            std::cout << "Popup nominal and smaller logical viewport layout passed\n";
            return 0;
        }
        QObject::connect(settings, &SliceSettingsBridge::printerSwitchDecisionRequested, &window,
            [settings](const QVariantMap&) { settings->resolvePrinterSwitch(QStringLiteral("discard")); });
        const auto verifyRejectedSetting = [&] {
            // Use the production full editor: a genuinely invalid switch edit must
            // restore its checked state without a revision change or a schema reload.
            require(settings->selectMachine("CFSYS Alpha500 Printer", "0.4"), "Cannot activate rejection fixture");
            settings->setSetting("toolchange_z_lift", 10.0);
            settings->markCurrentAsSliced();
            const auto rejectedBefore = GPlatform::SlicingBackend::instance().configSnapshot();
            require(rejectedBefore && rejectedBefore->value("toolchange_z_lift") == "10" &&
                        rejectedBefore->value("manual_filament_change") == "0", "Invalid rejection fixture state");
            panel->setVisible(false);
            QQmlComponent fullEditor(&engine, QUrl::fromLocalFile(QStringLiteral(POPUP_QML_ROOT "/components/SlicingSettingsContent.qml")));
            require(fullEditor.isReady(), qPrintable(fullEditor.errorString()));
            std::unique_ptr<QObject> fullObject(fullEditor.create());
            auto* full = qobject_cast<QQuickItem*>(fullObject.get());
            require(full != nullptr, "Full parameter editor did not load");
            full->setParentItem(window.contentItem());
            full->setSize(QSizeF(1000, 700));
            full->setProperty("embedded", true);
            full->setProperty("showAdvancedMode", true);
            full->setProperty("searchText", "manual_filament_change");
            const QString rejectedName = QStringLiteral("sliceSettingSearchEditor_manual_filament_change");
            require(QTest::qWaitFor([&] { return find(full, rejectedName) != nullptr; }, 2000),
                    "Manual-change search did not expose the actual switch");
            auto* rejectedControl = find(full, rejectedName);
            require(!rejectedControl->property("checked").toBool(), "Switch did not reflect the accepted false value");
            QSignalSpy warnings(settings, &SliceSettingsBridge::settingRejected);
            bool notificationReadBack = false;
            const auto observer = QObject::connect(settings, &SliceSettingsBridge::settingRejected,
                full, [&](const QString&, const QString&) {
                    notificationReadBack = !settings->getSetting("manual_filament_change").toBool();
                });
            click(window, full, rejectedName);
            require(QTest::qWaitFor([&] { return warnings.size() == 1; }, 1000), "Rejected edit did not notify the user");
            require(!rejectedControl->property("checked").toBool() && notificationReadBack,
                    "Rejected switch retained the requested value or notified while holding the settings lock");
            require(!warnings.front().at(1).toString().isEmpty(), "Rejection notification omitted the reason");
            require(GPlatform::SlicingBackend::instance().configSnapshot()->values() == rejectedBefore->values() &&
                        !settings->hasPendingReslice() && settings->configurationError().isEmpty(),
                    "Rejected edit changed accepted configuration, invalidated slicing, or disabled valid configuration");
            QObject::disconnect(observer);
            std::cout << "Rejected native switch restored its value and reported a reason without configuration mutation\n";
        };
        if (app.arguments().contains("--setting-rejection")) {
            verifyRejectedSetting();
            return 0;
        }
        const auto expandedHeight = panel->height();
        require(panel->isVisible() && panel->width() > 0 && expandedHeight > 42,
                "Popup is unavailable in the actual interaction viewport");
        auto* editor = find(panel, "print.configuration.editor");
        require(editor, "Missing native process editor");
        // The retained machine popup and floating editor coexist in the real UI.
        // Hidden controls must still have distinct names for strict Journal lookup.
        QQmlComponent machineComponent(&engine, QUrl::fromLocalFile(QStringLiteral(POPUP_QML_ROOT "/components/MachineSelectionBar.qml")));
        require(machineComponent.isReady(), qPrintable(machineComponent.errorString()));
        std::unique_ptr<QObject> machineObject(machineComponent.create());
        require(machineObject != nullptr, qPrintable(machineComponent.errorString()));
        auto* machine = qobject_cast<QQuickItem*>(machineObject.get());
        require(machine != nullptr, "Machine selector did not create an Item");
        machine->setParentItem(window.contentItem());
        machine->setVisible(false);
        QCoreApplication::processEvents();
        const auto verifyAutomationNames = [&] {
            const auto retained = machineObject->findChildren<QQuickItem*>("machineSelectorSettingsSearchField");
            const auto floating = object->findChildren<QQuickItem*>("print.configuration.machineSelectorSettingsSearchField");
            require(retained.size() == 1 && floating.size() == 1,
                    "Reused settings editors do not have independent automation names");
            for (auto* item : object->findChildren<QQuickItem*>()) {
                const auto name = item->objectName();
                if (!(name.startsWith("machineSelector") || name.startsWith("sliceSetting")
                      || name.startsWith("fiberTool") || name.startsWith("fiberSwap")
                      || name.startsWith("filament") || name.startsWith("print.configuration"))) continue;
                require(machineObject->findChildren<QQuickItem*>(item->objectName()).isEmpty(),
                        qPrintable("Duplicate editor automation name: " + item->objectName()));
            }
        };
        verifyAutomationNames();
        machine->setVisible(true);
        verifyAutomationNames();
        machine->setVisible(false);
        const auto before = trans::serializeState(*config);
        const auto configBefore = GPlatform::SlicingBackend::instance().configSnapshot();
        verifyCompleteProcessParameters(settings, editor, false);
        QTest::qWait(30);
        click(window, panel, "print.configuration.collapse");
        require(panel->height() == 42, "Collapse did not retain only title bar");
        click(window, panel, "print.configuration.collapse");
        require(panel->isVisible() && std::abs(panel->height()-expandedHeight) < 1,
                "Expanding did not restore popup height for the actual viewport");
        require(trans::serializeState(*config) == before,
                "Presentation-only operations changed slicing DB");
        require(configBefore && GPlatform::SlicingBackend::instance().configSnapshot()->values() == configBefore->values(),
                "Presentation-only operations changed actual slicing configuration");
        edit(window, panel, "print.configuration.editor.layer_height", "0.12");
        QTest::qWait(30);
        require(std::abs(settings->getSetting("layer_height").toDouble()-.12)<1e-6,
                "Native parameter editor did not write active library configuration");
        // SlicingConfigDB's historical LayerHeight is not the slicer's source.
        // Exercise the same immutable snapshot captured by the slice session.
        const auto captured = GPlatform::SlicingBackend::instance().configSnapshot();
        require(captured && captured->value("layer_height") == "0.12",
                "Native parameter editor did not update captured slicer configuration");
        require(settings->modifiedSettingKeys().contains("layer_height"),
                "Modified field is not reported by the real preset state");
        require(settings->hasPendingReslice(), "Parameter edit did not invalidate slicing output");
        settings->markCurrentAsSliced();
        require(!settings->hasPendingReslice() && settings->modifiedSettingKeys().contains("layer_height"),
                "Slicing incorrectly cleared unsaved process preset edits");
        const auto edited = trans::serializeState(*config);
        click(window, panel, "print.configuration.collapse");
        click(window, panel, "print.configuration.collapse");
        require(trans::serializeState(*config) == edited, "Collapse/expand reverted configuration");
        require(GPlatform::SlicingBackend::instance().configSnapshot()->values() == captured->values(),
                "Collapse/expand reverted actual slicing configuration");
        click(window, panel, "print.configuration.objectsTab");
        click(window, panel, "print.configuration.model." + QString::number(model.instance->getDBInstanceID().getValue()));
        require(QTest::qWaitFor([&] {
                    return SelectionBridge::instance()->selectedId() == model.instance->getDBInstanceID();
                }, 2000),
                "Object list did not select the real model");
        edit(window, panel, "print.configuration.position.x", "23.5");
        require(std::abs(model.instance->getPosition().x-23.5f)<1e-5f,
                "Position editor did not update selected model DB");
        panel->forceActiveFocus();
        engine.singletonInstance<ModelPropertyBridge*>("GPlatform", "ModelPropertyBridge")->setPosition(QVector3D(42, 15, 0));
        QCoreApplication::processEvents();
        require(find(panel, "print.configuration.position.x")->property("text").toString() == "42.00",
                "Position input did not follow external model DB updates after editing");
        require(settings->setFilamentColor(0, QColor("#33aabb")), "Cannot change actual filament color");
        require(settings->modifiedSettingKeys().contains("layer_height"), "Material change lost the process modification baseline");
        const auto colorBeforeReset = GPlatform::SlicingBackend::instance().configSnapshot()->value("filament_colour");
        click(window, panel, "print.configuration.processTab");
        click(window, panel, "print.configuration.reset");
        require(settings->modifiedSettingKeys().isEmpty() &&
                std::abs(settings->getSetting("layer_height").toDouble()-initialLayerHeight)<1e-6,
                qPrintable(QString("Restore did not reactivate the selected preset baseline: initial=%1 actual=%2 modified=%3")
                    .arg(initialLayerHeight).arg(settings->getSetting("layer_height").toDouble())
                    .arg(settings->modifiedSettingKeys().join(','))));
        require(GPlatform::SlicingBackend::instance().configSnapshot()->value("filament_colour") == colorBeforeReset,
                "Restoring process edits changed filament color");
        QTest::qWait(30);
        require(find(panel, "print.configuration.editor.layer_height")->property("text").toString() == QString::number(initialLayerHeight, 'f', 2),
                "Restoring process edits left a stale numeric input");
        edit(window, panel, "print.configuration.search", "layer_height");
        QTest::qWait(30);
        auto* searchResult = find(panel, "print.configuration.sliceSettingSearchEditor_layer_height");
        require(searchResult && searchResult->isVisible(), "Exact parameter search did not expose the actual editor");
        edit(window, panel, "print.configuration.search", "fiber_width");
        QTest::qWait(30);
        require(!find(panel, "print.configuration.sliceSettingSearchEditor_fiber_width"),
                "Ordinary-machine search exposed a continuous-fiber editor");
        const auto beforeDrag = GPlatform::SlicingBackend::instance().configSnapshot();
        const auto originalPosition = panel->position();
        auto* dragHandle = find(panel, "print.configuration.drag");
        const auto dragStart = dragHandle->mapToScene(QPointF(80, 20)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(&window, dragStart + QPoint(-25, 10), 20);
        QTest::mouseMove(&window, dragStart + QPoint(-55, 25), 20);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, dragStart + QPoint(-55, 25));
        require(panel->x() < originalPosition.x() && panel->y() >= originalPosition.y(),
                "Title-bar mouse drag did not move the popup");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 300));
        require(panel->isVisible(), "Clicking outside dismissed the nonmodal popup");
        require(GPlatform::SlicingBackend::instance().configSnapshot()->values() == beforeDrag->values(),
                "Dragging or outside click changed slicing configuration");
        require(settings->selectMachine("CFSYS Alpha500 Printer", "0.4"), "Cannot activate actual continuous-fiber machine");
        require(panel->property("fiberCapable").toBool(), "Fiber machine did not expose its process category");
        const auto fiberBefore = GPlatform::SlicingBackend::instance().configSnapshot();
        verifyCompleteProcessParameters(settings, editor, true);
        require(GPlatform::SlicingBackend::instance().configSnapshot()->values() == fiberBefore->values(),
                "Browsing complete fiber parameter categories changed the slicing configuration");
        require(settings->selectMachine("P3", "0.4"), "Cannot activate P3");
        require(panel->property("fiberCapable").toBool(), "Mixed-capability P3 tools hid the fiber settings");
        for (int tool = 0; tool < 6; ++tool) {
            require(settings->selectFilamentPresetForSlot(tool, "CFSYS CCF"),
                    "P3 rejected CCF on an interchangeable tool");
            QCoreApplication::processEvents();
            const auto materialSlots = settings->filamentSlots();
            require(materialSlots.size() == 6, "Selecting CCF changed P3 tool count");
            const auto slot = materialSlots[tool].toMap();
            require(slot.value("processType").toString() == "continuous_fiber" &&
                    slot.value("physicalToolIndex").toInt() == tool,
                    "CCF selection lost its process type or physical tool mapping");
            const auto filaments = config->getFilaments();
            require(filaments.size() == 6 && filaments[tool].presetId == "CFSYS CCF",
                    "P3 CCF selection did not update the real slicing DB");
            QVariant label;
            require(QMetaObject::invokeMethod(editor, "physicalToolLabel", Q_RETURN_ARG(QVariant, label),
                    Q_ARG(QVariant, QVariant(slot))), "Cannot read the actual material label");
            require(label.toString().contains("T" + QString::number(tool)) ||
                    label.toString().contains(QString::number(tool)), "Tool label lost its physical ID");
            require(label.toString().contains("连续纤维") || label.toString().contains("Continuous fiber"),
                    "Mixed-capability tool label did not follow its selected CCF material");
            require(settings->selectFilamentPresetForSlot(tool, "CFSYS PLA"),
                    "P3 could not switch a fiber tool back to plastic");
            require(settings->filamentSlots()[tool].toMap().value("processType").toString() == "thermoplastic" &&
                    config->getFilaments()[tool].presetId == "CFSYS PLA",
                    "Changing back to plastic left a stale fiber role or DB material");
        }
        // Exercise the full material editor rather than the compact process panel.
        QQmlComponent materialComponent(&engine, QUrl::fromLocalFile(QStringLiteral(POPUP_QML_ROOT "/components/SlicingSettingsContent.qml")));
        require(materialComponent.isReady(), qPrintable(materialComponent.errorString()));
        std::unique_ptr<QObject> materialObject(materialComponent.createWithInitialProperties({
            {"embedded", true}, {"workspaceIndex", 1}}));
        auto* materials = qobject_cast<QQuickItem*>(materialObject.get());
        require(materials != nullptr, qPrintable(materialComponent.errorString()));
        panel->setVisible(false);
        materials->setParentItem(window.contentItem());
        materials->setPosition(QPointF(8, 8));
        materials->setHeight(650);
        materials->setProperty("workspaceIndex", 1);
        for (const int width : {780, 580}) {
            materials->setWidth(width);
            QTest::qWait(60);
            QList<QRectF> cards;
            for (int slot = 1; slot <= 6; ++slot) {
                auto* card = find(materials, "filamentSlotCard_" + QString::number(slot));
                require(card && card->isVisible() && card->width() > 0, "Missing visible P3 material card");
                const QRectF rect(card->mapToItem(materials, QPointF()), card->size());
                require(rect.left() >= 0 && rect.right() <= width,
                        "P3 material card extends outside its editor");
                for (const auto& previous : cards) require(!rect.intersects(previous), "P3 material cards overlap");
                cards.push_back(rect);
            }
            std::cout << "Material layout: width=" << width << " groups=" << materials->property("toolGroups").value<QJSValue>().toVariant().toList().size()
                      << " perTool=" << materials->property("perToolMaterials").toBool()
                      << " first=" << cards[0].x() << "," << cards[0].y() << "," << cards[0].width()
                      << " last=" << cards[5].x() << "," << cards[5].y() << std::endl;
            require(cards[5].top() > cards[0].top(), "P3 material groups did not wrap");
        }
        materials->setWidth(780);
        QTest::qWait(60);
        const auto colorBeforeCancel = GPlatform::SlicingBackend::instance().configSnapshot()->value("filament_colour");
        click(window, materials, "filamentColorButton_1");
        QTest::qWait(30);
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(30);
        require(GPlatform::SlicingBackend::instance().configSnapshot()->value("filament_colour") == colorBeforeCancel,
                "Canceling the color picker changed filament colors");
        require(!find(materials, "restoreCurrentPresetButton")->isVisible(),
                "The material page must not offer an ambiguous all-presets reset");
        const auto originalFlow = settings->getSetting("filament_max_volumetric_speed").toString().split(',');
        require(originalFlow.size() == 6, "P3 flow values do not match its six slots");
        auto editedFlow = originalFlow;
        editedFlow[4] = QString::number(originalFlow[4].toDouble() + 3);
        editedFlow[5] = QString::number(originalFlow[5].toDouble() + 4);
        settings->setSetting("filament_max_volumetric_speed", editedFlow.join(','));
        require(settings->getSetting("filament_max_volumetric_speed").toString().split(',')[5] == editedFlow[5],
                "Cannot edit two material presets before rollback");
        click(window, materials, "restoreFilamentPresetButton_5");
        require(!find(materials, "restoreFilamentPresetButton_5")->isEnabled(),
                "Restored material must not offer a no-op rollback");
        const auto nativeAfterRestore = *GPlatform::SlicingBackend::instance().activeConfigView();
        require(settings->filamentSlots()[4].toMap().value("presetModified").toBool() == nativeAfterRestore.filament_slots[4].preset_modified,
                "Material restore state is not projected from the library");
        const auto restoredFlow = settings->getSetting("filament_max_volumetric_speed").toString().split(',');
        require(restoredFlow[4] == originalFlow[4] && restoredFlow[5] == editedFlow[5],
                "The material card reset changed another preset or failed to restore its own");
        materials->setProperty("workspaceIndex", 0);
        const auto originalSpeed = settings->getSetting("outer_wall_speed");
        settings->setSetting("outer_wall_speed", 97.0);
        require(settings->getSetting("outer_wall_speed").toDouble() == 97.0, "Cannot edit process before reset");
        require(settings->modifiedSettingKeys().contains("outer_wall_speed"), "Native process dirty state was not projected");
        click(window, materials, "restoreCurrentPresetButton");
        require(settings->getSetting("outer_wall_speed") == originalSpeed,
                "Full editor restore button did not restore the actual process preset");
        require(!find(materials, "restoreCurrentPresetButton")->isEnabled(),
                "Restored process must not offer a no-op rollback");
        require(GPlatform::SlicingBackend::instance().configSnapshot()->value("filament_colour") == colorBeforeCancel,
                "Full editor process restore changed project colors");
        materials->setVisible(false);
        panel->setVisible(true);
        verifyRejectedSetting();
        std::cout << "Native popup: collapse/hide, actual slicer snapshot, selection and model DB transform passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
