#include "Printer/PrinterRegistration.hpp"

namespace GPlatform::Printer {

ModuleRegistrationList& moduleRegistrations() {
    static ModuleRegistrationList registrations;
    return registrations;
}

void registerModule() {
    moduleRegistrations().execute();
}

} // namespace GPlatform::Printer
