#pragma once

#include <QObject>
#include <QString>
#include <memory>

class TransactionManager;

/**
 * @brief UndoRedoManager - QML桥接类，提供撤销/重做功能
 *
 * 将TransactionManager的功能暴露给QML界面
 */
class UndoRedoManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
    Q_PROPERTY(QString undoDescription READ undoDescription NOTIFY undoDescriptionChanged)
    Q_PROPERTY(QString redoDescription READ redoDescription NOTIFY redoDescriptionChanged)

    friend class DocumentManager; // Allow DocumentManager to create/destroy

public:
    /**
     * @brief 获取单例实例
     */
    static UndoRedoManager* instance();

    /**
     * @brief 检查是否可以撤销
     */
    bool canUndo() const;

    /**
     * @brief 检查是否可以重做
     */
    bool canRedo() const;

    /**
     * @brief 获取下一个撤销操作的描述
     */
    QString undoDescription() const;

    /**
     * @brief 获取下一个重做操作的描述
     */
    QString redoDescription() const;

public Q_SLOTS:
    /**
     * @brief 执行撤销操作
     */
    void undo();

    /**
     * @brief 执行重做操作
     */
    void redo();

    /**
     * @brief 清空撤销/重做栈
     */
    void clear();

    /**
     * @brief 测试连接 - 用于调试
     */
    void testConnection();

Q_SIGNALS:
    /**
     * @brief 可撤销状态变化信号
     */
    void canUndoChanged(bool canUndo);

    /**
     * @brief 可重做状态变化信号
     */
    void canRedoChanged(bool canRedo);

    /**
     * @brief 撤销描述变化信号
     */
    void undoDescriptionChanged(const QString& description);

    /**
     * @brief 重做描述变化信号
     */
    void redoDescriptionChanged(const QString& description);

    /**
     * @brief 撤销执行后的信号
     */
    void undoExecuted(const QString& description);

    /**
     * @brief 重做执行后的信号
     */
    void redoExecuted(const QString& description);

protected:
    /**
     * @brief 私有构造函数（单例模式）
     */
    explicit UndoRedoManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    virtual ~UndoRedoManager();

private:
    /**
     * @brief 初始化连接
     */
    void setupConnections();

    /**
     * @brief 更新状态
     */
    void updateState();

private:
    static UndoRedoManager* s_instance;
    // TransactionManager is now accessed via singleton instance()

    // 缓存状态
    bool m_canUndo = false;
    bool m_canRedo = false;
    QString m_undoDescription;
    QString m_redoDescription;
};