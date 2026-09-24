#pragma once

#include "StandardBackgroundHandler.hpp"
#include "ModelLoaderUtil.hpp"
#include <GlbAssetData.hpp>
#include <libslicer/Library.hpp>
#include <QFutureWatcher>
#include <QString>
#include <QObject>
#include <memory>
#include <atomic>

// 前置声明
struct GeomTriangle;

/**
 * @brief 统一的模型导入处理器
 *
 * 支持多种3D模型格式的导入，使用 ModelLoaderUtil 进行实际的文件加载。
 * 普通网格格式由 ModelLoaderUtil 加载；3MF 项目由 libslicer 加载。
 */
class UnifiedModelImportHandler : public StandardBackgroundHandler {
    Q_OBJECT
    Q_DISABLE_COPY(UnifiedModelImportHandler)

public:
    explicit UnifiedModelImportHandler(QObject* parent = nullptr);
    ~UnifiedModelImportHandler() override;

protected:
    const QHash<QString, QVariantMap>& aiDescriptorTable() const;

public:
    std::optional<QVariantMap> getAIDescriptor(const QString& actionCode) const override;

Q_SIGNALS:
    // 导入进度信号
    void importProgress(int percentage, const QString& message);

    // 导入完成信号
    void importCompleted(const QString& objectId, bool success, const QString& errorMessage = QString());

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
            return QString("正在导入 %1...").arg(m_name);
        }
        return QString("正在导入模型...");
    }

private:
    // 参数
    QString m_filePath;
    Vector3 m_position{0, 0, 0};
    float m_scale{1.0f};
    Vector3 m_color{0.8f, 0.8f, 0.8f}; // 默认灰色
    QString m_name;

    // 加载结果 - 直接使用ModelLoaderUtil的LoadResult类型
    ModelLoaderUtil::LoadResult m_loadResult;
    GlbImportResult m_glbLoadResult;
    libslicer::ProjectImportResult m_projectLoadResult;
    bool m_isProjectImport{false};
    bool m_isGlbImport{false};

    // 创建的对象ID
    QString m_createdObjectId;
    QStringList m_createdObjectIds;
    std::unique_ptr<QFutureWatcher<std::shared_ptr<libslicer::ProjectImportResult>>>
        m_projectImportWatcher;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;

    // 后台加载模型
    void loadModelInBackground();
    void loadProjectInBackground();
    void onProjectImportFinished();

    void createModelFromResult();
    void createModelsFromProject();

    void applyTransformations(
        const std::shared_ptr<class ModelInstanceDB>& instance,
        const Vector3& importPosition);

    // 选择目标打印床并计算普通网格的初始摆放位置。越界状态由
    // PrintBedRelations 根据模型世界 BBox 维护，不在导入器中检测。
    std::shared_ptr<class PrintBedDB> resolveTargetPrintBed() const;
    Vector3 calculateImportPosition(
        const Vector3& modelSize,
        const std::shared_ptr<class PrintBedDB>& targetPrintBed) const;
};
