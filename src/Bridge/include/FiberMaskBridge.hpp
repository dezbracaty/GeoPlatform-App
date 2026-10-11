#pragma once
#include "BridgeBase.hpp"
#include <QJSEngine>
#include <QVariantList>
#include <QVariantMap>
#include <DocumentManager.hpp>

class FiberMaskBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList modelGroups READ modelGroups NOTIFY modelGroupsChanged)
    QML_ELEMENT
    QML_SINGLETON
public:
    static FiberMaskBridge* instance();
    static FiberMaskBridge* create(QQmlEngine*, QJSEngine*);
    ~FiberMaskBridge() override;
    bool active() const {
        return m_state.value("active").toBool();
    }
    QVariantMap state() const {
        return m_state;
    }
    QVariantList parameters() const {
        return m_parameters;
    }
    Q_INVOKABLE bool enterSelectedModel();
    Q_INVOKABLE bool selectMask(qulonglong id);
    Q_INVOKABLE bool finish();
    Q_INVOKABLE void setMaskVisible(qulonglong id, bool visible);
    QVariantList modelGroups() const { return m_modelGroups; }
    void refreshModelGroups();
    Q_INVOKABLE void request(const QString& operation, const QVariantMap& values = {});
    Q_INVOKABLE void cancel() {
        request(QStringLiteral("cancel"));
    }
    void project(QVariantMap state, QVariantList parameters = {});
    void flushInputs() { emit commitInputsRequested(); }
signals:
    void commitInputsRequested();
    void changed();
    void parametersChanged();
    void modelGroupsChanged();
    void requested(const QString& operation, const QVariantMap& values);

private:
    explicit FiberMaskBridge(QObject* parent = nullptr);
    QVariantMap m_state;
    QVariantList m_parameters;
    QVariantList m_modelGroups;
    DocumentManager::ListenerID m_documentListener{};
    bool m_groupsRefreshPending{false};
};
