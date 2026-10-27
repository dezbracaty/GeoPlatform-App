"""Batch the repository's existing Journal scenes against the packaged executable."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time
import types
import os
import hashlib

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=root.parent / 'GPlatform-ui-sdk-build')
parser.add_argument('--list', action='store_true')
parser.add_argument('--application', type=Path, default=root / 'build-Windows-Release/GPlatform.exe')
parser.add_argument('--cases', default='libslicer_slice_line_type,libslicer_export_menu,libslicer_preview_exit,libslicer_preview_exit_camera_stress,libslicer_slice_progress')
parser.add_argument('--output',type=Path,default=root / 'test-results/startup-journals')
parser.add_argument('--exclude', default='')
args = parser.parse_args()
source = args.source.resolve()
sys.path.insert(0, str(source / 'ThirdParty/JournalSystem/tools'))
from journal_runner import load_journal

# Keep the source harness unchanged. Windows may retain an inherited stdout
# handle in an app-server descendant after the GUI process has exited.
# Retain that diagnostic file rather than failing before the real result saves.
original_harness = source / 'ThirdParty/JournalSystem/tools/journal_execution_session.py'
original_text = original_harness.read_text(encoding='utf-8')
needle = '                    Path(stream.name).unlink()'
replacement = '''                    try:
                        Path(stream.name).unlink()
                    except PermissionError:
                        result["retainedStartupLogPath"] = stream.name
                        result["diagnosticWarnings"] = ["Windows retained a descendant stdout handle; startup log preserved"]'''
if original_text.count(needle) != 1:
    raise RuntimeError('Unexpected upstream harness structure')
adapted_text = original_text.replace(needle, replacement)
terminate_needle = '                        process.terminate()'
terminate_replacement = '''                        if os.name == "nt":
                            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        else:
                            process.terminate()'''
if adapted_text.count(terminate_needle) != 1:
    raise RuntimeError('Unexpected upstream process-cleanup structure')
session_module = types.ModuleType('gplatform_windows_journal_session')
session_module.__file__ = str(original_harness)
sys.modules[session_module.__name__] = session_module
exec(compile(adapted_text.replace(terminate_needle, terminate_replacement), str(original_harness), 'exec'), session_module.__dict__)
JournalExecutionSession = session_module.JournalExecutionSession

scenes = source / 'Core/tests/journals'
inventory = {}
for directory in sorted(scenes.iterdir()):
    journal = directory / 'events/Journal.json'
    if not journal.is_file():
        continue
    document = load_journal(journal)
    metadata = json.loads((directory / 'metadata.json').read_text(encoding='utf-8'))
    missing = []
    actions = []
    for entry in document['entries']:
        payload = entry['payload']
        if entry['kind'] == 'action':
            actions.append(payload['code'])
            if payload['code'] == 'file.import':
                fixture = (journal.parent / payload.get('params', {}).get('filePath', '')).resolve()
                if not fixture.is_file():
                    missing.append(str(fixture))
                elif fixture.open('rb').read(100).startswith(b'version https://git-lfs.github.com/spec/'):
                    missing.append(str(fixture) + ' (LFS pointer)')
    inventory[directory.name] = {'journal': str(journal), 'entries': len(document['entries']),
        'durationMs': document['entries'][-1]['atMs'], 'actions': sorted(set(actions)),
        'environment': metadata.get('applicationEnvironment', {}), 'missingFixtures': missing}
if args.list:
    for name, record in inventory.items():
        print(json.dumps({'case': name, **record}, ensure_ascii=True), flush=True)
    sys.exit(0)

out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
(out / 'inventory.json').write_text(json.dumps(inventory, indent=2, ensure_ascii=False), encoding='utf-8')
settings = Path(os.environ['LOCALAPPDATA']) / 'GPlatform/GPlatform' / (args.application.stem+'.ini')
backup = settings.read_bytes() if settings.is_file() else None
summary = {'application': str(args.application.resolve()), 'source': str(source), 'applicationSha256': hashlib.sha256(args.application.read_bytes()).hexdigest(),
           'scope': 'Existing Journal user scenarios; excludes system installation and real-device commands',
           'cases': [], 'note': 'Original Journal acceptance is preserved. not_verified means visual/business review is still needed.',
           'harnessAdaptation': {'original': str(original_harness), 'originalSha256': hashlib.sha256(original_harness.read_bytes()).hexdigest(),
             'adapter': 'Windows cleanup adaptation in memory', 'change': 'Catch Windows PermissionError on temporary log deletion and retain log; terminate only the owned GUI process tree on Windows. Business execution and assertions unchanged.'}}
try:
    selected = list(inventory) if args.cases == 'all' else args.cases.split(',')
    selected = [name for name in selected if name not in args.exclude.split(',')]
    if 'continuous_fiber_4xiao_three_contours' in selected:
        selected.remove('continuous_fiber_4xiao_three_contours')
        selected.append('continuous_fiber_4xiao_three_contours')
    for name in selected:
        if backup is not None:
            settings.write_bytes(backup)
        record = inventory[name]
        print(f'BEGIN {name}: {record["entries"]} entries / {record["durationMs"]}ms', flush=True)
        if record['missingFixtures']:
            result = {'success': False, 'acceptance': 'blocked_fixture', 'missing': record['missingFixtures']}
        else:
            started = time.monotonic()
            result = JournalExecutionSession().run(
                record['journal'], application=args.application.resolve(),
                output_dir=out / name, startup_timeout=120,
                log=lambda message: print(message, flush=True))
            result['elapsedSeconds'] = round(time.monotonic()-started, 2)
        summary['cases'].append({'case': name, 'result': result})
        (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
        print(f'END {name}: {result.get("acceptance")} / entries {result.get("completedEntries",0)}/{record["entries"]}', flush=True)
finally:
    if backup is not None:
        settings.write_bytes(backup)
    else:
        settings.unlink(missing_ok=True)
    summary['settingsRestored'] = True
    (out / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
