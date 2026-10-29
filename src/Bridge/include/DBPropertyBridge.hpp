#pragma once

#include <ObservableBridgeBase.hpp>
#include <QProperty>
#include <QBindable>
#include <QVariantList>
#include <QtQml/qqml.h>
#include <memory>

class DBPropertyBridge : public bridge::ObservableBridgeBase {
    Q_OBJECT

    // Widget 存在性属性
    Q_PROPERTY(bool gridDisplayExists READ gridDisplayExists
               BINDABLE bindableGridDisplayExists NOTIFY gridDisplayExistsChanged)
    Q_PROPERTY(int meshCount READ meshCount
               BINDABLE bindableMeshCount NOTIFY meshCountChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)

    QML_ELEMENT

signals:
    void gridDisplayExistsChanged();
    void meshCountChanged();
    void modelsChanged();

public:
    explicit DBPropertyBridge(QObject* parent = nullptr);
    ~DBPropertyBridge();

    // Getters
    bool gridDisplayExists() const { return m_gridDisplayExists; }
    int meshCount() const { return m_meshCount; }
    QVariantList models() const;

    // Bindables
    QBindable<bool> bindableGridDisplayExists() { return &m_gridDisplayExists; }
    QBindable<int> bindableMeshCount() { return &m_meshCount; }

protected:
    // Override from ObservableBridgeBase
    void handleDatabaseChange(const DocumentManager::ChangeNotification& notification) override;
    bool shouldReceiveAllNotifications() const override { return true; }

private:
    void setupBindings();

private:
    void markBindingsDirty();

private:
    // 使用 Q_OBJECT_BINDABLE_PROPERTY 声明可绑定属性
    Q_OBJECT_BINDABLE_PROPERTY(DBPropertyBridge, bool, m_gridDisplayExists,
                                &DBPropertyBridge::gridDisplayExistsChanged)
    Q_OBJECT_BINDABLE_PROPERTY(DBPropertyBridge, int, m_meshCount,
                                &DBPropertyBridge::meshCountChanged)

    // 保存绑定以供重新创建
    QPropertyBinding<bool> m_gridBinding;
    QPropertyBinding<int> m_meshCountBinding;
};
