// Black-box tests of the standalone public API. All traffic stays on loopback.
// No vendor SDK, GUI application, private headers, or physical printer is used.
#include "Moonraker/MoonrakerPrinterAgent.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace GPlatform::Printer;
using namespace GPlatform::PrinterCore;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate>
void until(Predicate predicate, const char* message, int timeout = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    check(predicate(), message);
}
void pump(int milliseconds = 30) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
}

class HttpPrinter {
public:
    struct Request {
        QByteArray method;
        QString path;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
        QPointer<QTcpSocket> socket;
    };
    QTcpServer server;
    std::vector<Request> requests;
    std::function<bool(int)> intercept;
    std::function<void(const QString&)> headersReceived;
    QString state{"standby"};
    QString klippyState{"ready"};
    QString filename;
    QString protocolError;
    double progress{0.37};

    HttpPrinter() {
        check(server.listen(QHostAddress::LocalHost, 0), "cannot listen on loopback");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                auto headersNotified = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, &server,
                    [this, socket, bytes, headersNotified] {
                    bytes->append(socket->readAll());
                    const auto split = bytes->indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const auto lines = bytes->left(split).split('\n');
                    const auto first = lines.front().trimmed().split(' ');
                    if (first.size() < 2) { protocolError = "invalid HTTP request line"; return; }
                    Request req;
                    req.method = first[0];
                    req.path = QString::fromUtf8(first[1]);
                    req.socket = socket;
                    for (int i = 1; i < lines.size(); ++i) {
                        const auto colon = lines[i].indexOf(':');
                        if (colon > 0) req.headers.insert(lines[i].left(colon).trimmed().toLower(),
                                                        lines[i].mid(colon + 1).trimmed());
                    }
                    if (!*headersNotified) {
                        *headersNotified = true;
                        if (headersReceived) headersReceived(req.path);
                    }
                    // QFile-backed multipart has a known length. Reject unexpected
                    // framing instead of silently giving the test a false success.
                    if (req.headers.value("transfer-encoding").toLower().contains("chunked")) {
                        protocolError = "unexpected chunked request framing";
                        socket->abort();
                        return;
                    }
                    const qint64 length = req.headers.value("content-length").toLongLong();
                    if (bytes->size() - split - 4 < length) return;
                    req.body = bytes->mid(split + 4, length);
                    QObject::disconnect(socket, &QTcpSocket::readyRead, &server, nullptr);
                    requests.push_back(std::move(req));
                    const int index = static_cast<int>(requests.size()) - 1;
                    if (!intercept || !intercept(index)) respondDefault(index);
                });
            }
        });
    }
    std::string endpoint() const {
        return QStringLiteral("http://127.0.0.1:%1/moon").arg(server.serverPort()).toStdString();
    }
    bool is(int index, const char* suffix) const { return requests.at(index).path.endsWith(QString::fromLatin1(suffix)); }
    int count(const char* suffix) const {
        return static_cast<int>(std::count_if(requests.begin(), requests.end(), [suffix](const Request& r) {
            return r.path.endsWith(QString::fromLatin1(suffix));
        }));
    }
    QString uploadName(int index) const {
        const auto match = QRegularExpression("filename=\"([^\"]+)\"")
            .match(QString::fromUtf8(requests.at(index).body));
        return match.captured(1);
    }
    void respond(int index, QJsonValue result, int status = 200) {
        const auto body = QJsonDocument(QJsonObject{{"result", result}}).toJson(QJsonDocument::Compact);
        raw(index, body, status);
    }
    void raw(int index, const QByteArray& body, int status = 200) {
        auto socket = requests.at(index).socket;
        if (!socket || socket->state() == QAbstractSocket::UnconnectedState) return;
        const QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
            "Content-Type: application/json\r\nConnection: close\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        socket->write(response);
        socket->disconnectFromHost();
    }
    void drop(int index) { if (requests.at(index).socket) requests.at(index).socket->abort(); }
    void uploadResponse(int index, bool wrapped = false) {
        const QJsonObject result{{"item", QJsonObject{{"root", "gcodes"}, {"path", uploadName(index)}}},
                                 {"print_started", false}, {"print_queued", false}};
        // Moonraker FileUploadHandler responds with a bare object and HTTP 201,
        // unlike the result envelope used by its JSON API handlers.
        if (wrapped) respond(index, result); // Legacy/wrapped compatibility fixture.
        else raw(index, QJsonDocument(result).toJson(QJsonDocument::Compact), 201);
    }
    void respondDefault(int index) {
        if (is(index, "/server/info")) {
            respond(index, QJsonObject{{"klippy_connected", true}, {"klippy_state", klippyState},
                {"components", QJsonArray{"file_manager"}}, {"registered_directories", QJsonArray{"gcodes"}}});
        } else if (is(index, "/printer/info")) {
            respond(index, QJsonObject{{"state", klippyState}});
        } else if (is(index, "/printer/objects/list")) {
            respond(index, QJsonObject{{"objects", QJsonArray{"webhooks", "print_stats", "virtual_sdcard",
                                                               "pause_resume", "extruder", "heater_bed"}}});
        } else if (is(index, "/printer/objects/query")) {
            respond(index, QJsonObject{{"status", QJsonObject{
                {"webhooks", QJsonObject{{"state", klippyState}}},
                {"print_stats", QJsonObject{{"state", state}, {"filename", filename}, {"print_duration", 12.0}}},
                {"virtual_sdcard", QJsonObject{{"progress", progress}}},
                {"extruder", QJsonObject{{"temperature", 190.0}, {"target", 200.0}}},
                {"heater_bed", QJsonObject{{"temperature", 55.0}, {"target", 60.0}}}}}});
        } else if (is(index, "/server/files/upload")) {
            uploadResponse(index);
        } else if (is(index, "/printer/print/start")) {
            filename = QJsonDocument::fromJson(requests[index].body).object().value("filename").toString();
            state = "printing";
            respond(index, "ok");
        } else if (is(index, "/printer/print/pause")) {
            state = "paused"; respond(index, "ok");
        } else if (is(index, "/printer/print/resume")) {
            state = "printing"; respond(index, "ok");
        } else if (is(index, "/printer/print/cancel")) {
            state = "cancelled"; respond(index, "ok");
        } else {
            protocolError = "unexpected route: " + requests[index].path;
            raw(index, "{}", 404);
        }
    }
};

