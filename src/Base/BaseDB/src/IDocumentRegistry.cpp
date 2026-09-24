#include "../include/IDocumentRegistry.hpp"

namespace {
IDocumentRegistry* g_documentRegistry = nullptr;
}

IDocumentRegistry* documentRegistry() {
    return g_documentRegistry;
}

void setDocumentRegistry(IDocumentRegistry* registry) {
    g_documentRegistry = registry;
}
