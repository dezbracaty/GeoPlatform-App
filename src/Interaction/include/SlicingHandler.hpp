#pragma once

#include "StandardActionHandler.hpp"
#include "ModelInstanceDB.hpp"
#include "ToolpathPreviewDB.hpp"
#include "SliceSession.hpp"
#include "SliceSessionBuilder.hpp"
#include <libslicer/Library.hpp>
#include "TaskStateNotifier.hpp"
#include <memory>
#include <vector>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <chrono>
#include <QFutureWatcher>
#include <QStringList>
#include <QtConcurrent>

class SlicingPlaybackHandler;

/**
 * @brief 切片处理器 (Persistent Handler)
 *
 * 作为持续性 Handler 管理切片操作和环境切换：
 * - 响应 "model.slice" / "model.sliceAll" / "slicing.reslice"
 *   action：执行切片，切换环境
 * - 响应 "support.manual.slice" action：使用手动支撑网格执行切片
 * - 响应 "model.importGCode" action：调用 GCodeImportHandler，切换环境
 * - 响应 "slicing.exit" action：清理可视化，切换回 normal 环境
 *
 * 设计原则：
 * 1. 持续性：切片/导入完成后不自动退出，保持在 slicing 环境中
 * 2. 环境管理：自动切换环境，并响应退出请求
 * 3. 后台任务：切片使用 QtConcurrent，导入调用 GCodeImportHandler
 */
class SlicingHandler : public StandardActionHandler {
    Q_OBJECT
    Q_DISABLE_COPY(SlicingHandler)

public:
    /**
     * @brief 操作模式
     */
    enum class Mode {
        Slice,   // 切片模式
        Import   // 导入 GCode 模式
    };

    /**
     * @brief Handler 状态
     */
    enum class State {
        Inactive,    // 未激活
        Preparing,   // 准备中
        Processing,  // 处理中
        Ready        // 就绪（完成）
    };

    explicit SlicingHandler(QObject* parent = nullptr);
    virtual ~SlicingHandler();

    HandlerType getHandlerType() const override { return HandlerType::Middleware; }

    // 持续性 Handler：不被 ActionManager 自动清理
    bool isPersistent() const override { return true; }

    // 获取当前状态
    State currentState() const { return m_state; }
    Mode currentMode() const { return m_mode; }

Q_SIGNALS:
    void sliceProgress(int percentage, const QString& message);
    void sliceCompleted(const QString& gcodePath, bool success, const QString& errorMessage = QString());
    void importStarted(const QString& filePath);
    void importCompleted(const QString& objectId, bool success, const QString& errorMessage = QString());

protected:
    // StandardActionHandler 接口实现
    void onEnter(std::shared_ptr<ActionContext> context) override;
    void onExit() override;
    const QHash<QString, QVariantMap>& aiDescriptorTable() const override;

private:
    // 模式和状态
    Mode m_mode = Mode::Slice;
    State m_state = State::Inactive;

    // 参数
    QString m_modelId;
    QString m_modelName;
    QStringList m_pendingModelIds;
    std::vector<std::shared_ptr<ModelInstanceDB>> m_plateInstances;
    bool m_sliceAllMode{false};
    bool m_useManualSupportMeshForSlice{false};
    int m_sliceAllTotalCount{0};
    GPlatform::ToolpathPreviewSourceScope m_sliceSourceScope{
        GPlatform::ToolpathPreviewSourceScope::ImportedGCode};
    DBInstanceID m_slicePrintBedId{INVALID_DB_ID};
    DBInstanceID m_sliceSourceModelId{INVALID_DB_ID};

    // The document thread captures a lightweight immutable source lease. The
    // worker materializes its large vertex/triangle arrays into m_sliceSession.
    std::shared_ptr<const SliceSessionBuilder::CapturedRequest> m_sliceSessionCapture;
    std::shared_ptr<const GPlatform::SliceSession> m_sliceSession;
    QString m_supportMeshStlPath;
    bool m_needCleanupSupportMeshStl{false};

    // 切片结果
    QString m_gcodePath;
    bool m_gcodeIsLibraryTemporary{false};
    QString m_gcode3mfPath;
    bool m_gcode3mfIsLibraryTemporary{false};
    bool m_success{false};
    QString m_errorMessage;
    QStringList m_sliceWarnings;

    // 统计信息
    double m_totalTime{0.0};
    double m_filamentUsed{0.0};
    int m_layerCount{0};

    libslicer::ToolpathPreviewPtr m_toolpathPreview;

    // Import 模式参数
    QString m_importFilePath;

    // 当前处理的模型
    std::shared_ptr<ModelInstanceDB> m_currentModelInstance;

    // 后台任务
    std::unique_ptr<SlicingPlaybackHandler> m_playbackHandler;
    std::unique_ptr<QFutureWatcher<void>> m_taskWatcher;

    // TaskStateNotifier 任务ID
    QString m_taskId;

    // 时间追踪（用于性能分析）
    std::chrono::steady_clock::time_point m_taskStartTime;

    // 核心方法
    void performSlice();
    void onTaskCompleted();
    void cleanupSlicingEnvironment();
    void clearAllSliceData();

    // 切片辅助方法
    QStringList collectSliceTargets(const QString& actionCode) const;
    QStringList collectPlateSliceTargets(DBInstanceID printBedId) const;
    DBInstanceID defaultPrintBedId() const;
    bool resolveResliceTargets(QStringList& targets,
                               QString* errorMessage);
    bool prepareModelForSlice(const QString& modelId, QString* errorMessage = nullptr);
    bool startSliceTaskForModel(const QString& modelId, QString* errorMessage = nullptr);
    bool startSliceTaskForPlate(const QStringList& modelIds, QString* errorMessage = nullptr);
    QString getOrExportSupportMeshSTL();
    void cleanupUnownedTemporaryPrintOutputs();
    void createToolpathPreview();

public:
    Q_INVOKABLE void setDebugMode(bool enabled);
    Q_INVOKABLE void clearDebugActors();
};