struct Fixture {
    HttpPrinter http;
    QTemporaryDir directory;
    QString path;
    std::vector<Event> events;
    std::unique_ptr<MoonrakerPrinterAgent> agent;
    Device selected;
    explicit Fixture(int poll = 100000, int timeout = 400, int uploadTimeout = -1) {
        check(directory.isValid(), "cannot create temporary G-code directory");
        path = directory.filePath("source.gcode");
        write("G90\n; immutable-original\nG1 X1\n");
        MoonrakerConfig config{http.endpoint(), "test-key", timeout, poll};
        config.uploadTimeoutMs = uploadTimeout > 0 ? uploadTimeout : timeout;
        agent = std::make_unique<MoonrakerPrinterAgent>(std::move(config));
        agent->setEventSink([this](const Event& e) { events.push_back(e); });
    }
    ~Fixture() { agent.reset(); }
    void write(const QByteArray& bytes) {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
              "cannot write G-code fixture");
    }
    void ready() {
        agent->initialize();
        until([&] { return has(EventType::Availability); }, "availability event missing");
        agent->scan();
        until([&] { return has(EventType::ScanFinished); }, "scan event missing");
        for (const auto& e : events) if (e.type == EventType::ScanFinished && !e.devices.empty()) selected = e.devices.front();
        check(!selected.deviceId.empty(), "scan did not expose configured endpoint");
        selected.sessionId = 10;
        agent->selectDevice(selected);
        until([&] { return connected(10); }, "Moonraker handshake failed");
        until([&] { return has(EventType::StatusChanged); }, "initial status missing");
    }
    bool has(EventType type) const {
        return std::any_of(events.begin(), events.end(), [type](const Event& e) { return e.type == type; });
    }
    bool connected(uint64_t session) const {
        return std::any_of(events.begin(), events.end(), [session](const Event& e) {
            return e.type == EventType::ConnectionChanged && e.success && e.sessionId == session;
        });
    }
    const Event* done(uint64_t id) const {
        for (auto i = events.rbegin(); i != events.rend(); ++i)
            if (i->type == EventType::JobCommandFinished && i->requestId == id) return &*i;
        return nullptr;
    }
    Event awaitDone(uint64_t id) {
        until([&] { return done(id); }, "command completion timed out");
        Event result = *done(id);
        check(result.deviceId == selected.deviceId && result.sessionId == selected.sessionId,
              "completion identity was not retained");
        return result;
    }
    void submit(uint64_t id, bool printNow = false) {
        PrintJob job;
        job.requestId = id;
        job.formatId = "gcode";
        job.filePath = path.toStdString();
        job.destinationName = "friendly.gcode";
        job.printNow = printNow;
        agent->submitJob(job);
    }
};

