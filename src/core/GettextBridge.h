#pragma once

// GettextBridge — the ~15-line Qt glue that routes Qt's tr()/qsTr() to the Qt-free Tr catalog.
//
// This is the ONLY place Qt touches translation. It is app-level glue in core/ (NOT a library): QML
// can only talk to Qt objects, so making qsTr() use Tr requires a QTranslator (a Qt class). Installing
// one makes every tr()/qsTr() look its source text up in tr::Translator, with zero QML/source changes.

#include <QString>
#include <QTranslator>

class GettextBridge : public QTranslator {
    Q_OBJECT
public:
    using QTranslator::QTranslator;

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return false; }
};

namespace gettext_bridge {

// Read a .mo from an embedded Qt resource and load it into a Tr domain. The
// resource reading is here (Qt) so Tr never touches Qt resources. Returns false if missing/invalid.
bool loadCatalogFromResource(const QString& moResourcePath, const char* domain = "");

// Install one process-wide GettextBridge (idempotent) so tr()/qsTr() route through Tr.
void install();

} // namespace gettext_bridge
