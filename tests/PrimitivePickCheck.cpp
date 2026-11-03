#include <ModelGraphUtil.hpp>
#include <ModelGraphRelations.hpp>
#include <ModelInstanceDB.hpp>
#include <vtkCubeSource.h>
#include <vtkSphereSource.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyData.h>
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
    ModelGraphRelations::registerRelations();
    auto cubeSource = vtkSmartPointer<vtkCubeSource>::New();
    cubeSource->SetXLength(2);
    cubeSource->SetYLength(2);
    cubeSource->SetZLength(2);
    auto cubeTriangles = vtkSmartPointer<vtkTriangleFilter>::New();
    cubeTriangles->SetInputConnection(cubeSource->GetOutputPort());
    cubeTriangles->Update();
    auto cubeGraph = ModelGraphUtil::createSinglePartObject(
        {"Cube", {}, "generated"}, cubeTriangles->GetOutput(), ModelGraphUtil::InstanceCreateInfo{});
    if (!cubeGraph || !cubeGraph.instance) return fail("Could not create cube model");
    auto cube = std::static_pointer_cast<ActorDB>(cubeGraph.instance);
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

    cubeSource->SetCenter(2, 0, 0);
    cubeTriangles->Update();
    if (!ModelGraphUtil::replaceInstancePartGeometry(cube->getDBInstanceID(),
            cubeGraph.parts.front()->getDBInstanceID(), cubeTriangles->GetOutput()))
        return fail("Could not replace cube geometry");
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
    auto sphereSource = vtkSmartPointer<vtkSphereSource>::New();
    sphereSource->SetRadius(1);
    sphereSource->SetThetaResolution(64);
    sphereSource->SetPhiResolution(32);
    sphereSource->Update();
    auto sphereGraph = ModelGraphUtil::createSinglePartObject(
        {"Sphere", {}, "generated"}, sphereSource->GetOutput(), ModelGraphUtil::InstanceCreateInfo{});
    if (!sphereGraph || !sphereGraph.instance) return fail("Could not create sphere model");
    auto sphere = std::static_pointer_cast<ActorDB>(sphereGraph.instance);
    const auto sphereHit = sphere->intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!sphereHit || !near(sphereHit->worldDistance, 4) ||
        sphere->intersectPickRay({0.9f, 0.9f, 5}, {0, 0, -1}))
        return fail("Sphere model picking must use its surface rather than its bounding box");
    sphereSource->SetRadius(2);
    sphereSource->Update();
    if (!ModelGraphUtil::replaceInstancePartGeometry(sphere->getDBInstanceID(),
            sphereGraph.parts.front()->getDBInstanceID(), sphereSource->GetOutput()))
        return fail("Could not replace sphere geometry");
    const auto resized = sphere->intersectPickRay({0, 0, 5}, {0, 0, -1});
    if (!resized || !near(resized->worldDistance, 3))
        return fail("Model geometry replacement must invalidate the cached pick locator");

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
    std::cout << "PASS abstract ActorDB contract, model picks, transforms, "
                 "exact misses, cache invalidation, GLB and presentation policies" << std::endl;
    Log::shutdown();
    return 0;
}
