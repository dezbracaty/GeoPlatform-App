#pragma once

#include "transdb.h"
#include "BaseID.hpp"
#include "ChangeTypes.hpp"
#include "SystemTypes.hpp"
#include "SnapGeometry.hpp"
#include <memory>
#include <string>
#include <vector>

#define FIELD_RELATION_REF(ClassName, name) FIELD_VALUE_SIMPLE(ClassName, DBInstanceID, name)

// Forward declarations
class DocumentManager;
class TransactionManager;
class DocumentLoadGuard;
class TempDBScope;

/** A persisted DB-to-DB reference reported by the object that stores it. */
struct DBRelationRef {
    std::string relationName;
    DBInstanceID targetId;
};

/** A relation target translated by the document copy operation. */
struct DBRelationReplacement {
    std::string relationName;
    DBInstanceID oldTargetId;
    DBInstanceID newTargetId;
};

/**
 * @brief 自动注册DB基类
 *
 * 所有数据库对象的基类，继承自 TransDB，提供：
 * - 自动注册到DocumentManager
 * - 事务系统集成
 * - 变化通知机制
 * - 属性管理
 * - 模板化类型安全的属性访问
 */
class AutoRegisterDB : public trans::TransDB {
public:
    /**
     * @brief 二阶段初始化，在 shared_ptr 创建后调用
     *
     * @warning 重要：子类重写时必须在设置完所有属性后，最后调用父类的onCreated()
     *
     * 原因：此方法会触发注册到DocumentManager，DocumentManager会立即
     * 发送CREATED通知给所有监听器（如DBRenderAdapter）。如果在调用父类
     * onCreated()之前没有设置好属性，监听器将读取到错误的默认值。
     *
     * @code
     * void MyDB::onCreated() {
     *     // 先设置所有需要的属性
     *     setViewport(Vector4(0.0f, 0.0f, 0.15f, 0.15f));
     *     setName("MyWidget");
     *
     *     // 最后调用父类，触发注册和通知
     *     AutoRegisterDB::onCreated();
     * }
     * @endcode
     */
    void onCreated() override final;

protected:
    /**
     * @brief 受保护的构造函数
     */
    explicit AutoRegisterDB();

    /**
     * @brief 子类可以覆盖此方法来控制是否自动注册
     */
    virtual bool shouldAutoRegister() const {
        return true;
    }

    // === 钩子函数 - 子类可以覆盖这些方法 ===

    /**
     * @brief 初始化属性的钩子函数
     *
     * 子类应该在这里初始化所有属性
     */
    virtual void initializeProperties() {
    }

    /**
     * @brief 属性初始化完成后的钩子函数
     *
     * 可以在这里进行依赖于属性的额外初始化
     */
    virtual void afterPropertiesInitialized() {
    }

    /**
     * @brief 所有初始化完成后的钩子函数
     *
     * 在注册到 DocumentManager 之后调用
     */
    virtual void onFullyInitialized() {
    }

public:
    /**
     * @brief 虚析构函数
     */
    virtual ~AutoRegisterDB();

    // A read-only snapshot supplied by this DB. Reading never builds geometry.
    // The default is empty; DBs opt in by publishing their own points/lines.
    virtual SnapGeometryPtr snapGeometry() const;

protected:
    // Publish only after the corresponding DB data is ready, including on
    // load and undo/redo. This derived snapshot is not a transaction property.
    // Custom features that cannot be reconstructed must be persisted by the DB.
    // Invalid data leaves the previous snapshot intact. Empty data clears it.
    bool publishSnapGeometry(SnapGeometry geometry);

private:
    SnapGeometryPtr m_snapGeometry;

public:

    /**
     * Clone this DB node only. Implementations copy payload but leave relation
     * ID fields invalid; relations are restored after every node has an ID.
     */
    virtual std::shared_ptr<AutoRegisterDB> clone() const {
        return nullptr;
    }

    /** Report persisted references stored by this DB node. */
    virtual std::vector<DBRelationRef> reportRelations() const {
        return {};
    }

    /** Apply already translated relation targets to this cloned DB node. */
    virtual bool replaceRelations(
        const std::vector<DBRelationReplacement>& replacements) {
        return replacements.empty();
    }


