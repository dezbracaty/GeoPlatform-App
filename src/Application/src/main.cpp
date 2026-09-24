#include "AppInfo.h"
#include "SettingsHelper.h"
#include "ApplicationHelper.h"
#include "CrashHandler.hpp"
#include "ApplicationModuleRegistration.hpp"
#include "ScreenshotHandler.hpp"
#include "Foundation/Log.h"
#include <DocumentManager.hpp>
#include <SystemTypes.hpp>
#include <TransDBSerialization.hpp>
#include <QGuiApplication>
#include <chrono>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <InteractionRuntime.hpp>
#include <RenderingBootstrap.hpp>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QStandardPaths>

#include "ActionManager.hpp"
#include "MenuData.hpp"

#include <exception>
#include <iostream>
#include <QProcess>
#include <QTimer>

#ifdef JOURNALSYSTEM_ENABLED
#include "JournalSystem.h"
#endif

using namespace Qt::StringLiterals;

namespace {
class LogShutdownGuard final {
public:
    ~LogShutdownGuard()
    {
        Log::shutdown();
    }

    LogShutdownGuard() = default;
    LogShutdownGuard(const LogShutdownGuard&) = delete;
    LogShutdownGuard& operator=(const LogShutdownGuard&) = delete;
};
}

#ifdef SMOOTHUI_BUILD_STATIC_LIB
// Import the correct static plugin names (matching the demangled symbols)
Q_IMPORT_QML_PLUGIN(RenderPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUIPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUI_implPlugin)
Q_IMPORT_QML_PLUGIN(SmoothUIDockPlugin)
#endif

