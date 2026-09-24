#include "../include/Transaction.hpp"
#include "../include/AutoRegisterDB.hpp"
#include "Prop.hpp"
#include <algorithm>
#include <sstream>

// PropertyChangeOperation 实现
PropertyChangeOperation::PropertyChangeOperation(AutoRegisterDB* object, const trans::Prop& prop,
                                                 const std::any& oldValue, const std::any& newValue)
    : m_object(object), m_prop(prop), m_oldValue(oldValue), m_newValue(newValue) {
}

void PropertyChangeOperation::undo() {
    if (m_object) {
        // 直接使用 setPropertyImpl
        m_object->setPropertyImpl(m_prop, m_oldValue);
    }
}

void PropertyChangeOperation::redo() {
    if (m_object) {
        // 直接使用 setPropertyImpl
        m_object->setPropertyImpl(m_prop, m_newValue);
    }
}

std::string PropertyChangeOperation::getDescription() const {
    return "Property change: " + std::string(m_prop.name());
}

// CustomOperation 实现
CustomOperation::CustomOperation(const std::string& description, UndoFunction undoFunc, RedoFunction redoFunc)
    : m_description(description), m_undoFunction(undoFunc), m_redoFunction(redoFunc) {
}

void CustomOperation::undo() {
    if (m_undoFunction) {
        m_undoFunction();
    }
}

void CustomOperation::redo() {
    if (m_redoFunction) {
        m_redoFunction();
    }
}

std::string CustomOperation::getDescription() const {
    return m_description;
}

// Transaction 实现
Transaction::Transaction(const std::string& description)
    : m_id(DBInstanceID::generate()), m_description(description), m_timestamp(getCurrentTimestamp()) {
}

Transaction::~Transaction() {
}

void Transaction::addOperation(std::unique_ptr<ITransactionOperation> operation) {
    if (operation) {
        m_operations.push_back(std::move(operation));
    }
}

void Transaction::addPropertyChange(AutoRegisterDB* object, const trans::Prop& prop,
                                    const std::any& oldValue, const std::any& newValue) {
    auto operation = std::make_unique<PropertyChangeOperation>(object, prop, oldValue, newValue);
    addOperation(std::move(operation));
}

void Transaction::addCustomOperation(const std::string& description,
                                     std::function<void()> undoFunc,
                                     std::function<void()> redoFunc) {
    auto operation = std::make_unique<CustomOperation>(description, undoFunc, redoFunc);
    addOperation(std::move(operation));
}

void Transaction::undo() {
    // 反向执行撤销操作
    for (auto it = m_operations.rbegin(); it != m_operations.rend(); ++it) {
        (*it)->undo();
    }
}

void Transaction::redo() {
    // 正向执行重做操作
    for (auto& operation : m_operations) {
        operation->redo();
    }
}

bool Transaction::isEmpty() const {
    return m_operations.empty();
}

const std::string& Transaction::getDescription() const {
    return m_description;
}

void Transaction::setDescription(const std::string& description) {
    m_description = description;
}

size_t Transaction::getOperationCount() const {
    return m_operations.size();
}

DBInstanceID Transaction::getId() const {
    return m_id;
}

Timestamp Transaction::getTimestamp() const {
    return m_timestamp;
}

void Transaction::merge(std::shared_ptr<Transaction> other) {
    if (!other) {
        return;
    }

    // 移动其他事务的操作到当前事务
    for (auto& operation : other->m_operations) {
        m_operations.push_back(std::move(operation));
    }

    // 清空其他事务的操作列表
    other->m_operations.clear();

    // 更新描述
    if (!other->m_description.empty() && other->m_description != m_description) {
        m_description += " + " + other->m_description;
    }
}

std::string Transaction::getDebugInfo() const {
    std::ostringstream oss;
    oss << "Transaction [" << m_id.getValue() << "]:\n";
    oss << "  Description: " << m_description << "\n";
    oss << "  Operation Count: " << m_operations.size() << "\n";
    oss << "  Timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(m_timestamp.time_since_epoch()).count() << "ms\n";

    for (size_t i = 0; i < m_operations.size(); ++i) {
        oss << "  Operation " << i << ": " << m_operations[i]->getDescription() << "\n";
    }

    return oss.str();
}