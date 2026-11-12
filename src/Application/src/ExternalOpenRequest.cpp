#include "ExternalOpenRequest.hpp"
#include <ImportFileFormat.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>

namespace {
bool safeName(const QString& name) {
    static const QRegularExpression bad(R"([\x00-\x1f\x7f/\\:<>"|?*])");
    static const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))",
                                             QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && name.toUtf8().size() <= 255 &&
        !name.endsWith('.') && !name.endsWith(' ') && !bad.match(name).hasMatch() &&
        !reserved.match(name).hasMatch() && classifyImportFile(name) != ImportFileFormat::Unsupported;
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

QByteArray ExternalOpenRequest::fingerprint() const {
    const auto bytes = QJsonDocument(QJsonArray{localPath, downloadUrl.toString(QUrl::FullyEncoded),
                                                downloadFileName, rejectedInput}).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool parseExternalUrl(const QUrl& url, ExternalOpenRequest& request, QString& error) {
    request = {};
    const auto fail = [&error](const char* message) { error = QString::fromLatin1(message); return false; };
    if (!url.isValid() || url.toEncoded().size() > 16384 || url.scheme() != "gplatform" ||
        url.host() != "open" || !url.userInfo().isEmpty() || url.port() != -1 ||
        (!url.path().isEmpty() && url.path() != "/") || url.hasFragment())
        return fail("Invalid GPlatform open link");
    const auto items = QUrlQuery(url).queryItems(QUrl::FullyDecoded);
    QSet<QString> seen;
    QString source, name;
    for (const auto& item : items) {
        if ((item.first != "file" && item.first != "name") || seen.contains(item.first))
            return fail("Unsupported or duplicate open-link parameter");
        seen.insert(item.first);
        if (item.first == "file") source = item.second;
        else name = item.second;
    }
    const QUrl download(source, QUrl::StrictMode);
    if (source.isEmpty() || !download.isValid() || download.scheme() != "https" ||
        download.host().isEmpty() || !download.userInfo().isEmpty() || download.hasFragment())
        return fail("The model download address must be HTTPS");
    const auto pathName = QFileInfo(download.path()).fileName();
    const auto pathFormat = classifyImportFile(pathName);
    if (name.isEmpty()) name = pathName;
    if (!safeName(name)) return fail("Provide a plain STL, 3MF or G-code filename");
    if (pathFormat != ImportFileFormat::Unsupported && classifyImportFile(name) != pathFormat)
        return fail("The download filename and URL have conflicting formats");
    // An opaque endpoint needs an explicit filename. Never infer a mesh format.
    if (pathFormat == ImportFileFormat::Unsupported && !seen.contains("name"))
        return fail("This download address requires an explicit filename");
    if (pathFormat == ImportFileFormat::Unsupported && !QFileInfo(pathName).suffix().isEmpty())
        return fail("Unsupported model format in the download address");
    request = {newId(), {}, download, name};
    return true;
}

bool parseExternalFile(const QString& input, const QString& workingDirectory,
                       ExternalOpenRequest& request, QString& error) {
    request = {};
    if (input.isEmpty() || input.toUtf8().size() > 16384 || input.contains(QChar(0))) {
        error = "Invalid file path";
        return false;
    }
    const QUrl url(input, QUrl::StrictMode);
    const QString path = url.isLocalFile() ? url.toLocalFile() : input;
    if (!url.scheme().isEmpty() && !url.isLocalFile() && !QFileInfo(input).isAbsolute()) {
        error = "Only local files or GPlatform open links are accepted";
        return false;
    }
    const QString absolute = QDir::cleanPath(QDir(workingDirectory).absoluteFilePath(path));
    if (classifyImportFile(absolute) == ImportFileFormat::Unsupported) {
        error = "Supported files: STL, 3MF and G-code";
        return false;
    }
    request = {newId(), absolute, {}, {}};
    return true;
}

StartupInputs parseStartupInputs(const QStringList& arguments, const QString& workingDirectory) {
    StartupInputs result;
    const QSet<QString> valued{"--ghost-port", "--journal-replay-id", "--ghost-file"};
    const QSet<QString> dedicated{"--ghost-play", "--ghost-record", "--ghost-verify"};
    bool filesOnly = false;
    for (int i = 1; i < arguments.size(); ++i) {
        const auto value = arguments[i];
        if (!filesOnly && valued.contains(value)) {
            result.dedicatedJournal = true;
            result.restartArguments << value;
            if (i + 1 < arguments.size()) result.restartArguments << arguments[++i];
        } else if (!filesOnly && dedicated.contains(value)) {
            result.dedicatedJournal = true;
            result.restartArguments << value;
        } else if (!filesOnly && value == "--restart") {
            result.restartArguments << value;
        } else if (!filesOnly && value == "--") {
            filesOnly = true;
        } else if (!filesOnly && value.startsWith('-')) {
            result.errors << "Unsupported startup option: " + value;
        } else {
            ExternalOpenRequest request;
            QString error;
            const bool ok = value.startsWith("gplatform:", Qt::CaseInsensitive)
                ? parseExternalUrl(QUrl(value, QUrl::StrictMode), request, error)
                : parseExternalFile(value, workingDirectory, request, error);
            if (!ok) {
                request.id = newId();
                request.rejectedInput = value;
                request.inputError = error;
            }
            result.requests << request;
        }
    }
    if (result.dedicatedJournal && !result.requests.isEmpty())
        result.errors << "Journal instances do not accept external files or links";
    return result;
}
