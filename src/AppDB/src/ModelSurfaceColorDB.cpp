#include "ModelSurfaceColorDB.hpp"

#include <QColor>
Color ModelSurfaceColorDB::colorForLabel(
    std::uint32_t label,
    const std::vector<Color>& palette,
    const Color& baseColor) {
    if (label == 0) return baseColor;
    const std::size_t paletteIndex = static_cast<std::size_t>(label - 1u);
    if (paletteIndex < palette.size()) return palette[paletteIndex];

    const QColor fallback = QColor::fromHsv(
        static_cast<int>((label * 137u) % 360u), 190, 235);
    return {
        fallback.redF(), fallback.greenF(), fallback.blueF(),
        fallback.alphaF()};
}
