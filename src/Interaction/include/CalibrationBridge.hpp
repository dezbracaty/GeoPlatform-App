#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QFutureWatcher>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <cstdint>
#include <optional>
#include <libslicer/Calibration.hpp>

// Calibration owns a native Plater task and publishes document previews. It
// belongs in Interaction (which already depends on Bridge), like the existing
// QuickActionController; the configuration bridges do not depend on Interaction.
class CalibrationBridge final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY changed)
    Q_PROPERTY(bool configurationValid READ configurationValid NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString details READ details NOTIFY changed)
    Q_PROPERTY(float progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantList plates READ plates NOTIFY changed)
    Q_PROPERTY(int selectedPlate READ selectedPlate NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY changed)
    Q_PROPERTY(QVariantMap resultContext READ resultContext NOTIFY changed)
    Q_PROPERTY(QString recordPath READ recordPath NOTIFY changed)
public:
    static CalibrationBridge* instance();
    static CalibrationBridge* create(QQmlEngine*, QJSEngine*);
    ~CalibrationBridge() override;
    bool available() const { return m_available; }
    bool busy() const { return m_running; }
    bool active() const { return bool(m_session); }
    bool previewActive() const;
    bool configurationValid() const;
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    QString details() const { return m_details; }
    float progress() const { return m_progress; }
    QVariantList plates() const { return m_plates; }
    int selectedPlate() const { return m_selectedPlate; }
    QVariantMap parameters() const { return m_parameters; }
    QVariantMap resultContext() const;
    QString recordPath() const { return m_recordPath; }

    Q_INVOKABLE QVariantMap currentContext() const;
    Q_INVOKABLE QVariantMap parameterForm(int mode, const QVariantMap& values = {});
    Q_INVOKABLE QVariantMap updateParameter(const QString& key, const QVariant& value, bool focusOut = false);
    Q_INVOKABLE QVariantMap validateParameterForm() const;
    Q_INVOKABLE void openParameters(int mode);
    Q_INVOKABLE bool prepare(const QVariantMap& parameters);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE bool selectPlate(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool acceptResult(const QVariantMap& observation);
    void shutdown();
signals:
    void changed();
    void parametersRequested(int mode);
    void completed(bool success);
    void resultAccepted(bool applied, const QString& path);
private:
    explicit CalibrationBridge(QObject* parent = nullptr);
    struct Session;
    std::shared_ptr<Session> m_session;
    QFutureWatcher<void> m_watcher;
    bool m_available = false, m_running = false, m_closing = false;
    QString m_state = QStringLiteral("idle"), m_message, m_error, m_details, m_taskId, m_recordPath;
    std::uint64_t m_generation = 0;
    float m_progress = -1.0f;
    int m_selectedPlate = -1;
    QVariantList m_plates;
    QVariantMap m_parameters;
    std::optional<libslicer::CalibrationParameters> m_form;
    void startWork(bool prepare, int plate = -1);
    void finishWork();
    void invalidate();
    void refreshPlates();
};
