#include "SurfaceBrushCursor.hpp"

#include <QGuiApplication>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QScreen>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int kMinBrushCursorRadiusPx = 4;
constexpr int kMaxBrushCursorRadiusPx = 256;
}

SurfaceBrushCursor::~SurfaceBrushCursor() {
    clear();
}

SurfaceBrushCursor::Shape SurfaceBrushCursor::shapeFromName(const QString& name) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("square") || normalized == QStringLiteral("rect") ||
        normalized == QStringLiteral("box")) {
        return Shape::Square;
    }
    if (normalized == QStringLiteral("triangle") || normalized == QStringLiteral("tri")) {
        return Shape::Triangle;
    }
    if (normalized == QStringLiteral("sphere")) {
        return Shape::Sphere;
    }
    return Shape::Circle;
}

int SurfaceBrushCursor::radiusPxForSize(double brushSize) {
    return std::clamp(static_cast<int>(std::round(brushSize * 4.0)), 6, 128);
}

QCursor SurfaceBrushCursor::buildBrushCursor(int radiusPx, Shape shape) {
    const int radius = std::clamp(radiusPx, kMinBrushCursorRadiusPx, kMaxBrushCursorRadiusPx);
    const int canvasSize = radius * 2 + 10;
    const int center = canvasSize / 2;

    qreal dpr = 1.0;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        dpr = std::max(1.0, screen->devicePixelRatio());
    }

    QPixmap pixmap(static_cast<int>(std::ceil(canvasSize * dpr)),
                   static_cast<int>(std::ceil(canvasSize * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    const QRectF brushRect(center - radius, center - radius, radius * 2, radius * 2);

    auto drawOutline = [&](const QPen& pen) {
        painter.setPen(pen);
        switch (shape) {
            case Shape::Square:
                painter.drawRect(brushRect);
                break;
            case Shape::Triangle: {
                QPolygonF triangle;
                triangle << QPointF(center, center - radius)
                         << QPointF(center + radius * 0.866, center + radius * 0.5)
                         << QPointF(center - radius * 0.866, center + radius * 0.5);
                painter.drawPolygon(triangle);
                break;
            }
            case Shape::Sphere:
            case Shape::Circle:
                painter.drawEllipse(brushRect);
                break;
        }
    };

    QPen outerPen(QColor(0, 0, 0, 220));
    outerPen.setWidthF(2.6);
    drawOutline(outerPen);
    QPen innerPen(QColor(255, 255, 255, 245));
    innerPen.setWidthF(1.2);
    drawOutline(innerPen);

    if (shape == Shape::Sphere) {
        const QRectF longitude(center - radius * 0.42, center - radius,
                               radius * 0.84, radius * 2.0);
        const QRectF latitude(center - radius, center - radius * 0.32,
                              radius * 2.0, radius * 0.64);
        painter.setPen(outerPen);
        painter.drawEllipse(longitude);
        painter.drawEllipse(latitude);
        painter.setPen(innerPen);
        painter.drawEllipse(longitude);
        painter.drawEllipse(latitude);
    }

    // A fixed-size center reticle makes the exact paint hotspot visible for
    // every brush. Draw an outlined, gapped cross so it stays legible over
    // both bright and dark model colors.
    const qreal armInner = 2.0;
    const qreal armOuter = 7.0;
    const std::array<QLineF, 4> reticle{
        QLineF(center - armOuter, center, center - armInner, center),
        QLineF(center + armInner, center, center + armOuter, center),
        QLineF(center, center - armOuter, center, center - armInner),
        QLineF(center, center + armInner, center, center + armOuter)};
    QPen reticleOuter(QColor(0, 0, 0, 235));
    reticleOuter.setWidthF(3.0);
    reticleOuter.setCapStyle(Qt::RoundCap);
    painter.setPen(reticleOuter);
    painter.drawLines(reticle.data(), static_cast<int>(reticle.size()));
    QPen reticleInner(QColor(255, 255, 255, 255));
    reticleInner.setWidthF(1.1);
    reticleInner.setCapStyle(Qt::RoundCap);
    painter.setPen(reticleInner);
    painter.drawLines(reticle.data(), static_cast<int>(reticle.size()));
    return QCursor(pixmap, center, center);
}

void SurfaceBrushCursor::applyBrush(int radiusPx, Shape shape) {
    const int radius = std::clamp(radiusPx, kMinBrushCursorRadiusPx, kMaxBrushCursorRadiusPx);
    if (m_applied && m_cursorShape == Qt::CrossCursor && m_radiusPx == radius &&
        m_brushShape == shape) {
        return;
    }
    const QCursor cursor = buildBrushCursor(radius, shape);
    if (m_applied) {
        QGuiApplication::changeOverrideCursor(cursor);
    } else {
        QGuiApplication::setOverrideCursor(cursor);
        m_applied = true;
    }
    m_cursorShape = Qt::CrossCursor;
    m_radiusPx = radius;
    m_brushShape = shape;
}

void SurfaceBrushCursor::applyTool(Qt::CursorShape shape) {
    if (m_applied && m_cursorShape == shape && m_radiusPx < 0) return;
    const QCursor cursor(shape);
    if (m_applied) {
        QGuiApplication::changeOverrideCursor(cursor);
    } else {
        QGuiApplication::setOverrideCursor(cursor);
        m_applied = true;
    }
    m_cursorShape = shape;
    m_radiusPx = -1;
}

void SurfaceBrushCursor::clear() {
    if (!m_applied) return;
    if (QGuiApplication::instance()) QGuiApplication::restoreOverrideCursor();
    m_applied = false;
    m_cursorShape = Qt::ArrowCursor;
    m_radiusPx = -1;
    m_brushShape = Shape::Circle;
}
