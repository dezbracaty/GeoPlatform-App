#include <DownloadManager.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#include <iostream>
#include <cstring>
#include <stdexcept>

class Reply final : public QNetworkReply {
public:
    QByteArray buffer;
    explicit Reply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent) {
        setRequest(request); setUrl(request.url()); open(ReadOnly | Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    }
    void abort() override { setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished(); }
    void data(const QByteArray& bytes) { buffer += bytes; emit readyRead(); }
    void complete() { setFinished(true); emit finished(); }
    void fail() { setError(SslHandshakeFailedError, "bad certificate"); complete(); }
    void redirect(const QUrl& target) { setAttribute(QNetworkRequest::RedirectionTargetAttribute, target); complete(); }
    qint64 bytesAvailable() const override { return buffer.size() + QNetworkReply::bytesAvailable(); }
    qint64 readData(char* data, qint64 maximum) override {
        const auto size = qMin(maximum, qint64(buffer.size()));
        if (!size) return -1;
        std::memcpy(data, buffer.constData(), size); buffer.remove(0, size); return size;
    }
};
class Network final : public QNetworkAccessManager {
public:
    QPointer<Reply> current;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        current = new Reply(request, this); return current;
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    try {
        Network network;
        const auto id = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
        DownloadItem item(id(), "test", "https://example.com/model.stl");
        item.setExternalFileName(QString(82, QChar(0x4e2d)) + ".stl");
        QSignalSpy done(&item, &DownloadItem::finished);
        item.start(&network);
        require(network.current && network.current->request().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy, "external redirect policy missing");
        require(!QFile::exists(item.filePath()), "partial file published");
        network.current->data("partial"); item.pause();
        auto old = network.current;
        item.resume(); item.start(&network);
        old->complete();
        require(item.status() == "downloading" && done.isEmpty(), "old attempt ended resumed download");
        network.current->data("complete model"); network.current->complete();
        require(done.size() == 1 && item.status() == "completed", "download completion missing");
        require(QFileInfo(item.filePath()).fileName().toUtf8().size() <= 255, "final filename too long");
        QFile file(item.filePath()); require(file.open(QIODevice::ReadOnly) && file.readAll() == "complete model", "partial bytes leaked to result"); file.close(); QFile::remove(item.filePath());
        item.cancel(); require(done.size() == 1, "completed item terminated twice");

        DownloadItem cancelled(id(), "cancel", "https://example.com/model.stl");
        cancelled.setExternalFileName("cancel.stl"); QSignalSpy cancelDone(&cancelled, &DownloadItem::finished);
        cancelled.start(&network); auto cancelledReply = network.current;
        cancelledReply->data("partial"); cancelled.cancel(); cancelledReply->complete();
        require(cancelled.status() == "cancelled" && cancelDone.size() == 1 && !QFile::exists(cancelled.filePath()), "cancel overwritten or partial published");

        DownloadItem redirect(id(), "redirect", "https://example.com/model.stl");
        redirect.setExternalFileName("redirect.stl"); QSignalSpy redirectDone(&redirect, &DownloadItem::finished);
        redirect.start(&network); network.current->redirect(QUrl("http://example.com/model.stl"));
        require(redirect.status() == "failed" && redirectDone.size() == 1, "HTTPS downgrade accepted");

        DownloadItem secure(id(), "ssl", "https://example.com/model.stl");
        secure.setExternalFileName("ssl.stl"); QSignalSpy secureDone(&secure, &DownloadItem::finished);
        secure.start(&network); network.current->fail();
        require(secure.status() == "failed" && secureDone.size() == 1, "SSL failure did not terminate");

        DownloadItem legacy(id(), "legacy", "http://example.com/test.stl");
        legacy.start(&network); network.current->data("legacy"); network.current->complete();
        require(legacy.status() == "completed", "legacy HTTP source contract changed"); QFile::remove(legacy.filePath());

        const auto base = "gplatform-readonly-" + id();
        const auto root = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/GPlatform/Models/";
        QStringList protectedFiles{root + base + ".stl"};
        const auto now = QDateTime::currentDateTime();
        for (int i = -2; i <= 2; ++i) protectedFiles << root + base + now.addSecs(i).toString("_yyyyMMdd_HHmmss") + ".stl";
        for (const auto& name : protectedFiles) {
            QFile f(name); require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), "cannot create isolated readonly fixture");
            f.write("preserve existing bytes"); f.close(); f.setPermissions(QFile::ReadOwner);
        }
        DownloadItem protectedItem(id(), base, "http://example.com/model.stl");
        QSignalSpy protectedDone(&protectedItem, &DownloadItem::finished);
        protectedItem.start(&network);
        require(protectedItem.status() == "failed" && protectedDone.size() == 1, "readonly open did not fail once");
        bool preserved = true;
        for (const auto& name : protectedFiles) {
            QFile f(name); preserved &= f.open(QIODevice::ReadOnly) && f.readAll() == "preserve existing bytes"; f.close();
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner); f.remove();
        }
        require(preserved, "failed open deleted an existing legacy file");
        std::cout << "PASS atomic external download, long filenames, generations, cancellation, TLS and legacy HTTP\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
