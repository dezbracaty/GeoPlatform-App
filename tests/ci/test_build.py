"""Real SDK payload validation, without claiming an actual platform build."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/ci"))
from build import cache_value, validate_sdk
from common import digest


class AppSDKChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.slots = ["arm64/Release", "x86_64/Release"]
        for slot in self.slots:
            root = self.source / "ThirdParty/libs/GPlatformSDK" / slot
            (root / "lib").mkdir(parents=True)
            (root / "lib/test.a").write_bytes(b"actual payload")
            (root / "sdk-build.json").write_text(json.dumps({"SDK_FORMAT":2, "GPLATFORM_SDK_ARCH":slot.split("/")[0]}))
            self.manifest(root)

    def manifest(self, root):
        entries = {p.relative_to(root).as_posix():digest(p) for p in root.rglob("*")
                   if p.is_file() and p != root / "sdk-manifest.json"}
        (root / "sdk-manifest.json").write_text(json.dumps(entries))

    def test_both_sdk_payloads_validate(self):
        validate_sdk(self.source, self.slots)

    def test_second_architecture_corruption_fails(self):
        (self.source / "ThirdParty/libs/GPlatformSDK/x86_64/Release/lib/test.a").write_bytes(b"corrupt")
        with self.assertRaisesRegex(RuntimeError, "payload hash"):
            validate_sdk(self.source, self.slots)

    def test_missing_or_extra_payload_fails(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        (root / "extra.a").write_bytes(b"undeclared")
        with self.assertRaisesRegex(RuntimeError, "inventory"):
            validate_sdk(self.source, self.slots)

    def test_sdk_symlink_cannot_read_outside_slot(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/arm64/Release"
        outside = self.source / "outside.a"
        outside.write_bytes(b"external")
        (root / "lib/test.a").unlink()
        (root / "lib/test.a").symlink_to(outside)
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, "payload hash"):
            validate_sdk(self.source, self.slots)

    def test_wrong_architecture_fails(self):
        root = self.source / "ThirdParty/libs/GPlatformSDK/x86_64/Release"
        (root / "sdk-build.json").write_text(json.dumps({"SDK_FORMAT":2,"GPLATFORM_SDK_ARCH":"arm64"}))
        self.manifest(root)
        with self.assertRaisesRegex(RuntimeError, "matching format"):
            validate_sdk(self.source, self.slots)

    def test_missing_qt_cache_is_an_explicit_failure(self):
        build = self.source / "build-arm64-Release"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("Qt6_DIR:PATH=/toolchain/lib/cmake/Qt6\n")
        self.assertEqual(cache_value(build,"Qt6_DIR"),"/toolchain/lib/cmake/Qt6")
        with self.assertRaisesRegex(RuntimeError, "missing"):
            cache_value(build,"MISSING")


if __name__ == "__main__":
    unittest.main()
