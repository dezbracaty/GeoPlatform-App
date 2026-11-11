#include <SlicingPreviewBridge.hpp>
#include <ToolpathResultPublication.hpp>
#include <ModelInstanceDB.hpp>
#include <PrintBedDB.hpp>
#include <QJsonArray>
#include <QJsonDocument>
// Exercise the production command bar and SmoothUI controls with in-memory services.
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QtQml/QQmlExtensionPlugin>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

Q_IMPORT_QML_PLUGIN(SmoothUIPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_implPlugin)

namespace {
class ChineseCommandBarTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* text, const char*, int) const override {
        const QString source = QString::fromUtf8(text);
        if (source == "Add Model") return QStringLiteral("添加模型");
        if (source == "Start Slicing") return QStringLiteral("开始切片");
        if (source == "Send to Printer") return QStringLiteral("发送到打印机");
        if (source == "Connect to Send") return QStringLiteral("连接后发送");
        return {};
    }
};

void check(bool ok, const QString& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
void write(const QString& path, const QString& text) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), path);
    file.write(text.toUtf8());
}
QString source(const QString& path) {
    QFile file(QStringLiteral(PRINTER_QML_ROOT) + '/' + path);
    check(file.open(QIODevice::ReadOnly | QIODevice::Text), "Read production " + path);
    return QString::fromUtf8(file.readAll());
}
QString block(const QString& source, const QString& marker) {
    const auto start = source.indexOf(marker);
    check(start >= 0, "Missing production block: " + marker);
    auto brace = source.indexOf('{', start);
    int depth = 0; QChar quote;
    for (auto i = brace; i < source.size(); ++i) {
        const auto c = source[i];
        if (!quote.isNull()) {
            if (c == '\\') ++i;
            else if (c == quote) quote = QChar();
        } else if (c == '\"' || c == '\'') quote = c;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return source.mid(start, i-start+1);
    }
    throw std::runtime_error("Unclosed production block");
}
QVariant eval(QObject* root, const QString& code) {
    QQmlExpression expression(qmlContext(root), root, code);
    auto value = expression.evaluate();
    check(!expression.hasError(), expression.error().toString());
    return value;
}
void verifyItems(QQuickItem* item, QQuickItem* bar, int& actionCount) {
    if (!item->isVisible()) return;
    // Buttons expose down; labels expose text and elide. Ignore shadows/backgrounds.
    const bool button = item->metaObject()->indexOfProperty("down") >= 0;
    const bool label = item->metaObject()->indexOfProperty("elide") >= 0;
    if (button || label) {
        const auto point = item->mapToItem(bar, QPointF());
        const auto description = item->objectName() + " " + item->property("text").toString();
        check(point.x() >= 9 && point.x() + item->width() <= bar->width() - 9,
              "Overflow: " + description + " at " + QString::number(point.x())
              + " width " + QString::number(item->width()) + " bar " + QString::number(bar->width()));
        if (button && item->objectName() != "machineSelectorButton"
                   && item->objectName() != "printerStatusButton"
                   && item->objectName() != "preview.inspection.cursorSnap") {
            ++actionCount;
            check(std::abs(item->width() - item->implicitWidth()) <= 1,
                  "Action stretched/shrunk: " + description);
            // The reviewed theme specifies 12 px on each side. Keep this
            // independent of the implementation's implicit-size expression.
            check(item->property("leftPadding").toReal() == 12
                      && item->property("rightPadding").toReal() == 12,
                  "Action does not use the shared horizontal padding: " + description);
            const auto content = item->property("implicitContentWidth").toReal();
            check(item->width() >= content + 24.0 - 1
                      && item->width() <= std::max(36.0, content + 24.0) + 1,
                  "Excess action padding: " + description);
        }
    }
    for (auto* child : item->childItems()) verifyItems(child, bar, actionCount);
}
}

