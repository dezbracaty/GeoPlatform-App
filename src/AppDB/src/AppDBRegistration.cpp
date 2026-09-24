#include "AppDBRegistration.hpp"

#include "BaseID.hpp"
#include "SlicingConfigDB.hpp"
#include "ToolpathPreviewTypes.hpp"
#include "Transform.hpp"

#include <Geometry.hpp>

#include <QColor>
#include <QRgba64>

#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cereal {

template <class Archive>
void serialize(Archive& archive, Vector2& value)
{
    archive(value.x, value.y);
}

template <class Archive>
void serialize(Archive& archive, Vector3& value)
{
    archive(value.x, value.y, value.z);
}

template <class Archive>
void serialize(Archive& archive, Vector4& value)
{
    archive(value.x, value.y, value.z, value.w);
}

template <class Archive>
void serialize(Archive& archive, Color& value)
{
    archive(value.r, value.g, value.b, value.a);
}

template <class Archive>
void save(Archive& archive, const Transform& value)
{
    std::array<float, 16> elements{};
    const Transform::Matrix4& matrix = value.getMatrix();
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            elements[row * 4 + column] = matrix(
                static_cast<Eigen::Index>(row),
                static_cast<Eigen::Index>(column));
        }
    }
    archive(elements);
}

template <class Archive>
void load(Archive& archive, Transform& value)
{
    std::array<float, 16> elements{};
    archive(elements);

    Transform::Matrix4 matrix;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            matrix(static_cast<Eigen::Index>(row),
                   static_cast<Eigen::Index>(column)) =
                elements[row * 4 + column];
        }
    }
    value.setMatrix(matrix);
}

template <class Archive>
void save(Archive& archive, const DBInstanceID& value)
{
    const std::int64_t rawValue = value.getValue();
    archive(rawValue);
}

template <class Archive>
void load(Archive& archive, DBInstanceID& value)
{
    std::int64_t rawValue = 0;
    archive(rawValue);
    value = DBInstanceID(rawValue);
}

template <class Archive>
void save(Archive& archive,
          const GPlatform::SlicingConfigDB::Filament& filament)
{
    const std::uint64_t index = static_cast<std::uint64_t>(filament.index);
    const bool colorValid = filament.color.isValid();
    const QRgba64 rgba = colorValid ? filament.color.rgba64() : QRgba64{};
    const std::uint16_t red = rgba.red();
    const std::uint16_t green = rgba.green();
    const std::uint16_t blue = rgba.blue();
    const std::uint16_t alpha = rgba.alpha();
    archive(index,
            filament.presetId,
            filament.presetName,
            filament.vendor,
            filament.materialType,
            colorValid,
            red,
            green,
            blue,
            alpha,
            filament.diameterMm);
}

template <class Archive>
void load(Archive& archive, GPlatform::SlicingConfigDB::Filament& filament)
{
    std::uint64_t index = 0;
    bool colorValid = false;
    std::uint16_t red = 0;
    std::uint16_t green = 0;
    std::uint16_t blue = 0;
    std::uint16_t alpha = 0;
    archive(index,
            filament.presetId,
            filament.presetName,
            filament.vendor,
            filament.materialType,
            colorValid,
            red,
            green,
            blue,
            alpha,
            filament.diameterMm);
    if (index > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Slicing filament index is out of range");
    }
    filament.index = static_cast<std::size_t>(index);
    filament.color = colorValid
        ? QColor::fromRgba64(QRgba64::fromRgba64(red, green, blue, alpha))
        : QColor{};
}

} // namespace cereal

namespace GPlatform::AppDB {

ModuleRegistrationList& moduleRegistrations()
{
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule()
{
    moduleRegistrations().execute();
}

} // namespace GPlatform::AppDB

REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.vector2", Vector2)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.vector3", Vector3)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.vector4", Vector4)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.color", Color)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.polygon2", Polygon2)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.transform", Transform)
REGISTER_APPDB_FIELD_VALUE_CODEC("gplatform.db-instance-id", DBInstanceID)
REGISTER_APPDB_FIELD_VALUE_CODEC(
    "gplatform.toolpath-preview.color-mode",
    GPlatform::ToolpathPreviewColorMode)
REGISTER_APPDB_FIELD_VALUE_CODEC(
    "gplatform.toolpath-preview.source-scope",
    GPlatform::ToolpathPreviewSourceScope)
REGISTER_APPDB_FIELD_VALUE_CODEC(
    "gplatform.slicing-config.filaments",
    std::vector<GPlatform::SlicingConfigDB::Filament>)
