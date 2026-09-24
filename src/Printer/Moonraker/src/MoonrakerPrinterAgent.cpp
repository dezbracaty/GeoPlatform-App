#include "MoonrakerPrinterAgentImpl.hpp"
#include "Moonraker/MoonrakerPrinterAgent.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace GPlatform::Printer {
using namespace PrinterCore;

MoonrakerPrinterAgentImpl::MoonrakerPrinterAgentImpl(QUrl endpoint, QByteArray apiKey, int timeoutMs, int pollMs, int uploadTimeoutMs)
    : m_endpoint(std::move(endpoint)), m_apiKey(std::move(apiKey)), m_timeoutMs(timeoutMs),
      m_uploadTimeoutMs(uploadTimeoutMs) {
    m_poll.setInterval(pollMs);
    connect(&m_poll, &QTimer::timeout, this, [this] { queryStatus(); });
}
MoonrakerPrinterAgentImpl::~MoonrakerPrinterAgentImpl() { m_sink = {}; shutdown(); }

bool MoonrakerPrinterAgentImpl::validEndpoint(const QUrl& url) {
    return url.isValid() && (url.scheme() == "http" || url.scheme() == "https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasQuery() && !url.hasFragment();
}
void MoonrakerPrinterAgentImpl::publish(Event event) {
    event.deviceId = m_device.deviceId;
    event.sessionId = m_device.sessionId;
    const auto generation = m_generation;
    QMetaObject::invokeMethod(this, [this, generation, event = std::move(event)] {
        if (generation != m_generation) return;
        const auto sink = m_sink;
        if (sink) sink(event); // No access to this after an external callback.
    }, Qt::QueuedConnection);
}
void MoonrakerPrinterAgentImpl::initialize() {
    Event event;
    event.type = EventType::Availability;
    event.available = validEndpoint(m_endpoint) && !m_apiKey.contains('\r') && !m_apiKey.contains('\n');
    event.version = "Moonraker HTTP";
    if (!event.available) event.error = "Invalid Moonraker endpoint or API key";
    publish(event);
}
void MoonrakerPrinterAgentImpl::shutdown() {
    releaseDevice();
    m_apiKey.fill('\0');
    m_apiKey.clear();
}
void MoonrakerPrinterAgentImpl::releaseDevice() {
    ++m_generation;
    m_poll.stop();
    m_connected = m_queryPending = m_busy = m_uncertain = m_fileManager = false;
    m_objects.clear();
    m_state.clear();
    const auto replies = m_replies;
    for (auto* reply : replies) reply->abort();
    m_activeRequestId = 0;
    m_device = {};
    m_caps = {false, false, false, false, false, false, false, false, false};
}
void MoonrakerPrinterAgentImpl::scan() {
    Event event;
    event.type = EventType::ScanFinished;
    event.success = validEndpoint(m_endpoint);
    if (event.success) {
        Device device;
        device.deviceId = "moonraker:" + m_endpoint.toString(QUrl::FullyEncoded).toStdString();
        device.name = "Moonraker";
        device.model = "Klipper";
        device.ip = m_endpoint.toString().toStdString();
        device.route = DeviceRoute::LocalNetwork;
        event.devices.push_back(device);
    }
    publish(event);
}
void MoonrakerPrinterAgentImpl::login(const std::string&, const std::string&) {
    Event event; event.type = EventType::LoginFinished;
    event.error = "Moonraker uses the configured API key, not a cloud account";
    publish(event);
}
void MoonrakerPrinterAgentImpl::logout() { releaseDevice(); Event event; event.type = EventType::LogoutFinished; publish(event); }
void MoonrakerPrinterAgentImpl::selectDevice(const Device& device) {
    releaseDevice();
    m_device = device;
    if (!validEndpoint(m_endpoint) || m_apiKey.contains('\r') || m_apiKey.contains('\n')) {
        connectionFailed("Invalid Moonraker endpoint or API key");
        return;
    }
    handshake();
}
void MoonrakerPrinterAgentImpl::provideDeviceAccess(const std::string& credential) {
    const auto device = m_device;
    m_apiKey = QByteArray::fromStdString(credential);
    selectDevice(device);
}
QNetworkRequest MoonrakerPrinterAgentImpl::request(const QString& path) const {
    QUrl url = m_endpoint;
    QString prefix = url.path();
    while (prefix.endsWith('/')) prefix.chop(1);
    url.setPath(prefix + path);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setRawHeader("Accept", "application/json");
    if (!m_apiKey.isEmpty()) req.setRawHeader("X-Api-Key", m_apiKey);
    return req;
}
void MoonrakerPrinterAgentImpl::json(const QString& path, const QJsonObject& body,
                                bool post, bool mutating, Completion done) {
    auto req = request(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = post ? m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact))
                       : m_network.get(req);
    watch(reply, mutating, std::move(done));
}
void MoonrakerPrinterAgentImpl::watch(QNetworkReply* reply, bool mutating, Completion done, int timeoutMs, bool allowBareUpload) {
    const auto generation = m_generation;
    m_replies.insert(reply);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->start(timeoutMs > 0 ? timeoutMs : m_timeoutMs);
    connect(deadline, &QTimer::timeout, reply, [reply] { reply->abort(); });
    connect(reply, &QIODevice::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, mutating, allowBareUpload, done = std::move(done)] {
        m_replies.remove(reply);
        reply->deleteLater();
        if (generation != m_generation) return;
        Response response;
        response.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parse);
        const auto envelope = document.object();
        // FileUploadHandler is a real exception to Moonraker's usual HTTP
        // envelope: application.py returns the upload result directly with 201.
        // Only uploads permit that shape. Queries and controls still require result.
        const bool wrapped = envelope.contains("result");
        const bool bareUpload = allowBareUpload && response.code == 201
            && !wrapped && envelope.value("item").isObject();
        response.ok = reply->error() == QNetworkReply::NoError && response.code >= 200
            && response.code < 300 && parse.error == QJsonParseError::NoError
            && document.isObject() && (wrapped || bareUpload) && !envelope.contains("error");
        response.result = wrapped ? envelope.value("result") : QJsonValue(envelope);
        // Only an explicit client rejection is safely known not to have executed.
        // 5xx, redirects, lost replies, and malformed success replies are ambiguous.
        response.unknown = mutating && !response.ok
            && !(response.code == 401 || response.code == 403
                 || response.code == 404 || response.code == 405);
        if (!response.ok) response.error = response.code == 401 || response.code == 403
            ? QStringLiteral("Moonraker authorization failed. Check the API key.")
            : QStringLiteral("Moonraker HTTP request failed (%1). Check the printer before retrying.").arg(response.code);
        done(response);
    });
}
void MoonrakerPrinterAgentImpl::connectionFailed(const QString& error, bool access) {
    ++m_generation;
    m_connected = false;
    m_uncertain |= m_busy;
    m_busy = m_queryPending = false;
    m_activeRequestId = 0;
    m_poll.stop();
    const auto replies = m_replies;
    for (auto* reply : replies) reply->abort();
    updateCapabilities();
    Event event; event.type = EventType::ConnectionChanged;
    event.error = error.toStdString(); event.accessRequired = access;
    publish(event);
}
void MoonrakerPrinterAgentImpl::handshake() {
    json("/server/info", {}, false, false, [this](const Response& response) {
        const auto info = response.result.toObject();
        if (!response.ok || !info.value("klippy_connected").toBool()
            || info.value("klippy_state").toString() != "ready") {
            connectionFailed(response.ok ? "Klipper is not ready" : response.error,
                             response.code == 401 || response.code == 403);
            return;
        }
        m_fileManager = info.value("components").toArray().contains("file_manager")
            && info.value("registered_directories").toArray().contains("gcodes");
        json("/printer/info", {}, false, false, [this](const Response& response) {
            if (!response.ok || response.result.toObject().value("state").toString() != "ready") {
                connectionFailed(response.ok ? "Klipper is not ready" : response.error); return;
            }
            json("/printer/objects/list", {}, false, false, [this](const Response& response) {
                const auto objects = response.result.toObject().value("objects");
                if (!response.ok || !objects.isArray()) {
                    connectionFailed(response.ok ? "Invalid printer object list" : response.error); return;
                }
                for (const auto& value : objects.toArray()) m_objects.insert(value.toString());
                if (!m_objects.contains("webhooks")) {
                    connectionFailed("Klipper readiness status is unavailable"); return;
                }
                queryStatus(true);
            });
        });
    });
}
void MoonrakerPrinterAgentImpl::updateCapabilities() {
    m_caps = {false, false, m_connected && m_fileManager,
        m_connected && m_fileManager && m_objects.contains("virtual_sdcard") && m_objects.contains("print_stats"),
        m_connected && m_objects.contains("pause_resume") && m_objects.contains("print_stats"),
        m_connected && m_objects.contains("pause_resume") && m_objects.contains("print_stats"),
        false, false, false};
    Event event; event.type = EventType::CapabilitiesChanged; publish(event);
}
std::vector<std::string> MoonrakerPrinterAgentImpl::supportedPrintFormats() const {
    return m_caps.upload ? std::vector<std::string>{"gcode"} : std::vector<std::string>{};
}
void MoonrakerPrinterAgentImpl::queryStatus(bool initial) {
    if (m_queryPending || (!initial && !m_connected)) return;
    m_queryPending = true;
    QJsonObject objects;
    for (const auto* name : {"webhooks", "print_stats", "virtual_sdcard", "pause_resume", "extruder", "heater_bed"})
        if (m_objects.contains(QString::fromLatin1(name))) objects.insert(QString::fromLatin1(name), QJsonValue::Null);
    json("/printer/objects/query", {{"objects", objects}}, true, false, [this, initial](const Response& response) {
        m_queryPending = false;
        const auto status = response.result.toObject().value("status").toObject();
        if (!response.ok || status.value("webhooks").toObject().value("state").toString() != "ready") {
            connectionFailed(response.ok ? "Klipper is not ready or returned invalid status" : response.error); return;
        }
        const auto stats = status.value("print_stats").toObject();
        const QString state = stats.value("state").toString();
        if (m_objects.contains("print_stats") && !QStringList{"standby", "printing", "paused", "complete", "cancelled", "error"}.contains(state)) {
            connectionFailed("Invalid print status"); return;
        }
        m_state = state;
        if (initial) {
            m_connected = true;
            updateCapabilities();
            Event event; event.type = EventType::ConnectionChanged; event.success = true; publish(event);
            m_poll.start();
        }
        Event event; event.type = EventType::StatusChanged;
        auto& out = event.status;
        out.deviceId = m_device.deviceId;
        out.name = m_device.name;
        out.ip = m_device.ip;
        out.state = state == "paused" ? "pause" : state == "complete" ? "completed"
            : state == "cancelled" ? "cancel" : state == "standby" || state.isEmpty() ? "ready" : state.toStdString();
        out.fileName = stats.value("filename").toString().toStdString();
        out.printDurationSeconds = stats.value("print_duration").toDouble();
        out.estimatedTimeSeconds = -1.0; // No metadata estimate was requested or inferred.
        const double progress = status.value("virtual_sdcard").toObject().value("progress").toDouble();
        out.progress = std::isfinite(progress) ? 100.0 * std::clamp(progress, 0.0, 1.0) : 0.0;
        const auto extruder = status.value("extruder").toObject();
        const auto bed = status.value("heater_bed").toObject();
        out.nozzleTemperature = extruder.value("temperature").toDouble();
        out.targetNozzleTemperature = extruder.value("target").toDouble();
        out.bedTemperature = bed.value("temperature").toDouble();
        out.targetBedTemperature = bed.value("target").toDouble();
        publish(event);
    });
}
void MoonrakerPrinterAgentImpl::rejectCommand(const std::string& command, uint64_t id, const QString& error) {
    Event event; event.type = EventType::JobCommandFinished;
    event.command = command; event.requestId = id; event.error = error.toStdString();
    publish(event);
}
void MoonrakerPrinterAgentImpl::finishCommand(const std::string& command, uint64_t id,
                                         const Response& response, const QString& fileName) {
    if (id == m_activeRequestId) { m_busy = false; m_activeRequestId = 0; }
    m_uncertain |= response.unknown;
    Event event; event.type = EventType::JobCommandFinished;
    event.requestId = id; event.command = command; event.success = response.ok;
    event.outcomeUnknown = response.unknown; event.error = response.error.toStdString();
    event.status.fileName = fileName.toStdString();
    publish(event);
}
void MoonrakerPrinterAgentImpl::submitJob(const PrintJob& job) {
    Response invalid; invalid.error = "Moonraker submission is unavailable or unsupported";
    if (job.requestId == 0 || m_busy || m_uncertain || !m_caps.upload || (job.printNow && !m_caps.start)
        || m_state == "printing" || m_state == "paused" || job.formatId != "gcode"
        || job.levelingBeforePrint || job.flowCalibration || job.firstLayerInspection
        || job.timeLapseVideo || job.useMaterialStation) {
        rejectCommand("send", job.requestId, invalid.error); return;
    }
    auto input = std::make_shared<QFile>(QString::fromStdString(job.filePath));
    auto snapshot = std::make_shared<QTemporaryFile>();
    if (!input->open(QIODevice::ReadOnly) || !QFileInfo(*input).isFile() || input->size() <= 0
        || input->peek(4).startsWith("PK\x03\x04") || !snapshot->open()) {
        invalid.error = "Cannot snapshot the G-code file"; finishCommand("send", job.requestId, invalid); return;
    }
    m_busy = true;
    m_activeRequestId = job.requestId;
    snapshotChunk(job, input, snapshot, m_generation, input->size(), QFileInfo(*input).lastModified());
}
void MoonrakerPrinterAgentImpl::snapshotChunk(PrintJob job, std::shared_ptr<QFile> input,
        std::shared_ptr<QTemporaryFile> snapshot, uint64_t generation, qint64 size, QDateTime modified) {
    if (generation != m_generation) return;
    Response failed; failed.error = "Cannot snapshot the G-code file, or the source changed";
    if (!input->atEnd()) {
        const auto chunk = input->read(1024 * 1024);
        if (chunk.isEmpty() || chunk.size() > size - snapshot->size()
            || snapshot->write(chunk) != chunk.size()) {
            finishCommand("send", job.requestId, failed); return;
        }
        QTimer::singleShot(0, this, [this, job, input, snapshot, generation, size, modified] {
            snapshotChunk(job, input, snapshot, generation, size, modified);
        });
        return;
    }
    if (snapshot->size() != size || input->size() != size || QFileInfo(*input).lastModified() != modified
        || !snapshot->flush() || !snapshot->seek(0)) {
        finishCommand("send", job.requestId, failed); return;
    }
    input->close();
    upload(job, snapshot);
}
void MoonrakerPrinterAgentImpl::upload(PrintJob job, std::shared_ptr<QTemporaryFile> snapshot) {
    const QString name = "gplatform-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".gcode";
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart root;
    root.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"root\"");
    root.setBody("gcodes"); multipart->append(root);
    // Deliberately omit the optional `print` part. Moonraker defaults to false.
    // Omission also avoids treating a nonempty string (even "false") as truthy.
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(name));
    file.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    file.setBodyDevice(snapshot.get());
    multipart->append(file);
    m_busy = true;
    m_activeRequestId = job.requestId;
    auto* reply = m_network.post(request("/server/files/upload"), multipart);
    multipart->setParent(reply); // Snapshot stays alive through completion, including aborts.
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::uploadProgress, this, [this, generation, id = job.requestId](qint64 sent, qint64 total) {
        if (generation != m_generation || total <= 0) return;
        Event event; event.type = EventType::TransferProgress; event.requestId = id;
        event.progress = double(sent) / double(total); publish(event);
    });
    watch(reply, true, [this, job, name, snapshot](Response response) {
        const auto result = response.result.toObject();
        const auto item = result.value("item").toObject();
        if (response.ok && (item.value("root").toString() != "gcodes" || item.value("path").toString() != name
            || !result.value("print_started").isBool() || result.value("print_started").toBool()
            || !result.value("print_queued").isBool() || result.value("print_queued").toBool())) {
            response.ok = false; response.unknown = true;
            response.error = "Upload outcome is unexpected. Check the printer before retrying.";
        }
        if (!response.ok || !job.printNow) {
            finishCommand("send", job.requestId, response, name); return;
        }
        // Refresh authoritative readiness AFTER upload. A previous idle status is
        // not permission to start after another client starts a job or Klipper fails.
        json("/printer/objects/query", {{"objects", QJsonObject{{"webhooks", QJsonValue::Null},
             {"print_stats", QJsonValue::Null}}}}, true, false,
             [this, id = job.requestId, name](const Response& checked) {
            const auto status = checked.result.toObject().value("status").toObject();
            const auto state = status.value("print_stats").toObject().value("state").toString();
            if (!checked.ok || !m_connected || !m_caps.start
                || status.value("webhooks").toObject().value("state").toString() != "ready"
                || !QStringList{"standby", "complete", "cancelled"}.contains(state)) {
                Response stopped;
                stopped.error = "File uploaded, but readiness could not be confirmed. Printing was not requested.";
                finishCommand("send", id, stopped, name);
                return;
            }
            // Never retry: a lost response may already have started physical motion.
            json("/printer/print/start", {{"filename", name}}, true, true,
                 [this, id, name](Response started) {
                if (started.ok && started.result.toString() != "ok") {
                    started.ok = false; started.unknown = true;
                    started.error = "Print start could not be confirmed";
                }
                finishCommand("send", id, started, name);
                queryStatus();
            });
        });
    }, m_uploadTimeoutMs, true);
}
void MoonrakerPrinterAgentImpl::controlJob(const std::string& action, uint64_t requestId) {
    const bool resume = action == "continue";
    const bool pause = action == "pause";
    const bool cancel = action == "cancel";
    if (requestId == 0 || m_busy || m_uncertain || !m_connected || (!(resume || pause || cancel))
        || ((resume || pause) && !m_caps.pauseResume) || (cancel && !m_caps.cancel)
        || (pause && m_state != "printing") || (resume && m_state != "paused")
        || (cancel && m_state != "printing" && m_state != "paused")) {
        Response response; response.error = "Print control is unavailable. Check the printer state.";
        rejectCommand(action, requestId, response.error); return;
    }
    m_busy = true;
    m_activeRequestId = requestId;
    const QString command = resume ? "resume" : QString::fromStdString(action);
    json("/printer/print/" + command, {}, true, true, [this, action, requestId](Response response) {
        if (response.ok && response.result.toString() != "ok") {
            response.ok = false; response.unknown = true; response.error = "Print control outcome is unknown";
        }
        finishCommand(action, requestId, response);
        queryStatus();
    });
}
void MoonrakerPrinterAgentImpl::setCameraStreamEnabled(bool enabled) {
    Event event; event.type = EventType::CameraStreamCommandFinished;
    event.command = enabled ? "open" : "close"; event.success = !enabled;
    if (enabled) event.error = "Moonraker camera is not supported";
    publish(event);
}
} // namespace GPlatform::Printer