void handshakeAndUpload() {
    Fixture f;
    f.ready();
    check(f.agent->capabilities().upload && f.agent->capabilities().start
          && f.agent->supportedPrintFormats() == std::vector<std::string>{"gcode"}, "capability handshake failed");
    bool progressOk = false;
    for (const auto& e : f.events) if (e.type == EventType::StatusChanged) progressOk |= e.status.progress == 37.0;
    check(progressOk, "Moonraker 0.37 physical progress was not converted to 37 percent");
    f.submit(1);
    const auto first = f.awaitDone(1);
    check(first.success && !first.status.fileName.empty(), "upload-only did not succeed with remote filename");
    check(f.http.count("/printer/print/start") == 0, "upload-only unexpectedly started printing");
    f.submit(2);
    const auto second = f.awaitDone(2);
    check(second.success && first.status.fileName != second.status.fileName, "remote upload filenames were reused");
    for (int i = 0; i < static_cast<int>(f.http.requests.size()); ++i) {
        const auto& r = f.http.requests[i];
        check(r.path.startsWith("/moon/"), "configured endpoint path prefix was lost");
        check(r.headers.value("x-api-key") == "test-key", "configured API key was not supplied");
        const bool readOnlyGet = f.http.is(i, "/server/info") || f.http.is(i, "/printer/info")
            || f.http.is(i, "/printer/objects/list");
        check(r.method == (readOnlyGet ? "GET" : "POST"), "Moonraker HTTP method was incorrect");
        if (f.http.is(i, "/server/files/upload")) {
            check(!r.body.contains("name=\"print\""), "upload supplied a potentially truthy print form field");
            check(r.body.contains("immutable-original") && r.body.contains("gcodes"), "multipart file/root body missing");
        }
    }
    int transferEvents = 0;
    for (const auto& e : f.events) if (e.type == EventType::TransferProgress) {
        ++transferEvents;
        check((e.requestId == 1 || e.requestId == 2) && e.sessionId == 10
              && e.deviceId == f.selected.deviceId && e.progress >= 0.0 && e.progress <= 1.0,
              "transfer progress identity or normalized units changed");
    }
    check(transferEvents > 0, "upload did not publish any transfer progress event");
    check(f.http.protocolError.isEmpty(), "HTTP fake observed an invalid protocol request");
}

void wrappedUploadCompatibility() {
    Fixture f;
    f.ready();
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/server/files/upload")) return false;
        f.http.uploadResponse(i, true);
        return true;
    };
    f.submit(4, true);
    check(f.awaitDone(4).success && f.http.count("/printer/print/start") == 1,
          "wrapped upload compatibility regressed");
}