int main(int argc, char** argv) {
    QQuickStyle::setStyle("SmoothUI");
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir fixture;
        const auto module = fixture.path() + "/GPlatform";
        QDir().mkpath(module);
        QString qmldir = "module GPlatform\n";
        for (const auto* name : {"GlobalPrintCommandBar", "MachineSelectionBar", "PrinterStatusBar",
                                "PreviewInspectionHint", "PanelSurface", "AppSurface", "AppPopup",
                                "PrintPopoverHeader"}) {
            const auto file = QString::fromLatin1(name) + ".qml";
            check(QFile::copy(QStringLiteral(PRINTER_QML_ROOT) + "/components/" + file,
                             module + '/' + file), "Copy " + file);
            qmldir += QString::fromLatin1(name) + " 1.0 " + file + '\n';
        }
        check(QFile::copy(QStringLiteral(PRINTER_QML_ROOT) + "/global/PrintWorkspaceStyle.qml",
                         module + "/PrintWorkspaceStyle.qml"), "Copy style");
        qmldir += "singleton PrintWorkspaceStyle 1.0 PrintWorkspaceStyle.qml\n";
        // The settings editor is outside the toolbar's layout and remains closed.
        write(module + "/SlicingSettingsContent.qml", R"(
import QtQuick
Item { property bool embedded: false; property int workspaceIndex: 0
 signal closeRequested(); function focusSetting(key) {} }
)");
        qmldir += "SlicingSettingsContent 1.0 SlicingSettingsContent.qml\n";
        auto singleton = [&](const QString& name, const QString& body) {
            write(module + '/' + name + ".qml", "pragma Singleton\nimport QtQuick\nQtObject {\n" + body + "\n}");
            qmldir += "singleton " + name + " 1.0 " + name + ".qml\n";
        };
        singleton("PrinterService", R"(
 enum State { JobIdle, Printing, Paused, Pausing, Resuming, Stopping, Completed, JobError,
              TransferIdle, TransferUploading, TransferStarting, TransferUploaded, TransferUnknown,
              TransferFailed, Connecting, ConnectionError, TransferCancelled, Cancelled }
 property int jobState: 0
 property int transferState: 8
 property int connectionState: 0
 property bool connected: true
 property bool printing: jobState >= 1 && jobState <= 5
 property bool jobNoticeIsSubmission: true
 property bool jobNoticeDismissed: false
 property bool canCancelSubmission: transferState === 9
 property bool canRefreshStatus: true
 property int submissionCancellations: 0
 property int statusQueries: 0
 function cancelSubmission() { submissionCancellations++ }
 function refreshPrinterStatus() { statusQueries++ }
 property bool commandOutcomeUnknown: false
 property bool transferBusy: false
 property bool jobControllable: true
 property bool agentAvailable: true
 property bool selectedDeviceOnline: true
 property bool deviceAccessRequired: false
 property bool supportsUpload: true
 property bool supportsCancel: true
 property bool supportsPauseResume: true
 property string selectedDeviceId: "device-A"
 property string selectedDeviceName: "Printer A"
 property string selectedDeviceIp: "192.0.2.1"
 property string agentError: ""
 property string connectionError: ""
 property string jobError: ""
 property string jobFileName: "sample.gcode"
 property string submissionDisplayName: jobFileName
 property string remainingTime: "10 min"
 property real nozzleTemperature: 200
 property real targetNozzleTemperature: 210
 property real bedTemperature: 50
 property real targetBedTemperature: 60
 property real transferProgress: 0.4
 property real printProgress: 40
 property var supportedPrintFormats: ["gcode"]
)");
        singleton("SliceSettingsBridge", R"(
 property bool hasPendingReslice: false
 property string configurationError: ""
 property var selectedMachine: ({printerModel: "Flashforge AD5X", nozzleDiameter: "0.4"})
 property var filamentSlots: [{materialType: "PLA", color: "#e04b48"}]
 property var compatibleProcessPresets: []
 property var compatibleFilamentPresets: []
 property string selectedFilamentPresetName: "PLA"
 property string selectedProcessPresetName: "0.20 mm Standard"
 property string selectedMachineModelId: ""
 property string selectedMachineVariantId: ""
 property string selectedProcessPresetId: ""
 signal machineSelectionChanged()
 signal presetSelectionChanged()
 signal settingChanged(string key, var value)
 function machineModels() { return [] }
 function buildPlateOptions() { return [] }
 function getSetting(key) { return "Textured PEI Plate" }
)");
        qmlRegisterSingletonInstance("GPlatform", 1, 0, "SlicingPreviewBridge", SlicingPreviewBridge::instance());
        const auto inputPath = fixture.filePath("sample.gcode");
        write(inputPath, "G90\nG1 X1\nG1 X2\n");
        auto preview = trans::TransDB::create<GPlatform::ToolpathPreviewDB>();
        GPlatform::ToolpathPreviewData data; data.printGCodePath = inputPath.toStdString();
        preview->setPreviewData(std::move(data));
        auto* bridge = SlicingPreviewBridge::instance();
        bridge->addToolpathPreview(17, "sample", preview);
        singleton("FilePicker", R"(
 property int nextId: 1
 property var lastRequest: ({})
 signal accepted(int requestId, var urls)
 signal rejected(int requestId)
 function saveFile(options) { lastRequest = options; return nextId++ }
)");
        singleton("TaskStateNotifier", R"(
 property bool isBusy: false
 property real currentProgress: 0.35
 property int taskCount: 1
 property string currentDescription: "Slicing model"
)");
        write(module + "/qmldir", qmldir);
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(PRINTER_TEST_IMPORTS));
        engine.addImportPath(fixture.path());
        QStringList warnings;
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        QQmlComponent component(&engine);
        const auto central = source("workspaces/slicing/centralPart.qml");
        const auto functions = central.mid(central.indexOf("    function toLocalFilePath("),
            central.indexOf("    function buildManualSupportSettingsParams(") - central.indexOf("    function toLocalFilePath("));
        const auto acceptedCallback = block(central, "    Connections {\n        target: FilePicker");
        const auto exportHandler = block(source("pages/3DPrintPage.qml"), "        onExportRequested: function(formatId)");
        const auto page = source("pages/3DPrintPage.qml");
        const auto cancelStart = page.indexOf("        onCancelSubmissionRequested:");
        const auto cancelEnd = page.indexOf("        onStopPrintRequested:", cancelStart);
        check(cancelStart >= 0 && cancelEnd > cancelStart, "Missing production cancellation wiring");
        const auto cancellationHandlers = page.mid(cancelStart, cancelEnd - cancelStart);
        component.setData(QString::fromUtf8(R"(
import QtQuick
import QtQuick.Controls
import SmoothUI
import SmoothUI.Controls
import GPlatform
ApplicationWindow {
 id: root
 width: 1600; height: 160; visible: true
 Item {
  id: centralFragment
  anchors.fill: parent
  property int exportFileRequestId: 0
  property string exportFormatId: ""
  property string exportSourcePath: ""
  __FUNCTIONS__
  __ACCEPTED__
 }
 QtObject { id: previewPage; property var workspaceContent: centralFragment }
 GlobalPrintCommandBar {
  id: bar; objectName: "bar"
  anchors.centerIn: parent
  __EXPORT_HANDLER__
  __CANCEL_HANDLERS__
  popupHost: root.contentItem
  width: Math.min(parent.width - PrintWorkspaceStyle.space16 * 2, implicitWidth)
 }
}
)").replace("__FUNCTIONS__", functions).replace("__ACCEPTED__", acceptedCallback)
            .replace("__EXPORT_HANDLER__", exportHandler).replace("__CANCEL_HANDLERS__", cancellationHandlers).toUtf8(), QUrl::fromLocalFile(fixture.path() + "/Check.qml"));
        std::unique_ptr<QObject> root(component.create());
        check(root != nullptr, component.errorString());
        auto* bar = root->findChild<QQuickItem*>("bar");
        check(bar != nullptr, "Missing bar");
        check(eval(root.get(), "Theme.controlHorizontalPadding === 12 && Theme.controlHeight === 36").toBool(),
              "Unexpected shared theme dimensions");
        eval(root.get(), "PrinterService.transferState = PrinterService.TransferUploading");
        QTest::qWait(100);
        auto* cancelUpload = root->findChild<QObject*>("printerCancelSubmissionButton");
        check(cancelUpload && cancelUpload->property("visible").toBool(), "Uploading lacks the real Cancel button");
        check(QMetaObject::invokeMethod(cancelUpload, "clicked"), "Cannot click real upload Cancel");
        check(eval(root.get(), "PrinterService.submissionCancellations === 1").toBool(), "Bar/Page wiring did not cancel the submission");
        eval(root.get(), "PrinterService.canCancelSubmission = false");
        check(!cancelUpload->property("visible").toBool(), "Unsupported upload cancellation remained visible");
        check(QMetaObject::invokeMethod(bar, "checkStatusRequested"), "Missing real status-refresh signal");
        check(eval(root.get(), "PrinterService.statusQueries === 1").toBool(), "Bar/Page wiring did not refresh status");
        eval(root.get(), "PrinterService.transferState = PrinterService.TransferIdle");
        // Terminal caption and title use the same physical/submission context.
        eval(root.get(), "PrinterService.transferState = PrinterService.TransferUploaded;"
             "PrinterService.jobFileName = 'Physical B.gcode'; PrinterService.submissionDisplayName = 'Upload A.gcode';"
             "PrinterService.jobNoticeIsSubmission = false; PrinterService.jobState = PrinterService.Cancelled");
        QTest::qWait(100);
        auto* terminalTitle = root->findChild<QObject*>("printerJobTerminalTitle");
        check(terminalTitle && terminalTitle->property("text").toString() == "Print cancelled"
                  && bar->property("currentJobName").toString() == "Physical B.gcode",
              "Physical B cancellation inherited upload A's title or caption");
        eval(root.get(), "PrinterService.jobState = PrinterService.JobIdle; PrinterService.jobFileName = ''");
        check(!bar->property("terminalJobVisible").toBool(), "Stale uploaded A reopened a terminal card after B became idle");
        eval(root.get(), "PrinterService.jobState = PrinterService.Cancelled; PrinterService.transferState = PrinterService.TransferFailed;"
             "PrinterService.jobNoticeIsSubmission = true");
        QTest::qWait(100);
        terminalTitle = root->findChild<QObject*>("printerJobTerminalTitle");
        check(terminalTitle && terminalTitle->property("text").toString() == "Transfer failed"
                  && bar->property("currentJobName").toString() == "Upload A.gcode",
              "Old physical cancellation replaced a new submission failure");
        eval(root.get(), "PrinterService.transferState = PrinterService.TransferIdle;"
             "PrinterService.jobState = PrinterService.JobIdle; PrinterService.jobFileName = 'sample.gcode';"
             "PrinterService.submissionDisplayName = 'sample.gcode'");
        eval(root.get(), "PrinterService.canCancelSubmission = Qt.binding(function() { return PrinterService.transferState === PrinterService.TransferUploading; })");
        int cases = 0;
        const auto exerciseLayout = [&](int width, int state, bool disconnected, bool chinese) {
            root->setProperty("width", width);
            eval(root.get(), "bar.workspaceName = 'normal'; bar.objectCount = 0;"
                 "PrinterService.jobState = PrinterService.JobIdle;"
                 "PrinterService.transferState = PrinterService.TransferIdle;"
                 "TaskStateNotifier.isBusy = false;");
            if (state == 1) eval(root.get(), "bar.objectCount = 3");
            if (state == 2) eval(root.get(), "bar.workspaceName = 'slicing'");
            if (state == 3) eval(root.get(), "PrinterService.jobState = PrinterService.Printing");
            if (state == 4) eval(root.get(), "PrinterService.jobState = PrinterService.Pausing");
            if (state == 5) eval(root.get(), "PrinterService.transferState = PrinterService.TransferUploading");
            if (state == 6) eval(root.get(), "PrinterService.jobState = PrinterService.JobError");
            if (state == 7) eval(root.get(), "TaskStateNotifier.isBusy = true");
            eval(root.get(), disconnected
                 ? "PrinterService.connected = false; PrinterService.connectionState = PrinterService.ConnectionError"
                 : "PrinterService.connected = true; PrinterService.connectionState = 0");
            // SmoothUI animates control geometry when a branch changes.
            QTest::qWait(160);
            if (state == 5) {
                auto* currentCancel = root->findChild<QObject*>("printerCancelSubmissionButton");
                check(currentCancel && currentCancel->property("visible").toBool(), "Uploading layout omitted the new Cancel action");
            }
            int actions = 0;
            verifyItems(bar, bar, actions);
            if (state <= 2) {
                check(actions == (state == 2 ? 3 : 1), "Unexpected visible command-bar actions");
                const auto* parameters = root->findChild<QQuickItem*>("print.configuration.open");
                check(parameters == nullptr, "The redundant global-process command must not be created");
            }
            auto* normalPrimary = root->findChild<QQuickItem*>("printNormalPrimaryButton");
            auto* sendPrimary = root->findChild<QQuickItem*>("printerSendButton");
            if (state <= 2) {
                auto* primary = state == 2 ? sendPrimary : normalPrimary;
                check(primary && primary->isVisible(), "Missing primary workflow action");
                const auto expected = chinese
                    ? (state == 0 ? QStringLiteral("添加模型")
                       : state == 1 ? QStringLiteral("开始切片")
                       : disconnected ? QStringLiteral("连接后发送") : QStringLiteral("发送到打印机"))
                    : (state == 0 ? QStringLiteral("Add Model")
                       : state == 1 ? QStringLiteral("Start Slicing")
                       : disconnected ? QStringLiteral("Connect to Send") : QStringLiteral("Send to Printer"));
                check(primary->property("text").toString() == expected,
                      "Primary workflow text hidden or changed at width " + QString::number(width));
                check(std::abs(primary->height() - 40) <= 1
                          && primary->property("font").value<QFont>().pixelSize() == 14,
                      "Primary workflow action does not use 40 px / body typography");
            } else {
                check((!normalPrimary || !normalPrimary->isVisible())
                          && (!sendPrimary || !sendPrimary->isVisible()),
                      "Stale primary action overlays the active workflow state");
            }
            check(std::abs(bar->width() - std::min(width - 32.0, bar->implicitWidth())) <= 1,
                  "Bar does not wrap content");
            ++cases;
            if (width == 1600 && disconnected && (state == 0 || state == 2)) {
                const auto path = qEnvironmentVariable("COMMAND_BAR_SCREENSHOTS");
                if (!path.isEmpty()) {
                    QDir().mkpath(path);
                    qobject_cast<QQuickWindow*>(root.get())->grabWindow().save(
                        path + (state == 0 ? "/empty.png" : "/disconnected.png"));
                }
            }
        };
        for (int width : {2000, 1600, 1440, 1280, 1279, 1200, 1024, 800, 640}) {
            for (int state = 0; state < 8; ++state)
                for (bool disconnected : {false, true}) exerciseLayout(width, state, disconnected, false);
        }
        ChineseCommandBarTranslator chinese;
        check(app.installTranslator(&chinese), "Install Chinese workflow labels");
        engine.retranslate();
        eval(root.get(), "PrinterService.selectedDeviceName = '工艺验证打印机设备名称包含很长的中文说明';"
             "PrinterService.jobFileName = '工艺验证模型文件名称包含很长的中文说明.gcode';"
             "SliceSettingsBridge.selectedMachine = ({printerModel: '连续纤维复合材料工艺验证打印机', nozzleDiameter: '0.4'});"
             "SliceSettingsBridge.selectedProcessPresetName = '连续纤维直线填充工艺验证参数预设';"
             "TaskStateNotifier.currentDescription = '正在切片包含连续纤维轮廓与直线填充的工艺验证模型';"
             "bar.selectedFileName = '连续纤维工艺验证模型文件名称包含很长的中文说明.stl'; bar.hasSelection = true;");
        for (int width : {1200, 800, 640}) {
            for (int state = 0; state < 8; ++state)
                for (bool disconnected : {false, true}) exerciseLayout(width, state, disconnected, true);
        }
        app.removeTranslator(&chinese);
        engine.retranslate();
        // A03/A04: real preview command-bar branch, production export route and save callback.
        const auto bed = trans::TransDB::create<PrintBedDB>();
        const auto sourceModel = trans::TransDB::create<ModelInstanceDB>();
        sourceModel->setParentPrintBedDBId(bed->getDBInstanceID());
        for (bool dbFailure : {false, true}) {
            const auto newInput = fixture.filePath(dbFailure ? "db-failure.gcode" : "no-preview.gcode");
            write(newInput, dbFailure ? "G90\nG1 X30 E2 F600\n" : "G90\nG1 X40 E3 F900\n");
            auto output = std::make_shared<GPlatform::SliceOutputRecord>();
            output->setArtifacts(newInput, false, {}, false);
            output->sourceScope = GPlatform::ToolpathPreviewSourceScope::Plate;
            output->printBedId = bed->getDBInstanceID();
            output->sourceModelIds = {sourceModel->getDBInstanceID()};
            output->name = "sample";
            GPlatform::PreparedToolpathPreview incompatible;
            incompatible.error = "Incompatible preview schema";
            incompatible.geometryAvailable = dbFailure;
            const auto published = GPlatform::publishToolpathResult(output, incompatible, bridge->beginOutputTask(), [=](auto stage) {
                if (dbFailure && stage == GPlatform::PreviewPublicationStage::Create) throw std::runtime_error("Injected DB creation failure");
            });
            check(published.accepted && !bridge->geometryAvailable(), "Install output-only result");
            eval(root.get(), "bar.workspaceName = 'slicing'; TaskStateNotifier.isBusy = false; PrinterService.jobState = PrinterService.JobIdle; PrinterService.transferState = PrinterService.TransferIdle;");
            QTest::qWait(100);
            auto* exportButton = root->findChild<QQuickItem*>("printOutputExportButton");
            check(exportButton && exportButton->isVisible() && exportButton->property("enabled").toBool(), "Output-only export button unreachable");
            check(QMetaObject::invokeMethod(exportButton, "clicked"), "Open export menu");
            QTest::qWait(50);
            auto* exportItem = root->findChild<QObject*>("printOutputExportGCode");
            check(exportItem && exportItem->property("enabled").toBool(), "Output-only G-code menu disabled");
            check(QMetaObject::invokeMethod(exportItem, "triggered"), "Trigger production export handler");
            check(eval(root.get(), "centralFragment.exportFileRequestId > 0 && FilePicker.lastRequest.title.length > 0").toBool(), "Production save request not reached");
            const auto destination = fixture.filePath(dbFailure ? "db-failure-export.gcode" : "output-only-export.gcode");
            const auto urls = QString::fromUtf8(QJsonDocument(QJsonArray{QUrl::fromLocalFile(destination).toString()}).toJson(QJsonDocument::Compact));
            eval(root.get(), "FilePicker.accepted(centralFragment.exportFileRequestId, " + urls + ")");
            QFile saved(destination), original(newInput);
            check(saved.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly) && saved.readAll() == original.readAll(), "Output-only saved incorrect bytes");
        }
        auto* exportButton = root->findChild<QQuickItem*>("printOutputExportButton");
        eval(root.get(), "SliceSettingsBridge.hasPendingReslice = true");
        QTest::qWait(30);
        check(!exportButton->property("enabled").toBool(), "Dirty output bypassed command-bar guard");
        bridge->clearPreview();
        check(warnings.isEmpty(), warnings.join('\n'));
        std::cout << "Command bar layout: " << cases << " cases passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
