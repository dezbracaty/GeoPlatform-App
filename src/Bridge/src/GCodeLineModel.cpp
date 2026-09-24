#include "GCodeLineModel.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

GCodeLineModel::GCodeLineModel(QObject* parent) : QAbstractListModel(parent) {
}
int GCodeLineModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !m_source || !m_source->error.isEmpty() ? 0 : int(m_source->lineStarts.size());
}
QHash<int, QByteArray> GCodeLineModel::roleNames() const {
    return {
        {LineNumberRole, "lineNumber" },
        {TextRole,       "codeText"   },
        {HighlightRole,  "highlighted"}
    };
}
QVariant GCodeLineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    if (role == LineNumberRole)
        return index.row() + 1;
    if (role == TextRole || role == Qt::DisplayRole)
        return lineText(index.row());
    if (role == HighlightRole)
        return index.row() + 1 == m_line;
    return {};
}
QString GCodeLineModel::lineText(int row) const {
    constexpr int blockSize = 128;
    const int block = row / blockSize;
    if (const auto* cached = m_blocks.object(block))
        return cached->value(row % blockSize);
    QFile file(m_source->file->fileName());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const int begin = block * blockSize, end = std::min(begin + blockSize, rowCount());
    const qint64 offset = m_source->lineStarts[begin];
    const qint64 limit = end == rowCount() ? m_source->byteSize : m_source->lineStarts[end];
    if (!file.seek(offset))
        return {};
    const auto bytes = file.read(limit - offset);
    if (bytes.size() != limit - offset)
        return {};
    auto lines = std::make_unique<QStringList>();
    for (int i = begin; i < end; ++i) {
        const auto first = m_source->lineStarts[i] - offset;
        const auto last = (i + 1 == rowCount() ? m_source->byteSize : m_source->lineStarts[i + 1]) - offset;
        auto text = QString::fromUtf8(bytes.constData() + first, last - first);
        if (text.endsWith('\n'))
            text.chop(1);
        if (text.endsWith('\r'))
            text.chop(1);
        lines->push_back(text);
    }
    const auto text = lines->value(row % blockSize);
    m_blocks.insert(block, lines.release());
    return text;
}
void GCodeLineModel::setPreview(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview) {
    if (preview && m_preview.lock() == preview && m_generation == preview->getPreviewDataGeneration())
        return;
    ++m_request;
    m_preview = preview;
    m_generation = preview ? preview->getPreviewDataGeneration() : 0;
    setPinned(false);
    m_line = 0;
    m_description.clear();
    emit inspectionChanged();
    install({});
    if (!preview || preview->getPrintGCodePath().empty()) {
        m_status = tr("This preview has no G-code source.");
        emit statusChanged();
        return;
    }
    if (auto source = preview->gcodeSource()) {
        install(source);
        return;
    }
    m_loading = true;
    m_status = tr("Indexing G-code…");
    emit statusChanged();
    const auto request = m_request, generation = m_generation;
    const auto path = QString::fromStdString(preview->getPrintGCodePath());
    const auto size = preview->sourceByteSize(), modified = preview->sourceModified();
    auto* watcher = new QFutureWatcher<std::shared_ptr<const GPlatform::GCodeSource>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request, generation, weak = std::weak_ptr(preview)] {
        const auto source = watcher->result();
        watcher->deleteLater();
        const auto preview = weak.lock();
        if (request != m_request)
            return;
        if (!preview || preview->getPreviewDataGeneration() != generation) {
            setPreview(preview);
            return;
        }
        preview->setGCodeSource(generation, source);
        install(source);
    });
    watcher->setFuture(QtConcurrent::run([path, size, modified] {
        return GPlatform::GCodeSource::capture(path, size, modified);
    }));
}
void GCodeLineModel::install(std::shared_ptr<const GPlatform::GCodeSource> source) {
    beginResetModel();
    m_source = std::move(source);
    m_blocks.clear();
    endResetModel();
    m_loading = false;
    m_status = m_source ? m_source->error : QString{};
    if (m_source && m_status.isEmpty())
        m_status = tr("%1 lines").arg(rowCount());
    if (m_line > rowCount() && !m_loading)
        m_line = 0;
    emit statusChanged();
    emit inspectionChanged();
}
void GCodeLineModel::inspect(std::uint32_t line, const QString& description) {
    if (m_pinned)
        return;
    applyInspection(line, description);
}
void GCodeLineModel::navigate(std::uint32_t line, const QString& description) {
    applyInspection(line, description);
    emit navigationRequested();
}
void GCodeLineModel::applyInspection(std::uint32_t line, const QString& description) {
    if (!m_loading && line > std::uint32_t(rowCount())) {
        line = 0;
        m_status = tr("Source line is unavailable; reload the preview.");
        emit statusChanged();
    }
    if (m_line == int(line) && m_description == description)
        return;
    const int old = m_line;
    m_line = int(line);
    m_description = description;
    if (old > 0 && old <= rowCount())
        emit dataChanged(index(old - 1), index(old - 1), {HighlightRole});
    if (m_line > 0 && m_line <= rowCount())
        emit dataChanged(index(m_line - 1), index(m_line - 1), {HighlightRole});
    emit inspectionChanged();
}
void GCodeLineModel::clearInspection() {
    if (!m_pinned)
        inspect(0, {});
}
void GCodeLineModel::setPinned(bool value) {
    if (m_pinned == value)
        return;
    m_pinned = value;
    emit pinnedChanged();
}
void GCodeLineModel::copyHighlightedLine() {
    if (m_line > 0 && m_line <= rowCount() && QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(lineText(m_line - 1));
}
