#include "GCodeLineModel.hpp"
#include <ToolpathPreviewUtils.hpp>
#include <DocumentManager.hpp>
#include <QPointer>
#include <QClipboard>
#include <QGuiApplication>

GCodeLineModel::GCodeLineModel(QObject* parent) : QAbstractListModel(parent) {
    const QPointer<GCodeLineModel> self(this);
    m_listener = DocumentManager::instance()->addChangeListener([self](const auto& change) {
        if (!self || change.dbType != TypeID::TOOLPATH_PREVIEW_DB) return;
        QMetaObject::invokeMethod(self, [self, id = change.id] {
            if (!self) return;
            const auto preview = self->m_preview.lock();
            if (!preview || preview->getDBInstanceID() != id) return;
            if (preview->getPreviewDataGeneration() != self->m_generation) self->setPreview(preview);
            else emit self->inspectionChanged(); // Also refresh pinned route actions after filtering.
        }, Qt::QueuedConnection);
    });
}
GCodeLineModel::~GCodeLineModel() {
    DocumentManager::instance()->removeChangeListener(m_listener);
    if (const auto preview = m_preview.lock()) preview->setInspectedElementIndex(-1);
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
    if (const auto previous = m_preview.lock()) previous->setInspectedElementIndex(-1);
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
    m_status = tr("This preview has no G-code source.");
    emit statusChanged();
}
void GCodeLineModel::install(std::shared_ptr<const GPlatform::GCodeSource> source) {
    beginResetModel();
    m_source = std::move(source);
    m_blocks.clear();
    endResetModel();
    m_status = m_source ? m_source->error : QString{};
    if (m_source && m_status.isEmpty())
        m_status = tr("%1 lines").arg(rowCount());
    if (m_line > rowCount())
        m_line = 0;
    emit statusChanged();
    emit inspectionChanged();
}
void GCodeLineModel::inspect(std::uint32_t line, const QString& description, int elementIndex) {
    if (m_pinned)
        return;
    applyInspection(line, description, elementIndex);
}
void GCodeLineModel::navigate(std::uint32_t line, const QString& description) {
    applyInspection(line, description);
    emit navigationRequested();
}
void GCodeLineModel::applyInspection(std::uint32_t line, const QString& description, int elementIndex) {
    if (line > std::uint32_t(rowCount())) {
        line = 0;
        if (m_source && m_source->available()) {
            m_status = tr("Source line is unavailable; reload the preview.");
            emit statusChanged();
        }
    }
    const auto preview = m_preview.lock();
    if (preview && preview->getPreviewDataGeneration() == m_generation)
        preview->setInspectedElementIndex(elementIndex);
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

bool GCodeLineModel::hasFiberRoute() const {
    const auto preview = m_preview.lock();
    return m_source && m_source->available() && preview && preview->getPreviewDataGeneration() == m_generation &&
        bool(GPlatform::ToolpathPreviewUtils::inspectedFiberRoute(*preview));
}

void GCodeLineModel::navigateFiberEvent(bool depleted) {
    const auto preview = m_preview.lock();
    if (!m_source || !m_source->available() || !preview || preview->getPreviewDataGeneration() != m_generation) return;
    const auto route = GPlatform::ToolpathPreviewUtils::inspectedFiberRoute(*preview);
    if (!route) return;
    const auto kind = depleted ? GPlatform::ToolpathEventKind::FiberDepleted : GPlatform::ToolpathEventKind::FiberCut;
    const auto& elements = preview->inspectionElements();
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];
        if (element.kind != GPlatform::ToolpathPreviewDB::InspectionElement::Kind::Event || !preview->inspectionElementVisible(i)) continue;
        const auto& event = preview->getOptions()[element.index];
        if (event.kind != kind || event.fiberRoute != route) continue;
        applyInspection(event.sourceCommandId, GPlatform::ToolpathPreviewUtils::fiberEventDescription(*preview, event), int(i));
        emit navigationRequested();
        return;
    }
}
