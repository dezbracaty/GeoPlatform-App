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
    bool m_shutdownPrepared = false;

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

    // After the event loop stops, release parentless JavaScript-owned router
    // windows while their engine's theme singletons are still available.
    static void releaseQmlWindows(QQmlEngine& engine);

    Q_INVOKABLE void restart() {
        auto arguments = QCoreApplication::arguments().mid(1);
        if (arguments.isEmpty()) arguments << "--restart";
        QString error;
        if (!scheduleRestart(arguments, &error))
            LOG_ERROR("Application restart rejected: {}", error.toStdString());
    }

    // Deferred restart: main launches only after the old application is destroyed.
    bool scheduleRestart(const QStringList& arguments, QString* error);
    QStringList takeRestartArguments();

    Q_INVOKABLE void restartQmlEngine() {
        restart();
    }

    Q_INVOKABLE void exit(int code = 0);
    void prepareForExit();

private:
    // 安全清理所有 Handler 的方法
    void cleanupHandlers();
};
