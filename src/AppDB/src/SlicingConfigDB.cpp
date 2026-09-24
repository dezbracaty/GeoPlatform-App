#include "SlicingConfigDB.hpp"

#include <QVariantList>
#include <QVariantMap>

namespace GPlatform {
namespace {

QVariantMap serializeFilament(const SlicingConfigDB::Filament& filament) {
    return {
        {QStringLiteral("index"), static_cast<qulonglong>(filament.index)},
        {QStringLiteral("presetId"), QString::fromStdString(filament.presetId)},
        {QStringLiteral("presetName"), QString::fromStdString(filament.presetName)},
        {QStringLiteral("vendor"), QString::fromStdString(filament.vendor)},
        {QStringLiteral("materialType"), QString::fromStdString(filament.materialType)},
        {QStringLiteral("color"), filament.color.name(QColor::HexArgb)},
        {QStringLiteral("diameterMm"), filament.diameterMm}
    };
}

SlicingConfigDB::Filament deserializeFilament(const QVariantMap& value) {
    return {
        static_cast<std::size_t>(value.value(QStringLiteral("index")).toULongLong()),
        value.value(QStringLiteral("presetId")).toString().toStdString(),
        value.value(QStringLiteral("presetName")).toString().toStdString(),
        value.value(QStringLiteral("vendor")).toString().toStdString(),
        value.value(QStringLiteral("materialType")).toString().toStdString(),
        QColor(value.value(QStringLiteral("color")).toString()),
        value.value(QStringLiteral("diameterMm"), 1.75).toDouble()
    };
}

} // namespace

SlicingConfigDB::SlicingConfigDB()
    : AutoRegisterDB()
{
    // 构造函数保持简单
}

void SlicingConfigDB::initializeProperties() {
    // 设置默认值
    // 这个函数在 AutoRegisterDB::onCreated() 中被调用

    setFilaments({});
    setActiveRevision(0);
    setDefaultFilamentSlot(1);

    // 基础参数
    setLayerHeight(0.2f);           // 默认层高 0.2mm
    setFirstLayerHeight(0.2f);      // 首层层高 0.2mm
    setBottomLayers(3);             // 底部 3 层实心
    setTopLayers(3);                // 顶部 3 层实心

    // 周长参数
    setPerimetersCount(2);          // 2 层周长
    setExternalPerimeterWidth(0.45f); // 外周长 0.45mm
    setPerimeterWidth(0.45f);       // 内周长 0.45mm
    setExternalPerimetersFirst(false); // 内周长优先

    // 填充参数
    setInfillDensity(0.2f);         // 20% 填充
    setInfillPattern(0);            // 直线填充
    setInfillAngle(45.0f);          // 45度角
    setInfillLineWidth(0.4f);       // 填充线宽 0.4mm

    // 支撑参数
    setGenerateSupport(false);      // 默认不生成支撑
    setSupportOverhangAngle(45.0f); // 45度悬垂阈值
    setSupportPattern(0);           // 直线支撑
    setSupportDensity(0.2f);        // 20% 支撑密度

    // 高级参数
    setXYSizeCompensation(0.0f);    // 无尺寸补偿
    setElephantFootCompensation(0.0f); // 无象脚补偿
    setClosingRadius(0.0f);         // 无形态学闭运算
    setResolution(0.0025f);         // 2.5μm 精度
}

std::shared_ptr<SlicingConfigDB> SlicingConfigDB::createDefault() {
    return trans::TransDB::create<SlicingConfigDB>();
}

std::vector<Color> SlicingConfigDB::filamentColors() const {
    std::vector<Color> result;
    result.reserve(getFilaments().size());
    for (const auto& filament : getFilaments()) {
        if (!filament.color.isValid()) continue;
        result.emplace_back(
            filament.color.redF(), filament.color.greenF(),
            filament.color.blueF(), filament.color.alphaF());
    }
    return result;
}

PropertyMap SlicingConfigDB::serialize() const {
    PropertyMap properties = AutoRegisterDB::serialize();
    QVariantList filaments;
    for (const auto& filament : getFilaments()) {
        filaments.push_back(serializeFilament(filament));
    }
    properties[QStringLiteral("Filaments")] = filaments;
    properties[QStringLiteral("ActiveRevision")] =
        QVariant::fromValue<qulonglong>(getActiveRevision());
    return properties;
}

bool SlicingConfigDB::deserialize(const PropertyMap& properties) {
    PropertyMap baseProperties = properties;
    baseProperties.remove(QStringLiteral("Filaments"));
    baseProperties.remove(QStringLiteral("ActiveRevision"));
    if (!AutoRegisterDB::deserialize(baseProperties)) return false;

    std::vector<Filament> filaments;
    const QVariantList values = properties.value(QStringLiteral("Filaments")).toList();
    filaments.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant& value : values) {
        Filament filament = deserializeFilament(value.toMap());
        if (!filament.color.isValid()) return false;
        filaments.push_back(std::move(filament));
    }
    setFilaments(filaments);
    setActiveRevision(
        properties.value(QStringLiteral("ActiveRevision")).toULongLong());
    return true;
}

} // namespace GPlatform
