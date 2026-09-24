#pragma once
#include <QAbstractListModel>
#include <QCache>
#include <QStringList>
#include <ToolpathPreviewDB.hpp>

class GCodeLineModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int highlightedLine READ highlightedLine NOTIFY inspectionChanged)
    Q_PROPERTY(QString description READ description NOTIFY inspectionChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
public:
    explicit GCodeLineModel(QObject* parent = nullptr);
    enum Role { LineNumberRole = Qt::UserRole + 1,
                TextRole,
                HighlightRole };
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setPreview(std::shared_ptr<GPlatform::ToolpathPreviewDB> preview);
    void inspect(std::uint32_t line, const QString& description);
    void clearInspection();
    // Explicit layer/Step navigation takes precedence over hover and its pin.
    void navigate(std::uint32_t line, const QString& description);
    int highlightedLine() const {
        return m_line;
    }
    QString description() const {
        return m_description;
    }
    QString status() const {
        return m_status;
    }
    bool loading() const {
        return m_loading;
    }
    bool pinned() const {
        return m_pinned;
    }
    void setPinned(bool value);
    Q_INVOKABLE void copyHighlightedLine();
signals:
    void inspectionChanged();
    void navigationRequested();
    void statusChanged();
    void pinnedChanged();

private:
    void applyInspection(std::uint32_t line, const QString& description);
    void install(std::shared_ptr<const GPlatform::GCodeSource> source);
    QString lineText(int row) const;
    std::weak_ptr<GPlatform::ToolpathPreviewDB> m_preview;
    std::shared_ptr<const GPlatform::GCodeSource> m_source;
    mutable QCache<int, QStringList> m_blocks{16};
    std::uint64_t m_generation{0}, m_request{0};
    int m_line{0};
    QString m_description, m_status;
    bool m_loading{false}, m_pinned{false};
};
