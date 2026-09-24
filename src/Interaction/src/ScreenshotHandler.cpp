#include "ScreenshotHandler.hpp"
#include "InteractionRegistration.hpp"

REGISTER_INTERACTION_ACTION(ScreenshotHandler, "screenshot.capture")
#include "ActionHandlerRegistry.hpp"
#include "ActionContext.hpp"
#include "AIDescriptorHelper.hpp"
#include "NotificationManager.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPixmap>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <QWindow>
#include <QImage>

#include "Foundation/Log.h"

ScreenshotHandler::ScreenshotHandler(QObject* parent)
    : StandardActionHandler(parent) {
}

void ScreenshotHandler::onEnter(std::shared_ptr<ActionContext> context) {
    // 调用父类的onEnter设置基本状态
    StandardActionHandler::onEnter(context);

    // 获取参数
    QString description = getParam("description", "manual_screenshot").toString();
    QString customPath = getParam("filepath").toString();
    const bool notify = getParam("notify", true).toBool();

    bool success = false;
    QString screenshotPath;

    // screenshot.capture is a product Action. Journal checkpoints use the
    // separate app.screenshot.capture data endpoint and are saved by Python.
    screenshotPath = !customPath.isEmpty() ? customPath : generateScreenshotPath(description);
    success = captureScreenshot(screenshotPath);

    // 显示结果通知
    if (notify) {
        showScreenshotNotification(success, screenshotPath);
    }
    if (context) {
        if (success) {
            context->setResult(QVariantMap{{"success", true}, {"path", screenshotPath}});
        } else if (!context->hasError()) {
            context->setError(ActionErrorCode::Internal, "Screenshot capture failed");
        }
    }

    // 截图是即时操作，完成后立即退出
    onExit();
}

const QHash<QString, QVariantMap>& ScreenshotHandler::aiDescriptorTable() const {
    static const QHash<QString, QVariantMap> table = {
        {"screenshot.capture", makeDescriptor(
            "screenshot.capture",
            "Capture Screenshot",
            "Capture a screenshot.",
            "write",
            {
                {"description", "string", false},
                {"filepath", "string", false},
                {"notify", "bool", false}
            },
            {"screenshot", "capture", "file"}
        )}
    };
    return table;
}

QString ScreenshotHandler::generateScreenshotPath(const QString& description) const {
    QString appDataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QGuiApplication::applicationDirPath();
    }
    const QString screenshotDir = appDataPath + "/Screenshots";
    QDir().mkpath(screenshotDir);

    // 生成文件名：使用更精确的时间戳
    QString timestamp = QDateTime::currentDateTime().toString("HHmmss_zzz");
    QString filename;

    if (!description.isEmpty() && description != "manual_screenshot") {
        filename = QString("screenshot_%1_%2.png")
                   .arg(timestamp)
                   .arg(description);
    } else {
        filename = QString("screenshot_%1.png").arg(timestamp);
    }

    return screenshotDir + "/" + filename;
}

QImage ScreenshotHandler::captureApplicationWindow() {
    // allWindows().first() is not stable once Dock popups or dialogs exist.
    // The main application window is the largest visible top-level Quick
    // window; transient windows remain part of the native surface capture.
    QQuickWindow* quickWindow = nullptr;
    qint64 largestArea = -1;
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        auto* candidate = qobject_cast<QQuickWindow*>(window);
        if (!candidate || !candidate->isVisible()) {
            continue;
        }

        const qint64 area = static_cast<qint64>(candidate->width()) * candidate->height();
        if (area > largestArea) {
            largestArea = area;
            quickWindow = candidate;
        }
    }

    if (!quickWindow) {
        LOG_ERROR("ScreenshotHandler: no visible top-level QQuickWindow");
        return {};
    }

    QImage screenshot;
    QString captureMethod;

    // Screen-backed capture is only trustworthy while this application owns
    // the foreground. On macOS QScreen::grabWindow() may otherwise return the
    // pixels of another application covering the same screen rectangle.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        quickWindow->raise();
        quickWindow->requestActivate();

        QElapsedTimer activationTimer;
        activationTimer.start();
        while (QGuiApplication::applicationState() != Qt::ApplicationActive
               && activationTimer.elapsed() < 400) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
    }

    // QScreen::grabWindow() goes through the platform window system and
    // captures what was actually composited for this native window. This is
    // the important difference from QQuickWindow::grabWindow() for the VTK +
    // Retina rendering path used by GPlatform.
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        if (QScreen* screen = quickWindow->screen()) {
            const QPixmap nativeCapture = screen->grabWindow(quickWindow->winId());
            if (!nativeCapture.isNull()) {
                screenshot = nativeCapture.toImage();
                captureMethod = QStringLiteral("QScreen::grabWindow");
            }
        }
    }

    if (screenshot.isNull()) {
        screenshot = quickWindow->grabWindow();
        captureMethod = QStringLiteral("QQuickWindow::grabWindow fallback");

        // grabWindow() returns the complete window at the backing-store pixel size on Retina.
        // Cropping that image to the logical QSize discards the right and bottom of the UI; keep the
        // physical-pixel image intact just like QScreen::grabWindow().
    }

    if (screenshot.isNull()) {
        LOG_ERROR("ScreenshotHandler: both native and QQuickWindow capture failed");
        return {};
    }

    LOG_INFO("ScreenshotHandler: method={} window={}x{} image={}x{} dpr={}",
             captureMethod.toStdString(), quickWindow->width(), quickWindow->height(),
             screenshot.width(), screenshot.height(), quickWindow->devicePixelRatio());

    return screenshot;
}

bool ScreenshotHandler::captureScreenshot(const QString& filename) const {
    const QImage screenshot = captureApplicationWindow();
    if (screenshot.isNull()) {
        return false;
    }

    QFileInfo fileInfo(filename);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return screenshot.save(filename, "PNG");
}

void ScreenshotHandler::showScreenshotNotification(bool success, const QString& filepath) const {
    auto* notificationManager = NotificationManager::instance();
    if (!notificationManager) {
        return;
    }

    if (success) {
        QString message = QString("截图已保存到: %1").arg(QFileInfo(filepath).fileName());
        notificationManager->showSuccess("截图成功", message);
    } else {
        notificationManager->showError("截图失败", "无法保存截图，请检查权限设置");
    }
}
