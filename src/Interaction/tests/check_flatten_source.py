"""Check that extracted flatten geometry stays identical to pinned Orca code."""
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ORCA = ROOT / "ThirdParty/OrcaSlicer"
REVISION = "3c8c667dbd402eb52e24fcc69aea3a1b5c5c72a1"
ORIGINAL_PATH = "src/slic3r/GUI/Gizmos/GLGizmoFlatten.cpp"
EXPECTED_SHA256 = "68861c3636e1d875488d087e6b7c7a6538f591050ac43ee67a44c0c12cdabcd9"


def body(text, end):
    text = text[text.index("    // Following constants"):text.index(end)]
    for original, adapted in (
        ("ch.facets_count()", "ch.indices.size()"),
        ("its_face_normals(ch.its)", "meshFaceNormals(ch)"),
        ("its_face_neighbors(ch.its)", "meshFaceNeighbors(ch)"),
        ("ch.its.indices[", "ch.indices["),
        ("ch.its.vertices[", "ch.vertices["),
        ("BoundingBoxf3(polygon).size()", "polygonBoundsSize(polygon)"),
        ("Geometry::scale_transform", "scaleTransform"),
        ("Slic3r::Geometry::convex_hull", "polygonConvexHull"),
    ):
        text = text.replace(adapted, original)
    text = text.replace("const PlaneData&", "const FlattenPlane&")
    return "\n".join(line.rstrip() for line in text.splitlines()).strip() + "\n"


compiled = (ROOT / "src/Interaction/src/FlattenGeometry.cpp").read_text(encoding="utf-8")
extracted = body(compiled[compiled.index("std::vector<PlaneData> buildPlanes("):], "    return m_planes;")
assert hashlib.sha256(extracted.encode()).hexdigest() == EXPECTED_SHA256, "Flatten numerical algorithm differs from pinned Orca source"

polygon_hull = compiled[compiled.index("Pointf3s polygonConvexHull("):compiled.index("Pointf3s transform(")]
polygon_hull = polygon_hull.replace("polygonConvexHull", "convex_hull")
polygon_hull = polygon_hull.replace("if (orient(", "if (Geometry::orient(")
polygon_hull = polygon_hull.replace("!= ORIENTATION_CCW", "!= Geometry::ORIENTATION_CCW")
polygon_hull = "\n".join(line.rstrip() for line in polygon_hull.splitlines()).strip() + "\n"
assert hashlib.sha256(polygon_hull.encode()).hexdigest() == "bce517087427a381e159c671b31519ffe39e8207c10864f240e8205688c33d47", "Polygon hull differs from pinned Orca source"
if shutil.which("git"):
    original = subprocess.run(["git", "-C", str(ORCA), "show", f"{REVISION}:{ORIGINAL_PATH}"], capture_output=True)
    if original.returncode == 0:
        assert extracted == body(original.stdout.decode("utf-8"), "    // Planes are finished"), "Flatten extraction differs from original Git object"
print("Orca flatten geometry verified:", REVISION, ORIGINAL_PATH)