void bareJsonRejectedOutsideUpload() {
    for (const int statusCode : {200, 201}) {
        Fixture query;
        query.ready();
        query.http.intercept = [&](int i) {
            if (!query.http.is(i, "/printer/objects/query")) return false;
            const QJsonObject bare{{"status", QJsonObject{
                {"webhooks", QJsonObject{{"state", "ready"}}},
                {"print_stats", QJsonObject{{"state", "standby"}}}}}};
            query.http.raw(i, QJsonDocument(bare).toJson(QJsonDocument::Compact), statusCode);
            return true;
        };
        query.submit(5, true);
        check(!query.awaitDone(5).success && query.http.count("/printer/print/start") == 0,
              "bare query response was incorrectly accepted as authoritative readiness");

        Fixture control;
        control.http.state = "printing";
        control.ready();
        control.http.intercept = [&](int i) {
            if (!control.http.is(i, "/printer/print/pause")) return false;
            control.http.raw(i, "{\"status\":\"ok\"}", statusCode);
            return true;
        };
        control.agent->controlJob("pause", 6);
        const auto response = control.awaitDone(6);
        check(!response.success && response.outcomeUnknown,
              "bare control response was incorrectly accepted as confirmed success");
    }
}

void uploadThenStartAndRecheck() {
    Fixture f;
    f.ready();
    const int priorQueries = f.http.count("/printer/objects/query");
    f.submit(3, true);
    check(f.awaitDone(3).success, "upload-then-start did not succeed");
    check(f.http.count("/printer/print/start") == 1, "printNow did not issue exactly one explicit start");
    int upload = -1, query = -1, start = -1;
    for (int i = 0; i < static_cast<int>(f.http.requests.size()); ++i) {
        if (f.http.is(i, "/server/files/upload")) upload = i;
        if (upload >= 0 && f.http.is(i, "/printer/objects/query") && start < 0) query = i;
        if (f.http.is(i, "/printer/print/start")) start = i;
    }
    check(upload >= 0 && query > upload && start > query && f.http.count("/printer/objects/query") > priorQueries,
          "start was not preceded by fresh post-upload readiness validation");
    check(f.http.filename == f.http.uploadName(upload), "start did not use the unique returned upload identity");
}

void busyRejectDoesNotUnlock() {
    Fixture f;
    f.ready();
    int held = -1;
    f.http.intercept = [&](int i) { if (f.http.is(i, "/server/files/upload")) { held = i; return true; } return false; };
    f.submit(10);
    until([&] { return held >= 0; }, "upload did not enter held response");
    f.submit(10); // Even accidental reuse of the active request ID cannot unlock it.
    check(!f.awaitDone(10).success, "same-ID duplicate upload was not rejected");
    f.submit(11);
    check(!f.awaitDone(11).success, "second upload was not rejected while busy");
    f.agent->controlJob("pause", 12);
    check(!f.awaitDone(12).success, "busy control was not rejected");
    f.agent->acknowledgeUnknownOutcome(); // Must not clear an active operation.
    f.submit(13);
    check(!f.awaitDone(13).success && f.http.count("/server/files/upload") == 1,
          "rejection or acknowledgement unlocked the active upload");
    f.http.uploadResponse(held);
    until([&] { return f.done(10) && f.done(10)->success; },
          "rejected duplicate corrupted the original completion");
}

