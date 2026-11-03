#include "DebugActorDBSync.hpp"
#include "VtkDBSyncFactory.hpp"

REGISTER_VTK_DBSYNC(TypeID::DEBUG_ACTOR_DB, DebugActorDBSync,
                        GPlatform::Rendering::RendererRoleSets::Prepare)
