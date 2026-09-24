#include "../include/PrintBedTemplateDB.hpp"

void PrintBedTemplateDB::initializeProperties() {
    setModelId({});
    setVariantId({});
    setVendor({});
    setPrinterModel({});
    setWidth(PrintBedDefaults::DEFAULT_WIDTH);
    setHeight(PrintBedDefaults::DEFAULT_HEIGHT);
    setThickness(PrintBedDefaults::PLATFORM_THICKNESS);
    setPrintHeight(PrintBedDefaults::DEFAULT_PRINT_HEIGHT);
    setOrigin(Vector3(0.0f, 0.0f, 0.0f));
    setPrintableArea({Vector2(0.0f, 0.0f),
                      Vector2(getWidth(), 0.0f),
                      Vector2(getWidth(), getHeight()),
                      Vector2(0.0f, getHeight())});
    setBedModelPath({});
    setBedTexturePath({});
    setMaxTemperature(100.0f);
    setHeated(true);
}

std::shared_ptr<AutoRegisterDB> PrintBedTemplateDB::clone() const {
    auto copy = trans::TransDB::create<PrintBedTemplateDB>();
    copy->setModelId(getModelId());
    copy->setVariantId(getVariantId());
    copy->setVendor(getVendor());
    copy->setPrinterModel(getPrinterModel());
    copy->setWidth(getWidth());
    copy->setHeight(getHeight());
    copy->setThickness(getThickness());
    copy->setPrintHeight(getPrintHeight());
    copy->setPrintableArea(getPrintableArea());
    copy->setOrigin(getOrigin());
    copy->setBedModelPath(getBedModelPath());
    copy->setBedTexturePath(getBedTexturePath());
    copy->setMaxTemperature(getMaxTemperature());
    copy->setHeated(getHeated());
    return copy;
}
