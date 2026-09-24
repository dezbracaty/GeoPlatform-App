#pragma once

#include <QRegularExpression>
#include <QString>
#include <QUrl>

namespace GPlatform::Printer {

// User-input convenience only. The adapter must still validate the result.
inline QString normalizeMoonrakerEndpointInput(const QString& value) {
    const QString input = value.trimmed();
    if (input.isEmpty() || input.startsWith("//") || input.contains("://"))
        return input;

    const QUrl parsed(input, QUrl::StrictMode);
    if (!parsed.scheme().isEmpty()) {
        // QUrl interprets printer.local:7125 as a scheme. Only reinterpret
        // this unambiguous host:numeric-port form, never an explicit scheme.
        static const QRegularExpression hostAndPort(
            QStringLiteral("^[^/?#:@\\s]+:[0-9]+(?:/|$)"));
        if (!hostAndPort.match(input).hasMatch())
            return input;
    }
    return QStringLiteral("http://") + input;
}

} // namespace GPlatform::Printer
