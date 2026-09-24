#pragma once

#include <QObject>
#include <QProcess>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include "Foundation/Log.h"
#include <QtQml/qqml.h>

// Forward declaration
class ActionManager;

class ApplicationHelper : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

private:
    explicit ApplicationHelper(QObject* parent = nullptr) : QObject(parent) {
    }
    static ApplicationHelper* m_instance;
    QQmlApplicationEngine* m_engine = nullptr;
    QStringList m_restartArguments;

public:
    static ApplicationHelper* getInstance() {
        if (!m_instance) {
            m_instance = new ApplicationHelper();
        }
        return m_instance;
    }

    static ApplicationHelper* create(QQmlEngine*, QJSEngine*) {
        return getInstance();
    }

    void setEngine(QQmlApplicationEngine* engine) {
        m_engine = engine;
    }

    Q_INVOKABLE void restart() {
        // Start a new instance
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QStringList() << "--restart");
        // Exit current instance
        QCoreApplication::exit(0);
    }

    // Deferred restart: main launches only after the old application is destroyed.
    bool scheduleRestart(const QStringList& arguments, QString* error);
    QStringList takeRestartArguments();

    Q_INVOKABLE void restartQmlEngine() {
        if (m_engine) {
            LOG_DEBUG("ApplicationHelper: Starting QML engine restart...");

            // Clear component cache to force reload
            m_engine->clearComponentCache();
            LOG_DEBUG("ApplicationHelper: Component cache cleared");

            // Trigger garbage collection
            m_engine->collectGarbage();

            // Emit a signal to notify that we need to reload
            // For now, just log that restart was requested
            LOG_DEBUG("ApplicationHelper: QML engine cache cleared and garbage collected");
            LOG_DEBUG("ApplicationHelper: Note: Full QML restart requires application restart");

            // Alternative: Just restart the whole application
            restart();
        } else {
            LOG_DEBUG("ApplicationHelper: No engine set, cannot restart");
        }
    }

    Q_INVOKABLE void exit(int code = 0);

private:
    // 安全清理所有 Handler 的方法
    void cleanupHandlers();
};
