#pragma once

#include "BaseID.hpp"
#include "ChangeTypes.hpp"
#include <any>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class AutoRegisterDB;
class TransactionManager;

// Include Prop.hpp for complete type definition
#include "Prop.hpp"

/**
 * @brief 事务操作接口
 *
 * 定义单个可撤销操作的接口
 */
class ITransactionOperation {
public:
    virtual ~ITransactionOperation() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual std::string getDescription() const = 0;
};

/**
 * @brief 属性变化操作
 *
 * 记录和恢复对象属性的变化
 * 使用 std::any 存储值，不依赖 Qt
 */
class PropertyChangeOperation : public ITransactionOperation {
public:
    PropertyChangeOperation(AutoRegisterDB* object, const trans::Prop& prop,
                            const std::any& oldValue, const std::any& newValue);

    void undo() override;
    void redo() override;
    std::string getDescription() const override;

private:
    AutoRegisterDB* m_object;
    trans::Prop m_prop;
    std::any m_oldValue;
    std::any m_newValue;
};

/**
 * @brief 自定义操作
 *
 * 允许用户定义自己的撤销/重做逻辑
 */
class CustomOperation : public ITransactionOperation {
public:
    using UndoFunction = std::function<void()>;
    using RedoFunction = std::function<void()>;

    CustomOperation(const std::string& description, UndoFunction undoFunc, RedoFunction redoFunc);

    void undo() override;
    void redo() override;
    std::string getDescription() const override;

private:
    std::string m_description;
    UndoFunction m_undoFunction;
    RedoFunction m_redoFunction;
};

/**
 * @brief 事务类
 *
 * 管理一组相关的操作，支持原子性的撤销/重做
 */
class Transaction {
public:
    explicit Transaction(const std::string& description = "");
    ~Transaction();

    /**
     * @brief 添加操作到事务
     */
    void addOperation(std::unique_ptr<ITransactionOperation> operation);

    /**
     * @brief 添加属性变化操作
     * 使用 std::any 作为值类型，不依赖 Qt
     */
    void addPropertyChange(AutoRegisterDB* object, const trans::Prop& prop,
                           const std::any& oldValue, const std::any& newValue);

    /**
     * @brief 添加自定义操作
     */
    void addCustomOperation(const std::string& description,
                            std::function<void()> undoFunc,
                            std::function<void()> redoFunc);

    /**
     * @brief 执行撤销
     */
    void undo();

    /**
     * @brief 执行重做
     */
    void redo();

    /**
     * @brief 检查是否为空事务
     */
    bool isEmpty() const;

    /**
     * @brief 获取事务描述
     */
    const std::string& getDescription() const;

    /**
     * @brief 设置事务描述
     */
    void setDescription(const std::string& description);

    /**
     * @brief 获取操作数量
     */
    size_t getOperationCount() const;

    /**
     * @brief 获取事务ID
     */
    DBInstanceID getId() const;

    /**
     * @brief 获取创建时间戳
     */
    Timestamp getTimestamp() const;

    /**
     * @brief 合并另一个事务的操作
     */
    void merge(std::shared_ptr<Transaction> other);

    /**
     * @brief 获取调试信息
     */
    std::string getDebugInfo() const;

private:
    DBInstanceID m_id;
    std::string m_description;
    std::vector<std::unique_ptr<ITransactionOperation>> m_operations;
    Timestamp m_timestamp;

    // 友元类
    friend class TransactionManager;
};