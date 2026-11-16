#include "LocalInstanceEndpoint.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {
constexpr int MaxFrame = 128 * 1024;
constexpr qint64 Retention = 5 * 60 * 1000;
QByteArray frame(const QJsonObject& message) {
    const auto data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const quint32 size = data.size();
    QByteArray result;
    for (int shift : {24, 16, 8, 0}) result.append(char(size >> shift));
    return result + data;
}
int frameSize(const QByteArray& data) {
    if (data.size() < 4) return -1;
    quint32 size = 0;
    for (int i = 0; i != 4; ++i) size = (size << 8) | quint8(data[i]);
    return size > MaxFrame ? 0 : int(size);
}
QJsonArray encode(const QVector<ExternalOpenRequest>& requests) {
    QJsonArray result;
    for (const auto& r : requests)
        result.append(QJsonObject{{"id", r.id}, {"file", r.localPath},
            {"url", r.downloadUrl.toString(QUrl::FullyEncoded)}, {"name", r.downloadFileName},
            {"invalid", !r.inputError.isEmpty()}, {"invalidInput", r.rejectedInput}});
    return result;
}
}

LocalInstanceEndpoint::LocalInstanceEndpoint(QObject* parent) : QObject(parent) {}
LocalInstanceEndpoint::~LocalInstanceEndpoint() { stop(); }

LocalInstanceEndpoint::StartResult LocalInstanceEndpoint::startOrForward(
    const QVector<ExternalOpenRequest>& requests, Receiver receiver, QString& error) {
    m_receiver = std::move(receiver);
    QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
#ifdef Q_OS_WIN
    executable = executable.toCaseFolded();
#endif
    const auto dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (executable.isEmpty() || dataRoot.isEmpty()) { error = "Cannot identify the desktop application"; return StartResult::Failed; }
    const auto key = QString::fromLatin1(QCryptographicHash::hash(
        (executable + '\n' + dataRoot).toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto lockRoot = dataRoot + "/desktop-open";
    if (!QDir().mkpath(lockRoot)) { error = "Cannot create the desktop lock directory"; return StartResult::Failed; }
    m_endpoint = "gplatform-" + key;
#ifdef Q_OS_UNIX
    const auto socketRoot = QString("/tmp/gplatform-%1").arg(getuid());
    const auto rootBytes = QFile::encodeName(socketRoot);
    if (::mkdir(rootBytes.constData(), 0700) != 0 && errno != EEXIST) {
        error = "Cannot create the private desktop socket directory"; return StartResult::Failed;
    }
    struct stat info{};
    if (::lstat(rootBytes.constData(), &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != getuid() || (info.st_mode & 0777) != 0700) {
        error = "The desktop socket directory is not private to this user"; return StartResult::Failed;
    }
    m_endpoint = socketRoot + '/' + key;
    if (QFile::encodeName(m_endpoint).size() >= int(sizeof(sockaddr_un::sun_path))) {
        error = "The desktop socket path is too long"; return StartResult::Failed;
    }
#endif
    m_lock = std::make_unique<QLockFile>(lockRoot + '/' + key + ".lock");
    m_lock->setStaleLockTime(0);
    if (m_lock->tryLock()) {
        m_server = std::make_unique<QLocalServer>();
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
        // Only the process owning this scope's lock may remove its stale socket.
        QLocalServer::removeServer(m_endpoint);
        if (!m_server->listen(m_endpoint)) {
            error = m_server->errorString(); stop(); return StartResult::Failed;
        }
        connect(m_server.get(), &QLocalServer::newConnection, this, &LocalInstanceEndpoint::acceptConnections);
        return StartResult::Primary;
    }
    if (m_lock->error() != QLockFile::LockFailedError) {
        error = "Cannot acquire the desktop instance lock"; return StartResult::Failed;
    }
    const QString batchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto bytes = frame({{"version", 1}, {"batchId", batchId}, {"requests", encode(requests)}});
    if (bytes.size() > MaxFrame + 4) { error = "Too many external inputs"; return StartResult::Failed; }
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < 5000) {
        QLocalSocket socket;
        socket.connectToServer(m_endpoint);
        if (!socket.waitForConnected(qMin(200, 5000 - int(elapsed.elapsed())))) continue;
        socket.write(bytes);
        QByteArray response;
        while (elapsed.elapsed() < 5000) {
            response += socket.readAll();
            const int size = frameSize(response);
            if (size == 0) break;
            if (size > 0 && response.size() >= size + 4) {
                const auto ack = QJsonDocument::fromJson(response.mid(4, size)).object();
                if (ack.value("batchId").toString() != batchId) break;
                if (ack.value("accepted").toBool()) return StartResult::Forwarded;
                error = ack.value("error").toString(); return StartResult::Failed;
            }
            if (!socket.waitForReadyRead(qMax(1, 5000 - int(elapsed.elapsed())))) break;
        }
        // The original UUIDs are reused; losing an ACK must not duplicate imports.
    }
    error = "The running application did not acknowledge the open request";
    return StartResult::Failed;
}

void LocalInstanceEndpoint::acceptConnections() {
    while (m_server && m_server->hasPendingConnections()) {
        auto* socket = m_server->nextPendingConnection();
        if (m_connections >= 16) { socket->abort(); socket->deleteLater(); continue; }
        ++m_connections;
        auto buffer = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        auto* timer = new QTimer(socket); timer->setSingleShot(true); timer->start(5000);
        connect(timer, &QTimer::timeout, socket, [socket] { socket->abort(); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { --m_connections; socket->deleteLater(); });
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket, buffer, handled] {
            if (*handled) return;
            *buffer += socket->readAll();
            const int size = frameSize(*buffer);
            if (size == 0 || buffer->size() > MaxFrame + 4) { socket->abort(); return; }
            if (size < 0 || buffer->size() < size + 4) return;
            *handled = true;
            const auto receipt = receive(QJsonDocument::fromJson(buffer->mid(4, size)).object());
            socket->write(frame(receipt));
            socket->disconnectFromServer();
        });
    }
}

void LocalInstanceEndpoint::prune() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_requests.begin(); it != m_requests.end();)
        if (it->expires && it->expires <= now) it = m_requests.erase(it); else ++it;
    for (auto it = m_batches.begin(); it != m_batches.end();)
        if (it->expires && it->expires <= now) it = m_batches.erase(it); else ++it;
}

