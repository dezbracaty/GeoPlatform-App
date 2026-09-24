#pragma once

namespace ModelWidgetRelations {

inline constexpr const char* TranslateWidgetRelation = "ActorModelTranslateWidget";
inline constexpr const char* OrientationWidgetRelation = "ActorModelOrientationWidget";
inline constexpr const char* ScaleWidgetRelation = "ActorModelScaleWidget";
inline constexpr const char* SelectionBoxRelation = "ActorSelectionBoxWidget";

void registerRelations();

} // namespace ModelWidgetRelations
