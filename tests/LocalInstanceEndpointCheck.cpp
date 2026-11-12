#include "LocalInstanceEndpoint.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("GPlatformTests");
    QCoreApplication::setApplicationName("LocalInstanceEndpointCheck");
    QStandardPaths::setTestModeEnabled(true);
    const auto args = app.arguments();
    if (args.contains("--raw")) {
        auto executable = QFileInfo(app.applicationFilePath()).canonicalFilePath();
#ifdef Q_OS_WIN
        executable = executable.toCaseFolded();
#endif
        const auto key = QString::fromLatin1(QCryptographicHash::hash((executable + '\n' +
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).toUtf8(), QCryptographicHash::Sha256).toHex());
        QString address = "gplatform-" + key;
#ifdef Q_OS_UNIX
        address = QString("/tmp/gplatform-%1/").arg(getuid()) + key;
#endif
        QLocalSocket socket; socket.connectToServer(address);
        if (!socket.waitForConnected(3000)) return 3;
        const QJsonObject message{{"version", 1}, {"batchId", "dcad2000-ea4a-4531-a452-03012b6bf59a"},
            {"requests", QJsonArray{QJsonObject{{"id", "c1cab000-ea4a-4531-a452-03012b6bf59d"},
                {"file", "/tmp/lost-ack.stl"}}}}};
        const auto data = QJsonDocument(message).toJson(QJsonDocument::Compact);
        QByteArray bytes;
        for (int shift : {24, 16, 8, 0}) bytes.append(char(quint32(data.size()) >> shift));
        socket.write(bytes + data);
        if (!socket.waitForReadyRead(3000)) return 4;
        if (args.contains("--drop-ack")) { socket.abort(); return 0; }
        QByteArray response = socket.readAll();
        while (response.size() < 4 && socket.waitForReadyRead(3000)) response += socket.readAll();
        if (response.size() < 4) return 5;
        quint32 size = 0; for (int i = 0; i < 4; ++i) size = (size << 8) | quint8(response[i]);
        while (response.size() < size + 4 && socket.waitForReadyRead(3000)) response += socket.readAll();
        return QJsonDocument::fromJson(response.mid(4, size)).object().value("accepted").toBool() ? 0 : 6;
    }
    ExternalOpenRequest request{"c1cab000-ea4a-4531-a452-03012b6bf59a", "/tmp/模型 with space.stl", {}, {}};
    if (args.contains("--client") || args.contains("--crash")) {
        if (args.contains("--conflict")) request.localPath = "/tmp/different.stl";
        if (args.contains("--remote")) {
            request = {"c1cab000-ea4a-4531-a452-03012b6bf59b", {},
                QUrl("https://example.com/123"), "literal%2F.stl"};
        }
        if (args.contains("--full")) request.id = "c1cab000-ea4a-4531-a452-03012b6bf59c";
        LocalInstanceEndpoint endpoint;
        QString error;
        const auto result = endpoint.startOrForward({request}, [](const auto&, QString&) { return true; }, error);
        if (args.contains("--crash") && result == LocalInstanceEndpoint::StartResult::Primary) std::_Exit(99);
        return result == LocalInstanceEndpoint::StartResult::Forwarded ? 0 : 2;
    }
    const auto require = [](bool ok, const char* text) { if (!ok) throw std::runtime_error(text); };
    try {
        LocalInstanceEndpoint endpoint;
        QString error;
        int received = 0;
        bool full = false;
        QString remoteName;
        require(endpoint.startOrForward({}, [&](const auto& requests, QString& error) {
            if (full) { error = "full"; return false; }
            for (const auto& r : requests) if (r.remote()) remoteName = r.downloadFileName;
            received += requests.size(); return true;
        }, error) == LocalInstanceEndpoint::StartResult::Primary, "cannot become primary");
        const auto child = [&](const QStringList& arguments) {
            QProcess process;
            QEventLoop loop;
            QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(7000, &loop, &QEventLoop::quit);
            process.start(app.applicationFilePath(), arguments);
            loop.exec();
            if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); return -1; }
            return process.exitCode();
        };
        require(child({"--client"}) == 0 && received == 1, "handoff lost input");
        require(child({"--client"}) == 0 && received == 1, "request ID duplicated across batches");
        require(child({"--client", "--conflict"}) == 2 && received == 1, "conflicting ID accepted");
        endpoint.markFinished(request.id);
        require(child({"--client"}) == 0 && received == 1, "completed receipt forgotten");
        require(child({"--client", "--remote"}) == 0 && received == 2 && remoteName == "literal%2F.stl", "IPC decoded literal filename twice");
        full = true;
        require(child({"--client", "--full"}) == 2 && received == 2, "rejected queue was acknowledged");
        full = false;
        require(child({"--client", "--full"}) == 0 && received == 3, "rejected request was lost in dedup cache");
        require(child({"--raw", "--drop-ack"}) == 0 && received == 4, "lost-ACK fixture not accepted");
        require(child({"--raw"}) == 0 && received == 4, "same batch retried after lost ACK imported twice");
        endpoint.stop();
        require(child({"--crash"}) == 99, "crash fixture could not own scope");
        LocalInstanceEndpoint recovered;
        require(recovered.startOrForward({}, [](const auto&, QString&) { return true; }, error) ==
            LocalInstanceEndpoint::StartResult::Primary, "crashed lock/socket not recovered");
        std::cout << "PASS native handoff, cross-batch request deduplication, conflict and crashed-owner recovery\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