static int runApplication(int argc, char* argv[], QString& restartExecutable,
                          QStringList& restartArguments) {
    // ========== 启动性能追踪 ==========
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    auto logElapsed = [&lastTime, &startTime](const char* stage) {
        auto now = std::chrono::high_resolution_clock::now();
        auto sinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
        LOG_INFO("[STARTUP TIMING] {} - elapsed: {}ms, total: {}ms", stage, sinceLast, sinceStart);
        lastTime = now;
    };
    // ========== 启动性能追踪结束 ==========

    GPlatform::Rendering::configureGraphicsBeforeApplication();

#ifdef SMOOTHUI_BUILD_STATIC_LIB
    // Initialize static Qt resources for SmoothUI modules
    // Resources are automatically initialized by the static libraries
#endif

    qputenv("QT_QUICK_CONTROLS_STYLE", "SmoothUI");
    QGuiApplication::setOrganizationName(PROJECT_COMPANY);
    QGuiApplication::setOrganizationDomain(PROJECT_DOMAIN);
    QGuiApplication::setApplicationName(PROJECT_APP_NAME);
    QGuiApplication::setApplicationDisplayName(PROJECT_APP_NAME);
    QGuiApplication::setApplicationVersion(PROJECT_VERSION);

    // Initialize the logging system (enhanced Log class with spdlog)
    Log::setup(argv, PROJECT_APP_NAME);
    // Declared before Qt application objects so reverse destruction keeps
    // spdlog alive while QML/KDDockWidgets tear down and emit final messages.
    LogShutdownGuard logShutdownGuard;
    logElapsed("Log system initialized");

    // 2. Create Qt application
    QGuiApplication app(argc, argv);
    logElapsed("QGuiApplication created");

    // Use platform-native glyph rasterization (CoreText on macOS) for crisp
    // static UI text. SmoothUI's explicit render type is set in App.qml.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    registerInteractionRuntime();
    GPlatform::Rendering::configureRenderViewLifecycleSink(
        interactionRenderViewLifecycleSink());

#ifdef Q_OS_MACOS
    // 在 macOS 上禁用原生菜单栏，让 MenuBar 在窗口内显示
    app.setAttribute(Qt::AA_DontUseNativeMenuBar, true);
#endif

#ifdef HAS_BREAKPAD
    // Initialize crash handler
    QString crashDumpPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/crashdumps";
    CrashHandler::instance().initialize(crashDumpPath.toStdString());

    // Set crash callback
    CrashHandler::instance().setCrashCallback([](const std::string& dumpPath) {
        LOG_ERROR("💥 崩溃报告已生成: {}", dumpPath);
        LOG_ERROR("请将崩溃报告文件发送给开发者进行分析");

        // TODO: 这里可以添加更多的处理，比如：
        // - 显示崩溃对话框
        // - 自动重启程序
        // - 上传崩溃报告到服务器
    });

    LOG_INFO("🛡️ 崩溃报告系统已启用，转储路径: {}", crashDumpPath.toStdString());
#else
    LOG_INFO("崩溃报告系统未启用（编译时未开启 ENABLE_BREAKPAD）");
#endif

    // Get command line arguments
    QStringList args = app.arguments();
    const int replayIdIndex = args.indexOf(QStringLiteral("--journal-replay-id"));
    const QString startupReplayId = replayIdIndex >= 0 ? args.value(replayIdIndex + 1) : QString();
    if (replayIdIndex >= 0 && (startupReplayId.isEmpty() || startupReplayId.startsWith("--"))) {
        LOG_ERROR("--journal-replay-id requires an identifier");
        return 1;
    }


    GPlatform::Rendering::configureGraphicsAfterApplication();

    // Initialize settings
    SettingsHelper::getInstance()->init(argv);

    LOG_INFO("Registering application modules...");
    registerApplicationModules();
    startDefaultInteractionHandlers();
    logElapsed("Application modules registered");

    QQmlApplicationEngine engine;
    engine.addImportPath(":/qt/qml");

    // Set the engine reference in ApplicationHelper for QML engine restart functionality
    ApplicationHelper::getInstance()->setEngine(&engine);

    // Module QML types are registered before the engine loads components.

    AppInfo::getInstance()->init(&engine);
    logElapsed("AppInfo initialized");

    const QUrl url(u"qrc:/qt/qml/GPlatform/qml/global/App.qml"_s);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url, &logElapsed](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
            else if (obj && url == objUrl)
                LOG_INFO("[STARTUP TIMING] QML App.qml loaded");
        },
        Qt::QueuedConnection);
    // Load application menus before QML captures groups from MenuData.
    LOG_INFO("Loading application menu configuration...");
    MenuData::getInstance()->loadMenusFromFile(":/qt/qml/GPlatform/config/app_menus.json");
    LOG_INFO("Application menu configuration loaded successfully");
    logElapsed("Menu configuration loaded");

    engine.load(url);
    logElapsed("QML engine load started");

