#pragma once

#include "BridgeBase.hpp"

#include <QJSEngine>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <qqmlregistration.h>

class LocalModelLibraryBridge final : public bridge::BridgeBase {
    Q_OBJECT
    Q_PROPERTY(int totalCount READ totalCount NOTIFY modelsChanged)

    QML_ELEMENT
    QML_SINGLETON

public:
    static LocalModelLibraryBridge* instance();
    static LocalModelLibraryBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    Q_INVOKABLE QVariantList models(const QString& searchText,
                                    const QString& sortMode) const;
    int totalCount() const;

signals:
    void modelsChanged();

private:
    explicit LocalModelLibraryBridge(QObject* parent = nullptr);
};
