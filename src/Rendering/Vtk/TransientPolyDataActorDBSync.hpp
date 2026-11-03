#pragma once

#include "ActorDBSync.hpp"

/** Render-thread projection of a runtime-only TransientPolyDataActorDB. */
class TransientPolyDataActorDBSync : public ActorDBSync {
protected:
    bool isGeometryProperty(std::string_view prop) const override {
        return prop == "geometry" || prop == "triangles" || prop == "points";
    }
    void createActor() override;
    void updateGeometry(const std::string& prop) override;
    void onMaterialChanged(const std::string& prop) override;
    void onRenderingContextApplied(const RenderingContext& context) override;
};
