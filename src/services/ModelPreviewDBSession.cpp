#include "ModelPreviewDBSession.h"

#include <GlbDB.hpp>

#include <QDir>
#include <QFileInfo>

ModelPreviewDBSession::~ModelPreviewDBSession() {
    // Preview lifecycle is explicitly non-transactional. Keep that policy in
    // the preview owner instead of teaching generic TempDBScope about undo.
    TransientUpdateGuard transientPreviewCleanup;
    m_scope.clear();
}

QString ModelPreviewDBSession::normalizedPath(const QString& filePath) {
    const QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty()
        ? QDir::cleanPath(info.absoluteFilePath())
        : canonical;
}

std::shared_ptr<GlbDB> ModelPreviewDBSession::publish(
    const QString& filePath,
    GlbImportResult result) {
    const QString path = normalizedPath(filePath);
    if (path.isEmpty() || !result.success || !result.asset) return {};

    if (m_glb) return m_glb;

    // Thumbnail DBs are implementation details of one preview job. They must
    // never enter a surrounding user transaction; generic TempDBScope remains
    // transaction-neutral for interactive temporary objects.
    TransientUpdateGuard transientPreviewPublish;
    auto glb = m_scope.create<GlbDB>(
        GlbDB::CreationOptions{false, false});
    if (!glb || glb->getVisible() || glb->isPickable()) return {};

    // A preview DB is only an input to the offscreen image generator. Keep it
    // outside every business scene for its complete, short lifetime.
    glb->setSourceFile(path.toStdString());
    glb->setFileFormat("GLB");
    glb->setDataSource(path.toStdString());
    glb->setDisplayName(QFileInfo(path).completeBaseName().toStdString());
    if (!glb->replaceImportedAsset(std::move(result))) {
        return {};
    }

    m_glb = glb;
    return m_glb;
}
