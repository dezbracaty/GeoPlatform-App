#pragma once

#include <QCursor>
#include <QString>

/** Owns the application override cursor for a surface-painting tool session. */
class SurfaceBrushCursor final {
public:
    enum class Shape {
        Circle,
        Square,
        Triangle,
        Sphere
    };

    ~SurfaceBrushCursor();

    void applyBrush(int radiusPx, Shape shape);
    void applyTool(Qt::CursorShape shape);
    void clear();

    bool isApplied() const { return m_applied; }
    int radiusPx() const { return m_radiusPx; }
    Shape shape() const { return m_brushShape; }

    static Shape shapeFromName(const QString& name);
    static int radiusPxForSize(double brushSize);

private:
    static QCursor buildBrushCursor(int radiusPx, Shape shape);

    bool m_applied{false};
    Qt::CursorShape m_cursorShape{Qt::ArrowCursor};
    int m_radiusPx{-1};
    Shape m_brushShape{Shape::Circle};
};
