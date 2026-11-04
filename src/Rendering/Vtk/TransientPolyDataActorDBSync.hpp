#pragma once

#include "ActorDBSync.hpp"

/** Render-thread projection of a runtime-only TransientPolyDataActorDB. */
class TransientPolyDataActorDBSync : public ActorDBSync {
public:
    RenderLayer getDefaultRenderLayer() const override {
        return m_drawOnTop ? RenderLayer::OVERLAY : RenderLayer::GEOMETRY;
    }
protected:
    void createActor() override;
    void updateGeometry(const std::string& prop) override;
    void onMaterialChanged(const std::string& prop) override;
    void onRenderingContextApplied(const RenderingContext& context) override;
private:
    void updateRenderLayer(bool drawOnTop);
    bool m_drawOnTop{false};
};