void uncertainUploadAndAcknowledgement(int failure) {
    Fixture f;
    f.ready();
    int held = -1;
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/server/files/upload")) return false;
        held = i;
        if (failure == 0) f.http.drop(i);
        if (failure == 400) f.http.raw(i, "{\"error\":{\"message\":\"partial mutation\"}}", 400);
        return true; // failure==1 exercises absolute request deadline.
    };
    f.submit(20);
    const auto failed = f.awaitDone(20);
    check(!failed.success && failed.outcomeUnknown, "lost/400/timed-out upload was not unknown");
    f.submit(21);
    check(!f.awaitDone(21).success && f.http.count("/server/files/upload") == 1,
          "uncertain mutation was automatically retried without acknowledgement");
    f.http.intercept = {};
    f.agent->acknowledgeUnknownOutcome();
    f.submit(22);
    check(f.awaitDone(22).success && f.http.count("/server/files/upload") == 2,
          "explicit unknown acknowledgement did not permit a fresh submission");
    check(held >= 0, "failure fixture never received the upload");
}

void uploadUsesIndependentLongDeadline() {
    Fixture f(100000, 80, 800);
    f.ready();
    bool delayedResponse = false;
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/server/files/upload")) return false;
        // Deliberately exceed the short handshake/status/command deadline.
        QTimer::singleShot(200, &f.http.server, [&, i] {
            delayedResponse = true;
            f.http.uploadResponse(i);
        });
        return true;
    };
    f.submit(23);
    const auto result = f.awaitDone(23);
    check(delayedResponse && result.success && !result.outcomeUnknown,
          "slow upload incorrectly inherited the short query/command timeout");
    check(f.http.count("/server/files/upload") == 1 && f.http.count("/printer/print/start") == 0,
          "slow upload retried or unexpectedly started printing");
}

void startAndControl400AreUnknown() {
    Fixture f;
    f.ready();
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/printer/print/start")) return false;
        f.http.raw(i, "{}", 400);
        return true;
    };
    f.submit(30, true);
    check(f.awaitDone(30).outcomeUnknown, "HTTP400 start was treated as definitive nonexecution");
    check(f.http.count("/printer/print/start") == 1, "ambiguous start was retried");

    Fixture control;
    control.http.state = "printing";
    control.ready();
    control.http.intercept = [&](int i) {
        if (!control.http.is(i, "/printer/print/pause")) return false;
        control.http.raw(i, "{}", 400);
        return true;
    };
    control.agent->controlJob("pause", 31);
    check(control.awaitDone(31).outcomeUnknown, "HTTP400 pause was treated as definitive nonexecution");
}

void postUploadMustStillBeIdle() {
    Fixture f;
    f.ready();
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/server/files/upload")) return false;
        f.http.state = "printing"; // Another client started a different file.
        return false;
    };
    f.submit(40, true);
    check(!f.awaitDone(40).success && f.http.count("/printer/print/start") == 0,
          "stale pre-upload idle state authorized a new physical start");
}

void disconnectOrSwitchPreventsLateStart(bool switchSession) {
    Fixture f(switchSession ? 100000 : 20);
    f.ready();
    int held = -1;
    f.http.intercept = [&](int i) { if (f.http.is(i, "/server/files/upload")) { held = i; return true; } return false; };
    f.submit(50, true);
    until([&] { return held >= 0; }, "held upload missing");
    if (switchSession) {
        ++f.selected.sessionId;
        f.agent->selectDevice(f.selected);
        until([&] { return f.connected(f.selected.sessionId); }, "new selected session did not connect");
    } else {
        f.http.klippyState = "shutdown";
        until([&] {
            return std::any_of(f.events.begin(), f.events.end(), [](const Event& e) {
                return e.type == EventType::ConnectionChanged && !e.success;
            });
        }, "poll failure did not disconnect the session");
    }
    f.http.uploadResponse(held);
    pump(50);
    check(f.http.count("/printer/print/start") == 0, "late upload response started a retired connection");
    check(!f.done(50) || !f.done(50)->success, "retired upload reported success");
}

