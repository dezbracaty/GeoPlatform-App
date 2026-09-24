#pragma once

#include "StandardActionHandler.hpp"

#include <QImage>

/**
 * @brief 截图处理器
 *
 * 处理 "screenshot.capture" action，并提供应用截图数据给 Journal。
 * 两个入口共用同一抓图实现。
 */
class ScreenshotHandler : public StandardActionHandler {
    Q_OBJECT

public:
    explicit ScreenshotHandler(QObject* parent = nullptr);

    // Returns pixels only. The caller decides whether and where to persist or
    // compare them.
    static QImage captureApplicationWindow();

    // Screenshot capture is a one-shot command and must not replace the
    // currently active interactive tool.
    HandlerType getHandlerType() const override { return HandlerType::Middleware; }

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

protected:
    void onEnter(std::shared_ptr<ActionContext> context) override;

private:
    /**
     * @brief 生成截图文件名
     * @param description 截图描述（可选）
     * @return 完整的截图文件路径
     */
    QString generateScreenshotPath(const QString& description = QString()) const;

    /**
     * @brief 抓取当前可见的应用主窗口
     * @param filename 截图文件路径
     * @return 是否成功
     */
    bool captureScreenshot(const QString& filename) const;

    /**
     * @brief 显示截图结果通知
     * @param success 是否成功
     * @param filepath 截图文件路径
     */
    void showScreenshotNotification(bool success, const QString& filepath) const;

private:
};
