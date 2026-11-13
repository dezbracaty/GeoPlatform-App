#pragma once
#include <QStringList>
#include <QUrl>
#include <QVector>

struct ExternalOpenRequest {
    QString id;
    QString localPath;
    QUrl downloadUrl;
    QString downloadFileName;
    QString rejectedInput;
    QString inputError;
    bool remote() const { return !downloadUrl.isEmpty(); }
    QByteArray fingerprint() const;
};

struct StartupInputs {
    bool dedicatedJournal{false};
    QVector<ExternalOpenRequest> requests;
    QStringList restartArguments; // Excludes executable and one-shot external inputs.
    QStringList errors;
};

bool parseExternalUrl(const QUrl& url, ExternalOpenRequest& request, QString& error);
bool parseExternalFile(const QString& input, const QString& workingDirectory,
                       ExternalOpenRequest& request, QString& error);
StartupInputs parseStartupInputs(const QStringList& arguments, const QString& workingDirectory);
