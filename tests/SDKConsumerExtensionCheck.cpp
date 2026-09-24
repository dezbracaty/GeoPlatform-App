#include <AutoRegisterDB.hpp>
#include <DocumentManager.hpp>
#include <RendererBackendCatalog.hpp>
#include <MeshRayQuery.hpp>
#include <QCoreApplication>
#include <cmath>
#include <iostream>

// These types exist only in the consumer, after the SDK has been built.
class ConsumerDB final : public AutoRegisterDB {
public:
    TypeID getTypeID() const override { return static_cast<TypeID>(0x7f000001); }
    bool needsVTKSync() const override { return false; }
};

class ConsumerRendererFactory final : public GPlatform::Rendering::IRendererBackendFactory {
public:
    GPlatform::Rendering::RendererBackendDescriptor descriptor() const override {
        return {QStringLiteral("consumer-extension"), QStringLiteral("Consumer extension"),
                {GPlatform::Rendering::GraphicsApi::OpenGL}};
    }
    std::shared_ptr<GPlatform::Rendering::IRendererSession> createSession(
        const GPlatform::Rendering::RendererSessionCreateInfo&) const override {
        return {};
    }
};

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    MeshRayQuery geometry;
    if (!geometry.isAvailable() ||
        !geometry.build({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 1, 2}})) {
        std::cerr << "Consumer geometry could not construct its BVH\n";
        return 3;
    }
    const auto hit = geometry.firstHit({0.25f, 0.25f, 1}, {0, 0, -1});
    if (!hit.valid || hit.facetId != 0 || std::abs(hit.distance - 1.0f) > 1e-5f) {
        std::cerr << "Consumer geometry BVH did not resolve the expected ray hit\n";
        return 4;
    }
    auto* document = DocumentManager::instance();
    auto db = AutoRegisterDB::create<ConsumerDB>();
    if (!db || document->getDBInstance(db->getDBInstanceID()) != db) {
        std::cerr << "Consumer-defined DB did not register with the document\n";
        return 1;
    }
    using namespace GPlatform::Rendering;
    registerCompiledRendererBackends();
    auto& catalog = RendererBackendCatalog::instance();
    if (catalog.backendIds().isEmpty() ||
        !catalog.registerFactory(std::make_shared<ConsumerRendererFactory>()) ||
        !catalog.find(u"consumer-extension")) {
        std::cerr << "Consumer renderer extension did not coexist with SDK backends\n";
        return 2;
    }
    return 0;
}
