#!/usr/bin/env python3
"""Deploy, sign and audit one built macOS App; optionally create its disk image."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from macos_bundle import (ARCHITECTURES, BuildError, audit_bundle,
                          clean_bundle_rpaths, deploy_bundle, run)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-bundle', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--qt-dir', type=Path, required=True)
    parser.add_argument('--qml-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--architectures', choices=ARCHITECTURES, nargs='+', required=True)
    parser.add_argument('--dmg', action='store_true')
    args = parser.parse_args()
    source, build, qt, qml, output = (path.resolve() for path in
        (args.source_bundle, args.build_dir, args.qt_dir, args.qml_dir, args.output_dir))
    if (args.output_dir.is_symlink() or output.is_relative_to(source)
            or any(path.is_relative_to(output) for path in (source, build, qt, qml))):
        parser.error('Deployment output must be separate from build and source inputs')
    if not source.is_dir() or source.suffix != '.app':
        parser.error(f'Missing built application: {source}')
    if not (qt / 'Qt6Config.cmake').is_file() or not qml.is_dir():
        parser.error('Provide the selected Qt package directory and application QML directory')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='deploy-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'dist'
        stage.mkdir()
        bundle = stage / source.name
        excluded = deploy_bundle(source, bundle, build, qt.parents[2] / 'bin/macdeployqt',
                                 build / 'dist-deploy.log', os.environ.copy(), qml)
        removed = clean_bundle_rpaths(bundle)
        run(['codesign', '--force', '--deep', '--sign', '-', bundle], build / 'dist-codesign.log')
        run(['codesign', '--verify', '--deep', '--strict', bundle], build / 'dist-codesign-verify.log')
        binaries = audit_bundle(bundle, args.architectures)
        if args.dmg:
            # Package the already audited bundle without running Qt deployment again.
            image_source = Path(temporary) / 'image'
            image_source.mkdir()
            shutil.copytree(bundle, image_source / bundle.name, symlinks=True)
            (image_source / 'Applications').symlink_to('/Applications')
            run(['hdiutil', 'create', '-volname', source.stem, '-srcfolder', image_source,
                 '-format', 'UDZO', '-ov', stage / (source.stem + '.dmg')], build / 'dist-dmg.log')
        (stage / 'report.json').write_text(json.dumps({
            'status': 'passed', 'architectures': args.architectures, 'binaries': binaries,
            'app': str(output / source.name), 'excludedUnusedSqlDrivers': excluded,
            'removedExternalLoaderRpaths': removed,
            'signing': 'local ad-hoc; not notarized', 'runtimeTested': False,
        }, indent=2) + '\n')
        if output.exists():
            shutil.rmtree(output)
        stage.rename(output)
    print(f'Deployed application: {output / source.name}')


if __name__ == '__main__':
    try:
        main()
    except (BuildError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
