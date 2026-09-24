#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

/**
 * @brief 统一ID系统基类
 *
 * 为整个架构提供强类型、线程安全的ID管理
 * 兼容现有的QString系统，支持相互转换
 */
class BaseID {
public:
    using ValueType = std::int64_t;

    explicit BaseID(ValueType id = 0) : m_id(id) {
    }
    virtual ~BaseID() = default;

    // 🔧 基础访问接口
    ValueType getValue() const {
        return m_id;
    }
    std::string toString() const {
        return std::to_string(m_id);
    }
    QString toQString() const {
        return QString::number(m_id);
    }

    // 🔧 比较操作符
    bool operator==(const BaseID& other) const {
        return m_id == other.m_id;
    }
    bool operator<(const BaseID& other) const {
        return m_id < other.m_id;
    }
    bool operator!=(const BaseID& other) const {
        return m_id != other.m_id;
    }

    // 🔧 有效性检查
    bool isValid() const {
        return m_id != 0;
    }
    static BaseID invalid() {
        return BaseID(0);
    }

protected:
    ValueType m_id;
};

/**
 * @brief DB实例ID类
 *
 * 用于标识所有DB实例，使用递增策略生成
 * 从1开始递增，确保线程安全
 */
class DBInstanceID : public BaseID {
public:
    explicit DBInstanceID(ValueType id = 0) : BaseID(id) {
    }
    explicit DBInstanceID(const QString& qstr) : BaseID(qstr.toLongLong()) {
    }

    // 🔧 递增策略ID生成（线程安全）
    static DBInstanceID generate() {
        return DBInstanceID(counter().fetch_add(1));
    }

    static DBInstanceID generateTemp() {
        return DBInstanceID(tempCounter().fetch_sub(1));
    }

    bool isTemp() const noexcept {
        return getValue() < 0;
    }

    static void reserveAtLeast(const DBInstanceID& id) {
        if (!id.isValid()) {
            return;
        }

        if (id.isTemp()) {
            return;
        }

        auto& nextCounter = counter();
        ValueType expected = nextCounter.load();
        const ValueType desiredNext = id.getValue() + 1;
        while (expected < desiredNext &&
               !nextCounter.compare_exchange_weak(expected, desiredNext)) {
        }
    }

    // 🔧 类型转换便捷方法
    static DBInstanceID fromString(const std::string& str) {
        try {
            return DBInstanceID(std::stoll(str));
        } catch (...) {
            return DBInstanceID(0); // 无效ID
        }
    }

    static DBInstanceID fromQString(const QString& qstr) {
        bool ok;
        const ValueType id = qstr.toLongLong(&ok);
        return ok ? DBInstanceID(id) : DBInstanceID(0);
    }

private:
    static std::atomic<ValueType>& counter() {
        static std::atomic<ValueType> value{1};
        return value;
    }

    static std::atomic<ValueType>& tempCounter() {
        static std::atomic<ValueType> value{-1};
        return value;
    }
};

/**
 * @brief 渲染ID类
 *
 * 用于标识所有Render实例，使用递增策略生成
 * 从1000000开始递增，避免与DBInstanceID冲突
 */
class RenderID : public BaseID {
public:
    explicit RenderID(ValueType id = 0) : BaseID(id) {
    }

    // 🔧 递增策略ID生成（线程安全，避免与DBInstanceID冲突）
    static RenderID generate() {
        static std::atomic<ValueType> counter{1000000}; // 从1000000开始，避免与DBInstanceID冲突
        return RenderID(counter.fetch_add(1));
    }

    // 🔧 类型转换便捷方法
    static RenderID fromString(const std::string& str) {
        try {
            return RenderID(std::stoll(str));
        } catch (...) {
            return RenderID(0); // 无效ID
        }
    }
};

// 🔧 Hash支持，用于std::unordered_map等容器
namespace std {
    template <>
    struct hash<BaseID> {
        size_t operator()(const BaseID& id) const noexcept {
            return std::hash<BaseID::ValueType>()(id.getValue());
        }
    };

    template <>
    struct hash<DBInstanceID> {
        size_t operator()(const DBInstanceID& id) const noexcept {
            return std::hash<BaseID::ValueType>()(id.getValue());
        }
    };

    template <>
    struct hash<RenderID> {
        size_t operator()(const RenderID& id) const noexcept {
            return std::hash<BaseID::ValueType>()(id.getValue());
        }
    };
} // namespace std

// 🔧 调试输出支持
#include <QDebug>

inline QDebug operator<<(QDebug debug, const BaseID& id) {
    debug << "ID(" << id.getValue() << ")";
    return debug;
}

inline QDebug operator<<(QDebug debug, const DBInstanceID& id) {
    debug << "DBID(" << id.getValue() << ")";
    return debug;
}

inline QDebug operator<<(QDebug debug, const RenderID& id) {
    debug << "RenderID(" << id.getValue() << ")";
    return debug;
}

// 🔧 便捷宏定义
#define INVALID_DB_ID     DBInstanceID(0)
#define INVALID_RENDER_ID RenderID(0)

// 🔧 类型转换便捷函数
inline QString dbIdToQString(const DBInstanceID& id) {
    return id.toQString();
}

inline DBInstanceID qStringToDbId(const QString& qstr) {
    return DBInstanceID::fromQString(qstr);
}

// 🔧 类型别名，方便使用
using DBInstanceIDHasher = std::hash<DBInstanceID>;
using RenderIDHasher = std::hash<RenderID>;
