#include <CubeDB.hpp>
#include <SphereDB.hpp>
#include <CylinderDB.hpp>
#include <ConeDB.hpp>
#include <GlbDB.hpp>
#include <FiberMaskDB.hpp>
#include <ManualSupportDB.hpp>
#include <PrintBedDB.hpp>
#include <SnapPreviewDB.hpp>
#include <ToolpathPreviewDB.hpp>
#include <Foundation/Log.h>
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

// Implementing bounds and cloning is insufficient: a concrete actor must also
// explicitly implement the picking contract.
class ActorWithoutPick : public ActorDB {
public:
    TypeID getTypeID() const override { return TypeID::ACTOR_DB; }
    BoundingBox localBounds() const override { return {}; }
    std::shared_ptr<AutoRegisterDB> clone() const override { return {}; }
};
class ActorWithPick final : public ActorWithoutPick {
public:
    std::optional<PickHit> intersectPickRay(
        const Vector3&, const Vector3&, float) const override { return std::nullopt; }
};
static_assert(std::is_abstract_v<ActorWithoutPick>);
static_assert(!std::is_abstract_v<ActorWithPick>);
static_assert(!std::is_abstract_v<CubeDB> && !std::is_abstract_v<SphereDB> &&
              !std::is_abstract_v<CylinderDB> && !std::is_abstract_v<ConeDB>);

namespace {
bool near(float actual, float expected) {
    return std::abs(actual - expected) < 1.e-4f;
}
int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName("GPlatformTests");
    QCoreApplication::setApplicationName("PrimitivePickCheck");
    Log::setup(argv, QStringLiteral("PrimitivePickCheck"));
    auto cube = trans::TransDB::create<CubeDB>();
    cube->setUniformSize(2);
    cube->setTransform(Transform{});
    const auto hit = cube->intersectPickRay({0, 0, 5}, {0, 0, -2});
    if (!hit || !near(hit->worldDistance, 4) || !near(hit->localPosition.z, 1))
        return fail("A visible cube must resolve a CPU object pick in world units");
    if (cube->intersectPickRay({2, 0, 5}, {0, 0, -1}) ||
        cube->intersectPickRay({0, 0, 5}, {0, 0, 1}) ||
        cube->intersectPickRay({0, 0, 5}, {0, 0, -1}, 3.9f))
        return fail("Misses, backward hits and hits beyond the nearest object must be rejected");
    const auto inside = cube->intersectPickRay({0, 0, 0}, {1, 0, 0});
    if (!inside || !near(inside->worldDistance, 1))
        return fail("A ray starting inside a cube must hit the exit face");

