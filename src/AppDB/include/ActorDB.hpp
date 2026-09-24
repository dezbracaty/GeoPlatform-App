#pragma once

#include <AutoRegisterDB.hpp>
#include "MaterialDB.hpp"
#include <SystemTypes.hpp>
#include <ChangeTypes.hpp>
#include <memory>
#include <shared_mutex>
#include <Transform.hpp>

// =====================================================================
// 属性宏定义（使用 T{} 默认构造）
// =====================================================================

// 通用属性宏（引用类型参数）
#define FIELD_PROPERTY(ClassName, type, name, notifyCallback) \
public:                                                  \
    static inline const trans::Prop& PROP_##name() {      \
        static const trans::Prop prop(typeid(ClassName), #name); \
        return prop;                                      \
    }                                                     \
    type get##name() const {                             \
        return trans::TransDB::getProperty<type>(PROP_##name()); \
    }                                                    \
    void set##name(const type& value) {                  \
        trans::TransDB::setProperty(PROP_##name(), value); \
        notifyCallback(PROP_##name());                    \
    }

// 通用属性宏简化版（值类型参数）
#define FIELD_PROPERTY_SIMPLE(ClassName, type, name, notifyCallback) \
public:                                                   \
    static inline const trans::Prop& PROP_##name() {      \
        static const trans::Prop prop(typeid(ClassName), #name); \
        return prop;                                      \
    }                                                     \
    type get##name() const {                              \
        return trans::TransDB::getProperty<type>(PROP_##name()); \
    }                                                     \
    void set##name(type value) {                          \
        trans::TransDB::setProperty(PROP_##name(), value); \
        notifyCallback(PROP_##name());                    \
    }

// 可见性属性宏
#define FIELD_VISIBILITY_PROPERTY(ClassName, type, name, notifyCallback) \
public:                                                       \
    static inline const trans::Prop& PROP_##name() {          \
        static const trans::Prop prop(typeid(ClassName), #name); \
        return prop;                                          \
    }                                                         \
    type get##name() const {                                  \
        return trans::TransDB::getProperty<type>(PROP_##name()); \
    }                                                         \
    bool is##name() const {                                   \
        return trans::TransDB::getProperty<type>(PROP_##name()); \
    }                                                         \
    void set##name(type value) {                              \
        trans::TransDB::setProperty(PROP_##name(), value);    \
        notifyCallback();                                     \
    }

// 几何属性宏
#define FIELD_GEOMETRY_VALUE(ClassName, type, name) \
    FIELD_PROPERTY(ClassName, type, name, markGeometryChanged)

#define FIELD_GEOMETRY_VALUE_SIMPLE(ClassName, type, name) \
    FIELD_PROPERTY_SIMPLE(ClassName, type, name, markGeometryChanged)

// 材质属性宏
#define FIELD_MATERIAL_VALUE(ClassName, type, name) \
    FIELD_PROPERTY(ClassName, type, name, notifyMaterialChange)

#define FIELD_MATERIAL_VALUE_SIMPLE(ClassName, type, name) \
    FIELD_PROPERTY_SIMPLE(ClassName, type, name, notifyMaterialChange)

#define FIELD_VISIBILITY_VALUE(ClassName, type, name) \
    FIELD_VISIBILITY_PROPERTY(ClassName, type, name, notifyVisibilityChange)

/**
 * @brief ActorDB - 所有可渲染对象的DB基类
 *
 * 对应VTK Actor的属性，但不包含VTK对象
 * 负责管理所有几何体的共同属性
 * 继承自 AutoRegisterDB（而 AutoRegisterDB 已继承自 TransDB）
 */
class ActorDB : public AutoRegisterDB {

public:
    /**
     * @brief 边界框结构体
     */
    struct BoundingBox {
        Vector3 min{0, 0, 0};
        Vector3 max{0, 0, 0};
        bool valid = false;

        Vector3 getCenter() const {
            return Vector3(
                (min.x + max.x) * 0.5f,
                (min.y + max.y) * 0.5f,
                (min.z + max.z) * 0.5f);
        }

        Vector3 getSize() const {
            return Vector3(
                max.x - min.x,
                max.y - min.y,
                max.z - min.z);
        }
    };

public:
    /**
     * @brief 构造函数
     */
    ActorDB();

    /**
     * @brief 析构函数
     */
    virtual ~ActorDB();

    // === 纯虚函数（子类必须实现） ===


    /**
     * @brief 计算原始几何数据的包围盒（不应用Transform）
     * @return 原始几何数据的包围盒，通常以模型中心为原点
     */
    virtual BoundingBox localBounds() const = 0;

    /**
     * @brief 克隆对象
     */
    std::shared_ptr<AutoRegisterDB> clone() const override = 0;

    /**
     * @brief 计算应用变换矩阵后的包围盒
     *
     * 默认实现：变换包围盒8个角点（适用于大多数几何体）
     * 子类可重写此方法使用更精确的算法：
     * - ModelInstanceDB: 遍历所有 Part 的三角形顶点（更精确）
     *
     * @param matrix 变换矩阵
     * @return 变换后的包围盒
     */
    virtual BoundingBox worldBoundsAt(const Transform::Matrix4& matrix) const;

    /** Current actor's single authoritative, exact world-space AABB. */
    virtual BoundingBox worldBounds() const;

    // === 使用优化后的通用宏定义属性 ===

    // 位置属性 - 从 Transform 获取/设置
public:
    Vector3 getPosition() const {
        return getTransform().getPosition();
    }
    void setPosition(const Vector3& position);

    // 缩放属性 - 从 Transform 矩阵中提取和设置
    Vector3 getScale() const;           // 从 Transform 矩阵提取缩放
    void setScale(const Vector3& scale); // 设置 Transform 矩阵中的缩放

    // 旋转属性 - 欧拉角只读，所有修改通过 Transform
public:
    Vector3 getRotation() const;  // 只读接口，从 Transform 提取欧拉角

    // 基于矩阵的旋转操作接口
    void resetRotation();  // 重置为单位矩阵
    void setRotationFromMatrix(const Transform::Matrix4& matrix);  // 从矩阵设置旋转
    void setRotationFromQuaternion(float w, float x, float y, float z);  // 从四元数设置旋转

    // 增量旋转接口（类似 VTK）
    void rotate(float angle, const Vector3& axis);  // 世界坐标系旋转
    void rotateWXYZ(float angle, const Vector3& worldAxis);  // 显式的世界坐标系旋转
    void rotateLocal(float angle, const Vector3& localAxis);  // 局部坐标系旋转
    void rotateX(float angle);
    void rotateY(float angle);
    void rotateZ(float angle);

    // 获取完整的变换矩阵
    Transform::Matrix4 getTransformMatrix() const;

    // 从另一个 Actor 复制变换
    void copyTransformFrom(const ActorDB& other);

    static inline const trans::Prop& PROP_Transform() {
        static const trans::Prop prop(typeid(ActorDB), "Transform");
        return prop;
    }
    Transform getTransform() const {
        return trans::TransDB::getProperty<Transform>(PROP_Transform());
    }
    void setTransform(const Transform& value);

    // 缩放便捷方法
    void setScaleX(float x);
    void setScaleY(float y);
    void setScaleZ(float z);
    void setScaleUniform(float scale);  // 统一缩放

    // 可见性属性
    FIELD_VISIBILITY_PROPERTY(ActorDB, bool, Visible, notifyVisibilityChange)

    // 交互属性
    FIELD_PROPERTY_SIMPLE(ActorDB, bool, Pickable, notifyNoAction)
    FIELD_PROPERTY_SIMPLE(ActorDB, bool, Dragable, notifyNoAction)

    int getHoveredPart() const {
        auto lock = getSharedLock();
        return m_hoveredPart;
    }

    void setHoveredPart(int partId) {
        {
            auto lock = getUniqueLock();
            if (m_hoveredPart == partId) {
                return;
            }
            m_hoveredPart = partId;
        }
        notifyHoveredPartChange();
    }

    // Opacity属性
    FIELD_PROPERTY_SIMPLE(ActorDB, float, Opacity, notifyNoAction)

public:
    inline static constexpr const char* kMaterialRelation = "actor.material";

    /** Resolve the Actor's owned MaterialDB through the relation graph. */
    std::shared_ptr<MaterialDB> getMaterial() const;

    /** Unregister this Actor's owned MaterialDB. */
    bool removeMaterial();

public:
    // 提供布尔属性的自然访问方式
    bool isPickable() const {
        return getPickable();
    }
    bool isDragable() const {
        return getDragable();
    }

    // === 实现 AutoRegisterDB 的钩子函数 ===

    /**
     * @brief 初始化属性 - final，子类不能覆盖
     *
     * 通过调用内部方法和子类钩子来控制初始化流程
     */
    void initializeProperties() override final;

    /**
     * @brief 属性初始化完成后 - final，子类不能覆盖
     */
    void afterPropertiesInitialized() override final;

    void onFullyInitialized() override;

    // === 给子类的钩子函数 ===

    /**
     * @brief 初始化子Actor的特有属性
     *
     * 子类（如 SphereDB, CubeDB）应该覆盖这个方法
     * 来初始化自己的特有属性
     */
    virtual void initializeSubActorProperties() {
    }

    /**
     * @brief 子Actor属性初始化完成后的钩子
     */
    virtual void afterSubActorPropertiesInitialized() {
    }

    /** ModelInstanceDB renders Part materials and therefore returns false. */
    virtual bool ownsDefaultMaterial() const { return true; }

    /**
     * Called synchronously after the authoritative Transform property is
     * replaced and before Transform/WorldBounds notifications are published.
     * Derived runtime spatial data must be updated here; query paths must not
     * repair stale transform state lazily.
     */
    virtual void onTransformChanged(const Transform& transform) {
        (void)transform;
    }

private:
    /**
     * @brief 初始化 ActorDB 的通用属性
     *
     * 私有方法，由 initializeProperties 调用
     */
    void initializeActorProperties();

protected:
    // === 覆写TransDB虚函数以支持嵌套属性（如 Material.XXX）===
    std::any getPropertyImpl(const trans::Prop& prop) const override;
    void setPropertyImpl(const trans::Prop& prop, const std::any& value) override;
    std::vector<std::string_view> getPropertyNames() const override;
    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

protected:
    /**
     * @brief 通知属性变化
     */
    void notifyPropertyChange(const std::string& propertyName);

    /**
     * @brief 空的通知函数 - 用于不需要特殊处理的属性
     */
    void notifyNoAction(const trans::Prop& prop) { /* 空实现 */
    }

    /**
     * @brief 通知几何体变化
     * @param propertyName 变化的属性名称（可选）
     */
    void notifyGeometryChange(const std::string& propertyName = "");

    /**
     * @brief 通知几何体变化（Prop 版本）
     * @param prop 变化的属性
     */
    void notifyGeometryChange(const trans::Prop& prop);

    /**
     * @brief 通知材质变化
     */
    void notifyMaterialChange(const std::string& propertyName);

    /**
     * @brief 通知材质变化（Prop 版本）
     * @param prop 变化的属性
     */
    void notifyMaterialChange(const trans::Prop& prop);

    /**
     * @brief 通知变换变化
     */
    /**
     * @brief 通知可见性变化
     */
    void notifyVisibilityChange();
    void notifyHoveredPartChange();

    /**
     * @brief 使用当前变换矩阵变换一个点
     * @param point 要变换的点（局部坐标）
     * @return 变换后的点（世界坐标）
     */
    Vector3 transformPoint(const Vector3& point) const;

private:
    void initializeMaterial();
    void invalidateWorldBounds();

    // === 线程安全（私有，统一管理）===
    mutable std::shared_mutex m_mutex;

protected:
    // === 线程安全的属性访问方法（供子类使用）===

    /**
     * @brief 线程安全地获取属性值（不持有锁）
     * 供子类在实现 localBounds() 时使用
     */
    template<typename T>
    T getPropertySafe(const trans::Prop& prop) const {
        return trans::TransDB::getProperty<T>(prop);
    }

    /**
     * @brief 线程安全地获取属性值（字符串版本，用于兼容）
     */
    template<typename T>
    T getPropertySafe(const std::string& name) const {
        trans::Prop prop(typeid(*this), name.c_str());
        return trans::TransDB::getProperty<T>(prop);
    }

    /**
     * @brief 线程安全地设置属性值（不持有锁）
     */
    template<typename T>
    void setPropertySafe(const trans::Prop& prop, const T& value) {
        trans::TransDB::setProperty<T>(prop, value);
    }

    /**
     * @brief 线程安全地设置属性值（字符串版本，用于兼容）
     */
    template<typename T>
    void setPropertySafe(const std::string& name, const T& value) {
        trans::Prop prop(typeid(*this), name.c_str());
        trans::TransDB::setProperty<T>(prop, value);
    }

    /**
     * @brief 获取共享锁（供子类在 clone() 等方法中使用）
     */
    std::shared_lock<std::shared_mutex> getSharedLock() const {
        return std::shared_lock<std::shared_mutex>(m_mutex);
    }

    /**
     * @brief 获取独占锁（供子类在需要写入操作时使用）
     */
    std::unique_lock<std::shared_mutex> getUniqueLock() const {
        return std::unique_lock<std::shared_mutex>(m_mutex);
    }

    // === 工具方法：供子类使用 ===

    /**
     * @brief 工具方法：变换单个点
     *
     * @param point 原始点（局部坐标）
     * @param matrix 变换矩阵
     * @return 变换后的点（世界坐标）
     */
    Vector3 transformPoint(const Vector3& point, const Transform::Matrix4& matrix) const;

private:
    // Derived state. Callers can only access it through worldBounds().
    mutable BoundingBox m_worldBounds;
    mutable std::uint64_t m_worldBoundsRevision = 0;
    std::uint64_t m_spatialRevision = 1;
    int m_hoveredPart = -1;

    // Transform 现在作为属性存储，不再是成员变量
    // 通过 getTransform()/setTransform() 访问
};

// Qt元对象系统支持
Q_DECLARE_METATYPE(ActorDB::BoundingBox)