void snapshotAndShutdown() {
    Fixture f;
    f.ready();
    QByteArray original("G90\n; immutable-original\n");
    original += QByteArray(2 * 1024 * 1024, ';');
    f.write(original);
    bool changed = false;
    f.http.headersReceived = [&](const QString& path) {
        if (path.endsWith("/server/files/upload")) { f.write("G90\n; replaced-after-snapshot\n"); changed = true; }
    };
    f.submit(60);
    check(f.awaitDone(60).success && changed, "snapshot upload failed");
    for (int i = 0; i < static_cast<int>(f.http.requests.size()); ++i) if (f.http.is(i, "/server/files/upload")) {
        check(f.http.requests[i].body.contains(original) && !f.http.requests[i].body.contains("replaced-after-snapshot"),
              "upload read mutable source instead of completed immutable snapshot");
    }
    f.http.headersReceived = {};
    int held = -1;
    f.http.intercept = [&](int i) { if (f.http.is(i, "/server/files/upload")) { held = i; return true; } return false; };
    f.submit(61, true);
    until([&] { return held >= 0; }, "shutdown fixture upload missing");
    const auto count = f.events.size();
    QElapsedTimer elapsed;
    elapsed.start();
    f.agent->shutdown();
    f.agent.reset();
    check(elapsed.elapsed() < 100, "shutdown waited on a pending network request");
    f.http.uploadResponse(held);
    pump(50);
    check(f.events.size() == count && f.http.count("/printer/print/start") == 0,
          "late reply delivered after shutdown or started physical printing");

    Fixture preparing;
    preparing.ready();
    preparing.write(original);
    preparing.submit(62, true);
    preparing.agent.reset(); // Snapshot has queued continuations, but no HTTP yet.
    pump(30);
    check(preparing.http.count("/server/files/upload") == 0, "destroyed snapshot preparation continued uploading");
}

void authorizationRetry() {
    Fixture f;
    f.selected.deviceId = "moonraker:" + f.http.endpoint();
    f.selected.sessionId = 80;
    bool reject = true;
    f.http.intercept = [&](int i) {
        if (!reject || !f.http.is(i, "/server/info")) return false;
        f.http.raw(i, "{}", 401);
        return true;
    };
    f.agent->selectDevice(f.selected);
    until([&] {
        return std::any_of(f.events.begin(), f.events.end(), [](const Event& e) {
            return e.type == EventType::ConnectionChanged && e.accessRequired && !e.success;
        });
    }, "401 handshake did not request credentials");
    check(!f.agent->capabilities().upload, "unauthorized handshake enabled mutation capabilities");
    reject = false;
    f.agent->provideDeviceAccess("replacement-key");
    until([&] { return f.connected(80); }, "API-key retry did not preserve the connection attempt identity");
    check(f.http.requests.front().headers.value("x-api-key") == "test-key"
          && f.http.requests.back().headers.value("x-api-key") == "replacement-key",
          "API-key retry did not replace the in-memory credential");
}

void lostStartAndMalformedUpload() {
    Fixture f;
    f.ready();
    f.http.intercept = [&](int i) {
        if (!f.http.is(i, "/printer/print/start")) return false;
        f.http.drop(i);
        return true;
    };
    f.submit(81, true);
    check(f.awaitDone(81).outcomeUnknown, "lost start acknowledgement was treated as safe to retry");
    pump(30);
    check(f.http.count("/printer/print/start") == 1, "lost start was automatically retried");

    Fixture malformed;
    malformed.ready();
    malformed.http.intercept = [&](int i) {
        if (!malformed.http.is(i, "/server/files/upload")) return false;
        malformed.http.respond(i, QJsonObject{{"item", QJsonObject{{"root", "gcodes"}, {"path", "different.gcode"}}},
                                             {"print_started", false}, {"print_queued", false}});
        return true;
    };
    malformed.submit(82, true);
    check(malformed.awaitDone(82).outcomeUnknown && malformed.http.count("/printer/print/start") == 0,
          "unexpected upload identity was accepted and started");
}

