#include "GettextBridge.h"

#include "Tr.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>

#include <cstddef>
#include <cstring>
#include <string>

QString GettextBridge::translate(const char* context, const char* sourceText,
                                 const char* /*disambiguation*/, int /*n*/) const {
    if (sourceText == nullptr) {
        return QString();
    }
    const char* domain = context != nullptr && std::strcmp(context, "LibSlicerSettings") == 0
                       ? "libslicer" : "";
    std::string out;
    if (tr::Translator::instance().find(sourceText, out, domain)) {
        return QString::fromStdString(out);
    }
    return QString(); // miss -> Qt falls back to the source text
}

namespace gettext_bridge {

bool loadCatalogFromResource(const QString& moResourcePath, const char* domain) {
    QFile moFile(moResourcePath);
    if (!moFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = moFile.readAll();
    return tr::Translator::instance().loadCatalogFromBuffer(bytes.constData(),
                                                            static_cast<std::size_t>(bytes.size()),
                                                            domain != nullptr ? domain : "");
}

void install() {
    static GettextBridge* bridge = nullptr;
    if (bridge == nullptr) {
        bridge = new GettextBridge(QCoreApplication::instance());
        QCoreApplication::installTranslator(bridge);
    }
}

} // namespace gettext_bridge