#ifdef JOURNALSYSTEM_ENABLED
    // Initialize JournalSystem for UI testing support
    JournalSystem* journal = new JournalSystem(&app);
    journal->setActionInvoker(
        [](const QString& actionCode,
           const QVariantMap& params,
           QString& error) {
            if (actionCode.trimmed().isEmpty()) {
                error = QStringLiteral("Action code is empty");
                return false;
            }
            auto* manager = ActionManager::getInstance();
            if (!manager) {
                error = QStringLiteral("ActionManager is unavailable");
                return false;
            }
            if (!manager->triggerAction(actionCode, params)) {
                error = QStringLiteral("Action was not accepted: %1")
                            .arg(actionCode);
                return false;
            }
            return true;
        });
    journal->setDbStateProvider(
        QString::fromLatin1(trans::SerializedStateFormat),
        [](const QString& dbInstanceId,
           QByteArray& serialized,
           QString& error) {
            const QString normalizedId = dbInstanceId.trimmed();
            bool parsed = false;
            const qlonglong rawId = normalizedId.toLongLong(&parsed);
            if (!parsed || rawId == 0 || QString::number(rawId) != normalizedId) {
                error = QStringLiteral("Invalid DBInstanceID: %1")
                            .arg(dbInstanceId);
                return false;
            }

            auto* document = DocumentManager::instance();
            if (!document) {
                error = QStringLiteral("DocumentManager is unavailable");
                return false;
            }
            const auto object = document->getDBInstance(DBInstanceID(rawId));
            if (!object) {
                error = QStringLiteral("DBInstanceID does not exist: %1")
                            .arg(normalizedId);
                return false;
            }

            try {
                const trans::SerializedState state = trans::serializeState(*object);
                serialized = QByteArray(
                    reinterpret_cast<const char*>(state.data()),
                    static_cast<qsizetype>(state.size()));
                return true;
            } catch (const std::exception& exception) {
                error = QStringLiteral("Cannot serialize DB %1: %2")
                            .arg(normalizedId,
                                 QString::fromUtf8(exception.what()));
                return false;
            }
        });
    journal->setScreenshotProvider([] {
        return ScreenshotHandler::captureApplicationWindow();
    });

    int ghostPort = 9999;

    // Check for custom port
    for (int i = 0; i < args.size(); i++) {
        if (args[i] == "--ghost-port") {
            bool parsed = false;
            ghostPort = args.value(i + 1).toInt(&parsed);
            if (!parsed) ghostPort = -1;
            break;
        }
    }

    // Python owns Journal timing and validation; the application owns its restart.
    if (ghostPort < 1 || ghostPort > 65535) {
        LOG_ERROR("Invalid Journal port: {}", ghostPort);
        delete journal;
        return 1;
    }
    journal->setStartupReplayId(startupReplayId);
    journal->setReplayPreparationHandler([args, ghostPort](const QString& replayId, QString& error) {
        QStringList nextArguments;
        for (int i = 1; i < args.size(); ++i) {
            const QString& argument = args.at(i);
            if (argument == "--ghost-port" || argument == "--journal-replay-id" ||
                argument == "--ghost-file") {
                ++i;
                continue;
            }
            if (argument == "--restart" || argument == "--ghost-play" ||
                argument == "--ghost-record" ||
                argument == "--ghost-verify") continue;
            nextArguments.append(argument);
        }
        nextArguments << "--ghost-port" << QString::number(ghostPort)
                      << "--journal-replay-id" << replayId;
        LOG_INFO("Journal replay {}: scheduling application restart", replayId.toStdString());
        return ApplicationHelper::getInstance()->scheduleRestart(nextArguments, &error);
    });
    if (engine.rootObjects().isEmpty()) {
        delete journal;
        return 1;
    }
    // Fixed startup offset, as in the former startup replay entry. No business waits.
    const auto startJournal = [journal, ghostPort, startupReplayId] {
        if (journal->init(static_cast<quint16>(ghostPort)) != 0) {
            LOG_ERROR("Journal could not listen on port {}", ghostPort);
            QCoreApplication::exit(1);
        } else {
            LOG_INFO("Journal service ready; startup replay={}", startupReplayId.toStdString());
        }
    };
    QTimer::singleShot(startupReplayId.isEmpty() ? 0 : 500, journal, startJournal);
#endif

    const int exec = QGuiApplication::exec();

#ifdef JOURNALSYSTEM_ENABLED
    delete journal;
#endif

    LOG_INFO("Application event loop exited with code {}", exec);

    restartArguments = ApplicationHelper::getInstance()->takeRestartArguments();
    if (!restartArguments.isEmpty() && exec == 0) {
        restartExecutable = QCoreApplication::applicationFilePath();
    }

    return exec;
}

int main(int argc, char* argv[]) {
    QString executable;
    QStringList arguments;
    const int result = runApplication(argc, argv, executable, arguments);
    // runApplication has destroyed the old Journal server, QML engine and app.
    if (result == 0 && !executable.isEmpty()) {
        if (!QProcess::startDetached(executable, arguments)) {
            std::cerr << "Journal application restart failed: " << executable.toStdString() << '\n';
            return 1;
        }
    }
    return result;
}