    /**
     * @brief 创建DB对象的统一工厂方法
     *
     * 这个方法会：
     * 1. 创建对象的 shared_ptr
     * 2. 调用 onCreated() 让子类初始化属性值
     * 3. 调用 registerToDocument() 注册到文档管理器
     *
     * @tparam T DB对象类型
     * @tparam Args 构造函数参数类型
     * @param args 构造函数参数
     * @return 创建的对象的 shared_ptr
     */
    template <typename T, typename... Args>
    static std::shared_ptr<T> create(Args&&... args) {
        static_assert(std::is_base_of<AutoRegisterDB, T>::value,
                      "T must inherit from AutoRegisterDB");

        // 使用 TransDB 的 create 方法，它会调用 onCreated
        auto obj = trans::TransDB::create<T>(std::forward<Args>(args)...);

        // onCreated 已经在 TransDB::create 中被调用了
        // 其中会调用 autoRegisterToDocumentManager

        return obj;
    }

    /**
     * @brief 获取DB实例ID
     */
    const DBInstanceID& getDBInstanceID() const;

    /** Instance lifetime is defined exclusively by the sign of this DB's ID. */
    bool isTempDB() const noexcept {
        return m_dbInstanceId.isTemp();
    }

    /**
     * @brief 获取DB类型ID
     */
    virtual TypeID getTypeID() const = 0;

    /**
     * @brief 检查是否需要同步到 VTK 渲染系统
     *
     * 默认返回 true，表示大多数 DB 对象需要同步到 VTK。
     * 子类可以重写此方法来跳过不需要同步的类型（如 WindowDB）。
     *
     * @return true 如果需要同步到 VTK，false 如果不需要
     */
    virtual bool needsVTKSync() const {
        return true;
    }

    /**
     * @brief 获取对象显示名称
     */
    virtual std::string getDisplayName() const;

    /**
     * @brief 设置对象显示名称
     */
    void setDisplayName(const std::string& name);

    /**
     * @brief 检查对象是否有效
     */
    virtual bool isValid() const;

    /**
     * @brief 获取所有属性名列表
     */
    virtual std::vector<std::string_view> getPropertyNames() const;

    /**
     * @brief 序列化对象到属性映射
     */
    virtual PropertyMap serialize() const;

    /**
     * @brief 从属性映射反序列化对象
     */
    virtual bool deserialize(const PropertyMap& properties);

    static bool isDocumentLoadActive();

    /**
     * @brief 重写TransDB的setPropertyImpl以自动处理通知
     * 这样FIELD_VALUE宏生成的setter会自动触发通知
     */
    void setPropertyImpl(const trans::Prop& prop, const std::any& value) override;

protected:
    /**
     * @brief 通知属性变化
     * @param prop 属性标识符
     */
    void notifyPropertyChanged(const trans::Prop& prop);

    /**
     * @brief 通知对象变化
     * @param changeType 变化类型
     * @param fieldName 字段名（可选）
     */
    void notifyChange(ChangeType changeType, const std::string& fieldName = "");

    /**
     * @brief 在事务中标记变化
     * @param field 字段名
     * @param changeType 变化类型
     */
    void transWithChange(const std::string& field, ChangeType changeType);

private:
    friend class DocumentLoadGuard;
    friend class TempDBScope;

    /** Create a temporary graph for the owning TempDBScope. */
    template <typename T, typename... Args>
    static std::shared_ptr<T> createTempTracked(
        std::vector<DBInstanceID>& createdIds,
        Args&&... args) {
        static_assert(std::is_base_of<AutoRegisterDB, T>::value,
                      "T must inherit from AutoRegisterDB");
        TempCreationGuard guard(&createdIds);
        return trans::TransDB::create<T>(std::forward<Args>(args)...);
    }

    class TempCreationGuard {
    public:
        explicit TempCreationGuard(std::vector<DBInstanceID>* createdIds);
        ~TempCreationGuard();
        TempCreationGuard(const TempCreationGuard&) = delete;
        TempCreationGuard& operator=(const TempCreationGuard&) = delete;
    };

    static void pushDocumentLoad();
    static void popDocumentLoad();
    static void pushTempCreation(std::vector<DBInstanceID>* createdIds);
    static void popTempCreation();
    static bool isTempCreationActive();
    static void recordTempCreation(const DBInstanceID& id);

    /**
     * @brief 注册到DocumentManager（在 onCreated 中调用）
     */
    void autoRegisterToDocumentManager();

    /**
     * @brief 从DocumentManager注销
     */
    void unregisterFromDocumentManager();

private:
    DBInstanceID m_dbInstanceId; // DB实例ID
    std::string m_displayName;   // 显示名称

    // DocumentManager 和 TransactionManager 都是单例，直接使用 instance() 访问
};

class DocumentLoadGuard {
public:
    DocumentLoadGuard();
    ~DocumentLoadGuard();

    DocumentLoadGuard(const DocumentLoadGuard&) = delete;
    DocumentLoadGuard& operator=(const DocumentLoadGuard&) = delete;
};
