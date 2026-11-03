#pragma once
#include "ActorDBSync.hpp"
#include <SnapPreviewDB.hpp>

// VTK projection of backend-independent, immutable snap presentation data.
class SnapPreviewDBSync final : public ActorDBSync {
public:
    void synchronizeView() override;
protected:
    void createActor() override;
    void updateGeometry(const std::string&) override;
private:
    SnapPreviewDB::PreviewPtr m_preview;
};