QJsonObject LocalInstanceEndpoint::receive(const QJsonObject& message) {
    prune();
    const auto batchId = message.value("batchId").toString();
    auto receipt = QJsonObject{{"batchId", batchId}, {"accepted", false}};
    const auto reject = [&receipt](const QString& error) { receipt.insert("error", error); return receipt; };
    if (m_stopped || message.value("version").toInt() != 1 || QUuid(batchId).isNull() ||
        !message.value("requests").isArray()) return reject("Invalid or unavailable desktop request");
    const auto fingerprint = QCryptographicHash::hash(QJsonDocument(message).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    if (m_batches.contains(batchId))
        return m_batches[batchId].fingerprint == fingerprint ? m_batches[batchId].receipt : reject("Conflicting batch ID");
    const auto inputs = message.value("requests").toArray();
    if (inputs.size() > 32 || m_batches.size() >= 128) return reject("The desktop request queue is full");
    QVector<ExternalOpenRequest> added;
    QHash<QString, QByteArray> fingerprints;
    QStringList ids;
    for (const auto& input : inputs) {
        const auto object = input.toObject();
        const auto id = object.value("id").toString();
        if (QUuid(id).isNull()) return reject("Invalid request ID");
        ExternalOpenRequest parsed;
        QString error;
        if (object.value("invalid").toBool()) {
            const auto input = object.value("invalidInput").toString();
            if (input.toUtf8().size() > 16384) return reject("External input is too long");
            const bool valid = input.startsWith("gplatform:", Qt::CaseInsensitive)
                ? parseExternalUrl(QUrl(input, QUrl::StrictMode), parsed, error)
                : parseExternalFile(input, {}, parsed, error);
            if (valid) return reject("Conflicting external input representation");
            parsed.rejectedInput = input; parsed.inputError = error;
        } else if (!object.value("url").toString().isEmpty()) {
            QUrl link("gplatform://open"); QUrlQuery query;
            query.addQueryItem("file", QString::fromLatin1(QUrl::toPercentEncoding(object.value("url").toString())));
            query.addQueryItem("name", QString::fromLatin1(QUrl::toPercentEncoding(object.value("name").toString()))); link.setQuery(query);
            if (!parseExternalUrl(link, parsed, error)) return reject(error);
        } else {
            const auto file = object.value("file").toString();
            if (!QFileInfo(file).isAbsolute() || !parseExternalFile(file, {}, parsed, error))
                return reject("Invalid absolute file path");
        }
        parsed.id = id;
        const auto hash = parsed.fingerprint();
        if (fingerprints.contains(id) && fingerprints[id] != hash) return reject("Conflicting request ID");
        if (m_requests.contains(id) && m_requests[id].fingerprint != hash) return reject("Conflicting request ID");
        if (!m_requests.contains(id) && !fingerprints.contains(id)) added << parsed;
        fingerprints.insert(id, hash); ids << id;
    }
    if (m_requests.size() + added.size() > 128) return reject("The desktop receipt cache is full");
    QString error;
    if (!m_receiver || !m_receiver(added, error)) return reject(error);
    for (const auto& r : added) m_requests.insert(r.id, {r.fingerprint(), 0});
    receipt.insert("accepted", true);
    bool finished = true;
    for (const auto& id : ids) if (m_requests[id].expires == 0) finished = false;
    m_batches.insert(batchId, {fingerprint, ids, receipt,
        finished ? QDateTime::currentMSecsSinceEpoch() + Retention : 0});
    emit activationRequested();
    return receipt;
}

void LocalInstanceEndpoint::markFinished(const QString& id) {
    if (!m_requests.contains(id)) return;
    m_requests[id].expires = QDateTime::currentMSecsSinceEpoch() + Retention;
    for (auto it = m_batches.begin(); it != m_batches.end(); ++it) {
        bool finished = true;
        for (const auto& requestId : it->ids)
            if (m_requests.contains(requestId) && !m_requests[requestId].expires) finished = false;
        if (finished && !it->expires) it->expires = QDateTime::currentMSecsSinceEpoch() + Retention;
    }
}

void LocalInstanceEndpoint::stop() {
    m_stopped = true;
    if (m_server) m_server->close();
    if (m_lock && m_lock->isLocked()) m_lock->unlock();
}
