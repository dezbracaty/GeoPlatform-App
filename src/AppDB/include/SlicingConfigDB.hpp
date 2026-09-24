#pragma once

#include "AutoRegisterDB.hpp"
#include <Geometry.hpp>

#include <QColor>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace GPlatform {

/**
 * 切片配置数据库
 *
 * 存储所有切片相关的配置参数，支持 undo/redo
 */
class SlicingConfigDB : public AutoRegisterDB {
public:
    struct Filament {
        std::size_t index{0};
        std::string presetId;
        std::string presetName;
        std::string vendor;
        std::string materialType;
        QColor color;
        double diameterMm{1.75};

        bool operator==(const Filament& other) const {
            return index == other.index && presetId == other.presetId &&
                presetName == other.presetName && vendor == other.vendor &&
                materialType == other.materialType && color == other.color &&
                diameterMm == other.diameterMm;
        }
    };

    SlicingConfigDB();
    virtual ~SlicingConfigDB() = default;

    // libslicer 活动配置的强类型耗材投影。表面颜色标签按一基槽位索引。
    FIELD_VALUE(SlicingConfigDB, std::vector<Filament>, Filaments)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, std::uint64_t, ActiveRevision)
    // One-based slot used when a model part has no explicit filament binding.
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, DefaultFilamentSlot)

    // ==================== 基础参数 ====================
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, LayerHeight)           // 层高 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, FirstLayerHeight)      // 首层层高 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, BottomLayers)            // 底部实心层数
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, TopLayers)               // 顶部实心层数

    // ==================== 周长参数 ====================
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, PerimetersCount)         // 周长层数
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, ExternalPerimeterWidth) // 外周长线宽 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, PerimeterWidth)        // 内周长线宽 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, bool, ExternalPerimetersFirst) // 外周长优先

    // ==================== 填充参数 ====================
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, InfillDensity)         // 填充密度 (0.0-1.0)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, InfillPattern)           // 填充模式 (enum)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, InfillAngle)           // 填充角度 (度)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, InfillLineWidth)       // 填充线宽 (mm)

    // ==================== 支撑参数 ====================
    FIELD_VALUE_SIMPLE(SlicingConfigDB, bool, GenerateSupport)        // 是否生成支撑
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, SupportOverhangAngle)  // 悬垂角度阈值 (度)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, int, SupportPattern)          // 支撑模式 (enum)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, SupportDensity)        // 支撑密度 (0.0-1.0)

    // ==================== 高级参数 ====================
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, XYSizeCompensation)    // XY 尺寸补偿 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, ElephantFootCompensation) // 象脚补偿 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, ClosingRadius)         // 形态学闭运算半径 (mm)
    FIELD_VALUE_SIMPLE(SlicingConfigDB, float, Resolution)            // 轮廓简化精度 (mm)

    // TypeID
    TypeID getTypeID() const override {
        return TypeID::SLICING_CONFIG_DB;
    }

    // 创建默认配置
    static std::shared_ptr<SlicingConfigDB> createDefault();

    /** Renderer-independent projection of the active filament colors. */
    std::vector<Color> filamentColors() const;

    PropertyMap serialize() const override;
    bool deserialize(const PropertyMap& properties) override;

protected:
    void initializeProperties() override;
};

} // namespace GPlatform
