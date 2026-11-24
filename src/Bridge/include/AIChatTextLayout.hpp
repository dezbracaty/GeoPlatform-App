#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

// TextEdit/CopyableText has no QML line-height property. Apply only block
// geometry to its plain-text document; never parse or replace reply content.
class AIChatTextLayout : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
public:
    explicit AIChatTextLayout(QObject* parent = nullptr) : QObject(parent) {}
    QQuickTextDocument* document() const { return m_document; }
    qreal lineHeight() const { return m_lineHeight; }

    void setDocument(QQuickTextDocument* document) {
        if (m_document == document) return;
        if (m_document) disconnect(m_document->textDocument(), nullptr, this, nullptr);
        m_document = document;
        if (m_document)
            connect(m_document->textDocument(), &QTextDocument::contentsChanged,
                    this, &AIChatTextLayout::applyLineHeight);
        applyLineHeight();
        emit documentChanged();
    }
    void setLineHeight(qreal height) {
        if (qFuzzyCompare(m_lineHeight, height)) return;
        m_lineHeight = height;
        applyLineHeight();
        emit lineHeightChanged();
    }
signals:
    void documentChanged();
    void lineHeightChanged();
private:
    void applyLineHeight() {
        if (m_applying || !m_document || m_lineHeight <= 0) return;
        auto* doc = m_document->textDocument();
        m_applying = true;
        QTextCursor cursor(doc);
        cursor.beginEditBlock();
        for (auto block = doc->begin(); block.isValid(); block = block.next()) {
            auto format = block.blockFormat();
            if (format.lineHeightType() == QTextBlockFormat::FixedHeight
                    && qFuzzyCompare(format.lineHeight(), m_lineHeight)) continue;
            format.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }
        cursor.endEditBlock();
        m_applying = false;
    }
    QPointer<QQuickTextDocument> m_document;
    qreal m_lineHeight = 0;
    bool m_applying = false;
};