namespace GPlatform::Printer {
class MoonrakerPrinterAgent::Impl final : public MoonrakerPrinterAgentImpl {
public:
    explicit Impl(const MoonrakerConfig& config)
        : MoonrakerPrinterAgentImpl(QUrl(QString::fromStdString(config.endpoint)),
              QByteArray::fromStdString(config.apiKey), config.requestTimeoutMs, config.pollIntervalMs, config.uploadTimeoutMs) {}
};
MoonrakerPrinterAgent::MoonrakerPrinterAgent(MoonrakerConfig config)
    : m_impl(std::make_unique<Impl>(config)) {}
MoonrakerPrinterAgent::~MoonrakerPrinterAgent() = default;
bool MoonrakerPrinterAgent::validConfig(const MoonrakerConfig& config) {
    return MoonrakerPrinterAgentImpl::validEndpoint(QUrl(QString::fromStdString(config.endpoint)))
        && config.apiKey.find_first_of("\r\n") == std::string::npos
        && config.requestTimeoutMs > 0 && config.pollIntervalMs > 0 && config.uploadTimeoutMs > 0;
}
void MoonrakerPrinterAgent::setEventSink(EventSink sink) { m_impl->setEventSink(std::move(sink)); }
void MoonrakerPrinterAgent::initialize() { m_impl->initialize(); }
void MoonrakerPrinterAgent::shutdown() { m_impl->shutdown(); }
void MoonrakerPrinterAgent::scan() { m_impl->scan(); }
void MoonrakerPrinterAgent::login(const std::string& a, const std::string& b) { m_impl->login(a, b); }
void MoonrakerPrinterAgent::logout() { m_impl->logout(); }
void MoonrakerPrinterAgent::selectDevice(const PrinterCore::Device& device) { m_impl->selectDevice(device); }
void MoonrakerPrinterAgent::provideDeviceAccess(const std::string& credential) { m_impl->provideDeviceAccess(credential); }
void MoonrakerPrinterAgent::releaseDevice() { m_impl->releaseDevice(); }
std::vector<std::string> MoonrakerPrinterAgent::supportedPrintFormats() const { return m_impl->supportedPrintFormats(); }
PrinterCore::Capabilities MoonrakerPrinterAgent::capabilities() const { return m_impl->capabilities(); }
void MoonrakerPrinterAgent::acknowledgeUnknownOutcome() { m_impl->acknowledgeUnknownOutcome(); }
void MoonrakerPrinterAgent::submitJob(const PrinterCore::PrintJob& job) { m_impl->submitJob(job); }
void MoonrakerPrinterAgent::controlJob(const std::string& action, uint64_t id) { m_impl->controlJob(action, id); }
void MoonrakerPrinterAgent::setCameraStreamEnabled(bool enabled) { m_impl->setCameraStreamEnabled(enabled); }
} // namespace GPlatform::Printer
