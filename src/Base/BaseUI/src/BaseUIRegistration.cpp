#include "BaseUIRegistration.hpp"
#include "NotificationManager.h"

namespace GPlatform::BaseUI {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    NotificationManager::initialize(*QCoreApplication::instance());
    moduleRegistrations().execute();
}

} // namespace GPlatform::BaseUI
