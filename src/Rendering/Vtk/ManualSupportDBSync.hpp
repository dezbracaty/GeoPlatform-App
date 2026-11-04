#pragma once

#include "ActorDBSync.hpp"

/** VTK projection of one ManualSupportDB aggregate. */
class ManualSupportDBSync final : public ActorDBSync {
public:
    using ActorDBSync::ActorDBSync;

    void createActor() override;
    void updateGeometry(const std::string& prop = "SupportGeometry") override;
};