void sourceChangeDuringPreparation() {
    Fixture f;
    f.ready();
    f.write("G90\n" + QByteArray(2 * 1024 * 1024, ';'));
    f.submit(83);
    // submitJob copies only the first chunk before returning to its event loop.
    f.write("G90\n; replaced-before-snapshot-completed\n");
    const auto failed = f.awaitDone(83);
    check(!failed.success && !failed.outcomeUnknown && f.http.count("/server/files/upload") == 0,
          "modified source was uploaded as an inconsistent snapshot");
    f.submit(84);
    check(f.awaitDone(84).success, "failed snapshot left transaction busy");
}

void controlLifecycle() {
    Fixture f;
    f.http.state = "printing";
    f.ready();
    auto waitState = [&](const char* state) {
        until([&] {
            return std::any_of(f.events.begin(), f.events.end(), [state](const Event& e) {
                return e.type == EventType::StatusChanged && e.status.state == state;
            });
        }, "control completion did not refresh physical printer state");
    };
    f.agent->controlJob("pause", 85);
    check(f.awaitDone(85).success, "pause command failed");
    waitState("pause");
    f.events.clear();
    f.agent->controlJob("continue", 86);
    check(f.awaitDone(86).success, "continue was not translated to Moonraker resume");
    waitState("printing");
    f.events.clear();
    f.agent->controlJob("cancel", 87);
    check(f.awaitDone(87).success, "cancel command failed");
    waitState("cancel");
    check(f.http.count("/printer/print/pause") == 1 && f.http.count("/printer/print/resume") == 1
          && f.http.count("/printer/print/cancel") == 1, "control routes were not dispatched exactly once");
}

void reentrantSinkShutdown() {
    Fixture f;
    bool destroyed = false;
    f.agent->setEventSink([&](const Event& event) {
        f.events.push_back(event);
        if (event.type == EventType::CapabilitiesChanged) { f.agent.reset(); destroyed = true; }
    });
    f.selected.deviceId = "manual-moonraker";
    f.selected.sessionId = 90;
    f.agent->selectDevice(f.selected);
    until([&] { return destroyed; }, "reentrant capability callback was not exercised");
    const auto size = f.events.size();
    pump(30);
    check(f.events.size() == size, "events escaped an agent destroyed from its own event sink");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"handshake/key/path prefix/upload-only/unique name/progress", handshakeAndUpload},
        {"wrapped upload response compatibility", wrappedUploadCompatibility},
        {"bare JSON rejected for query and control", bareJsonRejectedOutsideUpload},
        {"upload then fresh readiness then start", uploadThenStartAndRecheck},
        {"busy rejection preserves active transaction", busyRejectDoesNotUnlock},
        {"lost upload and explicit acknowledgement", [] { uncertainUploadAndAcknowledgement(0); }},
        {"HTTP400 upload and explicit acknowledgement", [] { uncertainUploadAndAcknowledgement(400); }},
        {"deadline cancellation and explicit acknowledgement", [] { uncertainUploadAndAcknowledgement(1); }},
        {"independent long upload deadline", uploadUsesIndependentLongDeadline},
        {"HTTP400 start and control uncertainty", startAndControl400AreUnknown},
        {"post-upload idle revalidation", postUploadMustStillBeIdle},
        {"disconnect suppresses delayed start", [] { disconnectOrSwitchPreventsLateStart(false); }},
        {"selection switch suppresses delayed start", [] { disconnectOrSwitchPreventsLateStart(true); }},
        {"immutable snapshot and shutdown cancellation", snapshotAndShutdown},
        {"authorization challenge and API-key retry", authorizationRetry},
        {"lost start and unexpected upload identity", lostStartAndMalformedUpload},
        {"source changes during snapshot preparation", sourceChangeDuringPreparation},
        {"pause/resume/cancel lifecycle", controlLifecycle},
        {"reentrant sink destruction", reentrantSinkShutdown},
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }
    return 0;
}
