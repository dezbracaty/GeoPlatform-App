#pragma once

#include "StandardBackgroundHandler.hpp"
#include <libslicer/Toolpath.hpp>
#include <libslicer/Library.hpp>
#include <QFutureWatcher>
#include <QString>
#include <QObject>
#include <atomic>
#include <memory>

/**
 * @brief GCode 文件导入处理器
 *
 * 负责通过 libslicer 加载 GCode 文件，创建 ToolpathPreviewDB 投影。
 * 使用后台线程解析大文件，避免阻塞 UI。
 */
class GCodeImportHandler : public StandardBackgroundHandler {
    Q_OBJECT
    Q_DISABLE_COPY(GCodeImportHandler)

public:
    explicit GCodeImportHandler(QObject* parent = nullptr);
    ~GCodeImportHandler() override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const;

public:
    std::optional<QVariantMap> getAIDescriptor(const QString& actionCode) const override;

Q_SIGNALS:
    // 导入进度信号
    void importProgress(int percentage, const QString& message);

    // 导入完成信号
    void importCompleted(const QString& objectId, bool success, const QString& errorMessage = QString());

    // 请求切换到切片工作区
    void requestSwitchToSlicingWorkspace();

protected:
    // StandardBackgroundHandler 接口实现
    bool onInitialize() override;
    void onBackgroundStarted() override;
    void onBackgroundCompleted() override;
    void onCleanup() override;
    QVariant getCompletionResult() const override;

    // 任务描述（显示在状态栏）
    QString getTaskDescription() const override {
        if (!m_name.isEmpty()) {
            return QString("正在加载 %1...").arg(m_name);
        }
        return QString("正在加载 GCode 文件...");
    }

private:
    // 参数
    QString m_filePath;
    QString m_name;

    // 加载结果
    int m_layerCount{0};
    int m_segmentCount{0};
    int m_pointCount{0};
    float m_totalTime{0.0f};
    float m_totalExtrusion{0.0f};
    bool m_success{false};
    QString m_errorMessage;

    // 创建的对象 ID
    QString m_createdObjectId;
    libslicer::ToolpathPreviewPtr m_preview;
    std::unique_ptr<QFutureWatcher<libslicer::GCodePreviewResult>> m_parseWatcher;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;

    // 在 QtConcurrent 工作线程中请求 libslicer 生成统一刀路预览。
    void parseGCodeInBackground();
    void onParseFinished();

    void createToolpathPreviewDB();
};
