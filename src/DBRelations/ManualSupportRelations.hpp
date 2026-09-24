#pragma once

#include <BaseID.hpp>
#include <memory>

class ManualSupportDB;
namespace ManualSupportRelations {

inline constexpr const char* ManualSupportRelation = "ModelManualSupport";

void registerRelations();
std::shared_ptr<ManualSupportDB> findForModel(DBInstanceID modelId);

} // namespace ManualSupportRelations
