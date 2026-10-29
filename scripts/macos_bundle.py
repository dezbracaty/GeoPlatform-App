"""Shared Qt deployment, Mach-O merging and runtime dependency checks."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile


ARCHITECTURES = ("arm64", "x86_64")
MACHO_MAGIC = {bytes.fromhex(x) for x in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe",
    "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}


class BuildError(RuntimeError):
    pass


def capture(command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


def run(command, log, environment=None):
    print("Running:", " ".join(map(str, command)), flush=True)
    with log.open("w") as output:
        result = subprocess.run(list(map(str, command)), stdout=output,
                                stderr=subprocess.STDOUT, env=environment)
    if result.returncode:
        raise BuildError(f"Command failed ({result.returncode}); see {log}")


def create_disk_image(bundle, destination, log):
    """Package an already deployed and audited bundle; preserve framework symlinks."""
    with tempfile.TemporaryDirectory(prefix='dmg-', dir=destination.parent) as temporary:
        image = Path(temporary) / 'image'
        image.mkdir()
        shutil.copytree(bundle, image / bundle.name, symlinks=True)
        (image / 'Applications').symlink_to('/Applications')
        run(['hdiutil', 'create', '-volname', bundle.stem, '-srcfolder', image,
             '-format', 'UDZO', '-ov', destination], log)


def is_macho(path):
    with path.open("rb") as source:
        return source.read(4) in MACHO_MAGIC


def inventory(bundle):
    files = {}
    for directory, children, names in os.walk(bundle, followlinks=False):
        children[:] = [name for name in children if name != "_CodeSignature"
                       and not name.endswith(".dSYM")]
        for name in list(children):
            path = Path(directory) / name
            if path.is_symlink():
                files[path.relative_to(bundle)] = path
                children.remove(name)
        for name in names:
            if name != ".DS_Store":
                path = Path(directory) / name
                files[path.relative_to(bundle)] = path
    return files


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_deployment(bundle, log):
    # macdeployqt copies these optional drivers even though GPlatform has no
    # external SQL-server integration. They require clients absent from Qt's
    # package; keep SQLite and reject all other deployment errors.
    optional = ("libqsqlmimer.dylib", "libqsqlodbc.dylib", "libqsqlpsql.dylib")
    excluded, external = [], set()
    for name in optional:
        plugin = bundle / "Contents/PlugIns/sqldrivers" / name
        if not plugin.exists():
            continue
        for line in capture(["otool", "-L", plugin]).splitlines():
            if " (compatibility version" in line:
                dependency = line.strip().split(" (compatibility version", 1)[0]
                if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
                    external.add(dependency)
        plugin.unlink()
        excluded.append(name)
    for line in log.read_text().splitlines():
        if line.startswith("ERROR:"):
            missing = re.fullmatch(r'ERROR: no file at "([^"]+)"', line)
            if not missing or missing.group(1) not in external:
                raise BuildError(f"Qt deployment reported an error: {line}; see {log}")
    # Static QML plugins are already linked into GPlatform. macdeployqt also
    # copies their build archives, which are not runtime resources and differ
    # across CPUs. Leave the actual QML/module resources for strict comparison.
    for archive in (bundle / "Contents/Resources/qml").rglob("*.a"):
        archive.unlink()
    return excluded


def deploy_bundle(source, deployed, build, qt_deploy, log, environment, qml_directory):
    shutil.copytree(source, deployed, symlinks=True,
                    ignore=shutil.ignore_patterns("*.dSYM", ".DS_Store"))
    run([qt_deploy, deployed, f"-qmldir={qml_directory}",
         f"-qmlimport={build / 'imports'}", "-always-overwrite", "-no-strip"], log, environment)
    return check_deployment(deployed, log)


def merge_bundles(arm_bundle, intel_bundle, destination, scratch, removed_rpaths=None):
    slices = scratch / "merge-slices"
    slices.mkdir(exist_ok=True)
    inventories = [inventory(bundle) for bundle in (arm_bundle, intel_bundle)]
    if inventories[0].keys() != inventories[1].keys():
        delta = sorted(map(str, inventories[0].keys() ^ inventories[1].keys()))
        raise BuildError(f"Architecture bundles have different files: {delta[:20]}")
    shutil.copytree(arm_bundle, destination, symlinks=True,
                    ignore=shutil.ignore_patterns("_CodeSignature", "*.dSYM", ".DS_Store"))
    merged = []
    for relative, arm in sorted(inventories[0].items()):
        intel = inventories[1][relative]
        output = destination / relative
        if arm.is_symlink() or intel.is_symlink():
            if not (arm.is_symlink() and intel.is_symlink()
                    and os.readlink(arm) == os.readlink(intel)):
                raise BuildError(f"Architecture symlinks differ: {relative}")
            continue
        if not is_macho(arm):
            if is_macho(intel) or sha(arm) != sha(intel):
                raise BuildError(f"Architecture resources differ: {relative}")
            continue
        for arch, source in zip(ARCHITECTURES, (arm, intel)):
            available = capture(["lipo", "-archs", source]).split()
            if arch not in available:
                raise BuildError(f"Missing {arch} slice: {source} ({available})")
            thin = slices / arch
            if len(available) == 1:
                shutil.copyfile(source, thin)
            else:
                subprocess.run(["lipo", str(source), "-thin", arch, "-output", str(thin)], check=True)
            remove_external_loader_rpaths(thin, arch, output, destination, removed_rpaths)
        subprocess.run(["lipo", "-create", str(slices / "arm64"),
                        str(slices / "x86_64"), "-output", str(output)], check=True)
        shutil.copymode(arm, output)
        merged.append(str(relative))
    return merged


def remove_external_loader_rpaths(binary, arch, output, bundle, removed=None):
    """Remove Qt's stale loader-relative search paths, then audit dependencies."""
    for value in load_commands(binary, arch)["rpaths"]:
        if value.startswith("@loader_path/") and not (
                output.parent / value[len("@loader_path/"):]).resolve().is_relative_to(bundle.resolve()):
            subprocess.run(["install_name_tool", "-delete_rpath", value, str(binary)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if removed is not None:
                removed.append({"path": str(output.relative_to(bundle)), "architecture": arch, "rpath": value})


def clean_bundle_rpaths(bundle):
    """Apply the shared cleanup to every slice of an ordinary deployed bundle."""
    removed = []
    with tempfile.TemporaryDirectory(prefix="rpaths-", dir=bundle.parent) as temporary:
        scratch = Path(temporary)
        for path in inventory(bundle).values():
            if path.is_symlink() or not is_macho(path):
                continue
            arches = capture(["lipo", "-archs", path]).split()
            slices, before = [], len(removed)
            for arch in arches:
                thin = scratch / arch
                if len(arches) == 1:
                    shutil.copyfile(path, thin)
                else:
                    subprocess.run(["lipo", str(path), "-thin", arch, "-output", str(thin)], check=True)
                remove_external_loader_rpaths(thin, arch, path, bundle, removed)
                slices.append(thin)
            if len(removed) != before:
                if len(slices) == 1:
                    shutil.copyfile(slices[0], path)
                else:
                    subprocess.run(["lipo", "-create", *map(str, slices), "-output", str(path)], check=True)
    return removed


def load_commands(path, arch):
    """Read paths from load commands, excluding a dylib's own LC_ID_DYLIB."""
    dependencies, rpaths, executable = [], [], False
    command = None
    loads = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
             "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB"}
    for line in capture(["otool", "-arch", arch, "-l", path]).splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line[4:]
            executable |= command in {"LC_MAIN", "LC_UNIXTHREAD"}
        elif command == "LC_RPATH" and line.startswith("path "):
            rpaths.append(line[5:].rsplit(" (offset ", 1)[0])
        elif command in loads and line.startswith("name "):
            dependencies.append(line[5:].rsplit(" (offset ", 1)[0])
    return {"dependencies": dependencies, "rpaths": rpaths, "executable": executable}


def audit_bundle(bundle, architectures=ARCHITECTURES):
    """Check required slices and resolve dependencies through their actual RPATHs.

    Libraries inherit the run-path stack of their loading chain. Qt plugins
    not statically reached from an executable are checked in the main app's
    context. Absolute system dependencies are allowed; other paths must remain
    relative to, and resolve inside, the relocatable application bundle.
    """
    bundle = bundle.resolve()
    binaries, images = [], {}
    for relative, path in sorted(inventory(bundle).items()):
        if path.is_symlink():
            if Path(os.readlink(path)).is_absolute() or not path.resolve().is_relative_to(bundle):
                raise BuildError(f"Nonportable bundle symlink: {relative}")
            if not path.exists():
                raise BuildError(f"Missing bundle symlink target: {relative}")
            continue
        if not is_macho(path):
            continue
        arches = capture(["lipo", "-archs", path]).split()
        if not set(architectures).issubset(arches):
            raise BuildError(f"Missing required architectures {architectures}: {relative}: {arches}")
        images[path.resolve()] = {arch: load_commands(path, arch) for arch in architectures}
        binaries.append({"path": str(relative), "architectures": arches, "sha256": sha(path)})
    if not binaries:
        raise BuildError("No Mach-O binaries found in app")

    info = bundle / "Contents/Info.plist"
    main = None
    if info.exists():
        with info.open("rb") as stream:
            executable = plistlib.load(stream).get("CFBundleExecutable")
        if executable:
            main = (bundle / "Contents/MacOS" / executable).resolve()

    def relative_path(value, loader, executable):
        for prefix, base in (("@loader_path/", loader.parent),
                             ("@executable_path/", executable.parent)):
            if value == prefix[:-1] or value.startswith(prefix):
                target = (base / value[len(prefix):]).resolve()
                if target.is_relative_to(bundle):
                    return target
        raise BuildError(f"Nonportable runtime path in {loader.relative_to(bundle)}: {value}")

    for arch in architectures:
        executables = [path for path, slices in images.items() if slices[arch]["executable"]]
        primary = main or next((p for p in executables if p.parent == bundle / "Contents/MacOS"), None)
        if primary not in executables:
            raise BuildError(f"Missing main {arch} executable in bundle")
        visited, reached = set(), set()

        def walk(path, executable, inherited=(), chain=()):
            if path in chain:
                return
            image = images[path][arch]
            own = [relative_path(value, path, executable) for value in image["rpaths"]]
            runpaths = tuple(dict.fromkeys((*own, *inherited)))
            identity = (path, executable, runpaths)
            if identity in visited:
                return
            visited.add(identity)
            reached.add(path)
            for dependency in image["dependencies"]:
                if os.path.normpath(dependency).startswith(("/System/Library/", "/usr/lib/")):
                    continue
                if dependency.startswith("@rpath/"):
                    candidates = [(root / dependency[len("@rpath/"):]).resolve() for root in runpaths]
                    if any(not candidate.is_relative_to(bundle) for candidate in candidates):
                        raise BuildError(f"Nonportable {arch} dependency in {path.relative_to(bundle)}: {dependency}")
                    target = next((candidate for candidate in candidates if candidate.exists()), None)
                else:
                    target = relative_path(dependency, path, executable)
                if target not in images:
                    raise BuildError(f"Missing bundled {arch} dependency in {path.relative_to(bundle)}: {dependency}")
                walk(target, executable, runpaths, (*chain, path))

        for executable in executables:
            walk(executable, executable)
        main_rpaths = tuple(relative_path(value, primary, primary) for value in images[primary][arch]["rpaths"])
        for path in images:
            if path not in reached:
                walk(path, primary, main_rpaths)
    return binaries