    cube->setCenter({2, 0, 0});
    Transform transform;
    transform.rotateZ(90);
    transform.setScale({2, 3, 4});
    transform.setPosition({10, 20, 30});
    cube->setTransform(transform);
    const auto transformed = cube->intersectPickRay({10, 24, 40}, {0, 0, -1});
    if (!transformed || !near(transformed->worldDistance, 6) ||
        !near(transformed->worldPosition.z, 34) ||
        !near(transformed->localPosition.x, 2) ||
        !near(transformed->localPosition.z, 1))
        return fail("Picking must honor geometry center, translation, rotation and nonuniform scale");
    if (cube->intersectPickRay({14, 24, 40}, {0, 0, -1}) ||
        cube->intersectPickRay({10, 24, 40}, {0, 0, 0}) ||
        cube->intersectPickRay({10, 24, 40}, {0, 0, -1}, -1))
        return fail("Parallel misses and invalid rays must be rejected");
    transform.getMatrix().col(0).setZero();
    cube->setTransform(transform);
    if (cube->intersectPickRay({10, 24, 40}, {0, 0, -1}))
        return fail("A singular transform must not produce a hit");
    cube->setTransform(Transform{});
    cube->setCenter({0, 0, 0});
    cube->setUniformSize(0);
    if (cube->intersectPickRay({0, 0, 5}, {0, 0, -1}))
        return fail("Degenerate geometry must not produce a hit");
    auto sphere = trans::TransDB::create<SphereDB>();
    sphere->setRadius(1);
    const auto sphereHit = sphere->intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!sphereHit || !near(sphereHit->worldDistance, 4) ||
        sphere->intersectPickRay({0.9f, 0.9f, 5}, {0, 0, -1}))
        return fail("Sphere picking must use its surface rather than its bounding box");
    sphere->setRadius(2);
    const auto resized = sphere->intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!resized || !near(resized->worldDistance, 3))
        return fail("Geometry changes must invalidate the cached pick locator");
    sphere->setProperty<float>(SphereDB::PROP_Radius(), 3.0f);
    const auto genericWrite = sphere->intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!genericWrite || !near(genericWrite->worldDistance, 2))
        return fail("Generic property writes must not leave stale pick geometry");

    auto cylinder = trans::TransDB::create<CylinderDB>();
    cylinder->setHeight(2);
    cylinder->setRadius(1);
    const auto cap = cylinder->intersectPickRay({0, 5, 0}, {0, -1, 0});
    if (!cap || !near(cap->worldDistance, 4) ||
        cylinder->intersectPickRay({0.9f, 5, 0.9f}, {0, -1, 0}))
        return fail("Cylinder caps must use circular geometry");
    cylinder->setCapping(false);
    if (cylinder->intersectPickRay({0, 5, 0}, {0, -1, 0}))
        return fail("An uncapped cylinder must not pick an invisible cap");

    auto cone = trans::TransDB::create<ConeDB>();
    cone->setHeight(2);
    cone->setRadius(1);
    cone->setDirection(2);
    const auto side = cone->intersectPickRay({5, 0, 0}, {-1, 0, 0});
    if (!side || !near(side->worldDistance, 4.5f))
        return fail("Cone direction and tapered surface must participate in picking");
    const auto base = cone->intersectPickRay({0, 0, -5}, {0, 0, 1}, 5);
    if (!base || !near(base->worldDistance, 4))
        return fail("A capped cone must pick its base");
    cone->setCapping(false);
    if (cone->intersectPickRay({0, 0, -5}, {0, 0, 1}, 5))
        return fail("An uncapped cone must not pick an invisible base");

    // Exercise the same virtual interface used by PickService.
    const ActorDB& sphereActor = *sphere;
    if (!sphereActor.intersectPickRay({0, 0, 5}, {0, 0, -1}))
        return fail("The ActorDB interface must dispatch the concrete surface query");
    sphere->setPickable(false);
    if (sphereActor.intersectPickRay({0, 0, 5}, {0, 0, -1}))
        return fail("A disabled surface must not produce a hit");

    auto glb = trans::TransDB::create<GlbDB>();
    auto asset = [](float z) {
        GlbImportResult result;
        result.success = true;
        result.asset = std::make_shared<GlbAssetData>(
            std::make_shared<GlbAssetData::Bytes>(1, 1), GlbAssetMetadata{});
        result.printableTriangles.emplace_back(Vector3{-1, -1, z},
            Vector3{1, -1, z}, Vector3{0, 1, z});
        result.boundingBoxMin = {-1, -1, z};
        result.boundingBoxMax = {1, 1, z};
        result.vertexCount = 3;
        result.faceCount = 1;
        return result;
    };
    if (!glb->replaceImportedAsset(asset(0)))
        return fail("GLB test surface could not be published");
    const ActorDB& glbActor = *glb;
    const auto glbHit = glbActor.intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!glbHit || !near(glbHit->worldDistance, 5) ||
        glbActor.intersectPickRay({0.9f, 0.9f, 5}, {0, 0, -1}))
        return fail("GLB must pick triangles rather than their bounding box");
    if (!glb->replaceImportedAsset(asset(1)))
        return fail("GLB replacement surface could not be published");
    const auto replaced = glbActor.intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!replaced || !near(replaced->worldDistance, 4))
        return fail("GLB asset replacement must invalidate picking geometry");

    const std::shared_ptr<ActorDB> presentations[]{
        trans::TransDB::create<FiberMaskDB>(),
        trans::TransDB::create<ManualSupportDB>(),
        trans::TransDB::create<PrintBedDB>(),
        trans::TransDB::create<SnapPreviewDB>(),
        trans::TransDB::create<GPlatform::ToolpathPreviewDB>()};
    for (const auto& actor : presentations) {
        if (actor->isPickable() || actor->intersectPickRay({0, 0, 5}, {0, 0, -1}))
            return fail("Presentation-only actors must explicitly remain non-pickable");
    }
    std::cout << "PASS abstract ActorDB contract, four primitive picks, transforms, "
                 "exact misses, cache invalidation, caps, GLB and presentation policies" << std::endl;
    Log::shutdown();
    return 0;
}
