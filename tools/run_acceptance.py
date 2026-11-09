#!/usr/bin/env python3
"""Run versioned App acceptance assets through JournalSystem and business checks."""
import argparse
import configparser
import json
import os
from pathlib import Path
import sys
from datetime import datetime

from acceptance import load_case, run_case, write_summary
from verification_methods import METHODS, SUPPLEMENTAL_VALIDATORS, VALIDATORS
from screenshot_review import checkpoint_declarations


def regression_environment():
    """App owns its Windows regression policy; other platforms keep native defaults."""
    if sys.platform == 'win32':
        return dict(QT_ENABLE_HIGHDPI_SCALING='0', QT_SCALE_FACTOR='1', QT_FONT_DPI='96')
    return None


def select_cases(names, cases=None, *, exclude=''):
    """Keep explicit filters effective and distinguish them from the full manifest."""
    requested = cases if cases is not None else 'p3_height_color_export'
    selected = names[:] if requested == 'all' else requested.split(',')
    selected = [name for name in selected if name not in exclude.split(',')]
    if not selected or len(selected) != len(set(selected)) or set(selected) - set(names):
        raise ValueError('Select distinct existing scenario IDs')
    return selected


def update_run_summary(summary):
    cases, planned = summary['cases'], summary['plannedCases']
    summary.update(completedCaseCount=len(cases), plannedCaseCount=len(planned),
        runComplete=len(cases) == len(planned), notRunCases=[name for name in planned if name not in {c['id'] for c in cases}])
    statuses = [case['status'] for case in cases]
    summary['status'] = ('failed' if 'failed' in statuses else 'blocked' if 'blocked' in statuses else
                         'not_verified' if not summary['runComplete'] or 'not_verified' in statuses else 'passed')


def prepare_theme(settings, theme):
    """Use the same persisted preference as SettingsHelper before launching the App."""
    if theme is None:
        return
    if theme not in ('light', 'dark'):
        raise ValueError('Scenario theme must be light or dark')
    if settings is None:
        raise ValueError('Scenario theme requires --settings-file on this platform')
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(settings, encoding='utf-8')
    if not config.has_section('General'):
        config.add_section('General')
    config.set('General', 'darkMode', '0' if theme == 'light' else '1')
    settings.parent.mkdir(parents=True, exist_ok=True)
    with settings.open('w', encoding='utf-8') as stream:
        config.write(stream)



def main(argv=None, *, session=None, progress=None):
    app_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Complete host source (compatibility entry)')
    parser.add_argument('--assets', type=Path, default=app_root / 'tests/TestAssets')
    parser.add_argument('--journal-tools', type=Path, default=app_root / 'tools/journal',
                        help='Override the bundled Journal tools directory for development')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--list-methods', action='store_true', help='Print the fixed method and adapter registry')
    parser.add_argument('--application', type=Path)
    parser.add_argument('--cases', help='Comma-separated IDs, or all; defaults to all for --list, otherwise p3_height_color_export')
    parser.add_argument('--exclude', default='')
    parser.add_argument('--capture-candidates', action='store_true',
                        help='Capture DB/screenshots at existing checkpoints for review; never adopts standards or passes formal acceptance')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--exports', type=Path, help='Actual App export directory; files must be generated in this run')
    parser.add_argument('--settings-file', type=Path, help='App INI to restore between cases (Windows default is detected)')
    args = parser.parse_args(argv)
    if args.list_methods:
        print(json.dumps(dict(schemaVersion=2, methods=METHODS, validators=VALIDATORS,
                              supplementalChecks=SUPPLEMENTAL_VALIDATORS), ensure_ascii=True, indent=2))
        return 0
    if args.source:
        args.assets = args.source / 'App/tests/TestAssets'
        args.journal_tools = args.source / 'App/tools/journal'
    if not (args.assets / 'catalog.json').is_file():
        parser.error('Initialize pinned assets: git -C App submodule update --init --checkout tests/TestAssets')
    if not (args.journal_tools / 'journal_execution_session.py').is_file():
        parser.error(f'Journal tools not found at {args.journal_tools}; use the bundled App tools/journal directory')
    sys.path.insert(0, str(args.journal_tools.resolve()))
    from journal_runner import load_journal
    from journal_execution_session import JournalExecutionSession
    names = sorted(p.parent.parent.name for p in (args.assets / 'scenarios').glob('*/events/Journal.json'))
    if args.list:
        try:
            listed = select_cases(names, args.cases or 'all', exclude=args.exclude)
        except ValueError as exc:
            parser.error(str(exc))
        for name in listed:
            row = dict(case=name, journal=str((args.assets / 'scenarios' / name / 'events/Journal.json').resolve()), missingFixtures=[])
            try:
                case = load_case(args.assets, name, load_journal)
                row.update(entries=len(case['document']['entries']), durationMs=case['document']['entries'][-1]['atMs'],
                           checks=[c['id'] for c in case['metadata'].get('checks', [])], preflight='ready',
                           scope=case['metadata']['scope'], testPurpose=case['metadata']['testPurpose'],
                           verificationPlan=case['verificationPlan'])
            except Exception as exc:
                row.update(preflight='blocked', error=str(exc))
            print(json.dumps(row, ensure_ascii=True), flush=True)
        return 0
    if not args.application:
        parser.error('--application is required')
    try:
        selected = select_cases(names, args.cases, exclude=args.exclude)
    except ValueError as exc:
        parser.error(str(exc))
    output = (args.output or Path('test-results/app-acceptance') / datetime.now().strftime('%Y%m%d_%H%M%S_%f')).resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = dict(schemaVersion=1, status='not_verified', plannedCases=selected, cases=[],
                   applicationPath=str(args.application.resolve()), assetsRoot=str(args.assets.resolve()),
                   selectionScope='all' if args.cases == 'all' and selected == names else 'explicit')
    if args.capture_candidates:
        summary['candidateCollection'] = True
    environment = regression_environment()
    if environment is not None:
        summary['environmentOverrides'] = environment
    update_run_summary(summary)
    summary['screenshotCheckpoints'] = {}
    for name in selected:
        try:
            document = load_journal(args.assets / 'scenarios' / name / 'events/Journal.json')
            summary['screenshotCheckpoints'][name] = checkpoint_declarations(document)
        except Exception:
            pass  # run_case records the declaration/preflight failure.
    settings = args.settings_file
    if settings is None and os.name == 'nt':
        settings = Path(os.environ['LOCALAPPDATA']) / 'GPlatform/GPlatform' / (args.application.stem + '.ini')
    elif settings is None and sys.platform == 'darwin':
        settings = Path.home() / 'Library/Application Support/GPlatform/GPlatform' / (args.application.stem + '.ini')
    backup = settings.read_bytes() if settings is not None and settings.is_file() else None
    def restore_settings():
        if settings is not None:
            if backup is not None:
                settings.write_bytes(backup)
            else:
                settings.unlink(missing_ok=True)
    try:
        for case_index, name in enumerate(selected, 1):
            if session is not None and getattr(session, 'cancel_event', None) is not None and session.cancel_event.is_set():
                summary['cancelled'] = True
                raise InterruptedError('Replay cancelled by caller')
            if progress:
                progress(dict(case=name, index=case_index, total=len(selected), phase='begin'))
            restore_settings()
            metadata = json.loads((args.assets / 'scenarios' / name / 'metadata.json').read_text(encoding='utf-8'))
            prepare_theme(settings, metadata.get('preconditions', {}).get('theme'))
            print('BEGIN ' + name, flush=True)
            case_exports = args.exports
            if args.exports is not None and len(selected) > 1:
                case_exports = args.exports / output.name / name
                case_exports.mkdir(parents=True, exist_ok=False)
            result = run_case(args.assets, name, args.application, output / name, case_exports,
                              session if session is not None else JournalExecutionSession(), load_journal,
                              review_screenshots=True, environment_overrides=environment,
                              **(dict(capture_candidates=True) if args.capture_candidates else {}))
            summary['cases'].append(dict(id=name, status=result['status'], result=f'{name}/case_result.json'))
            update_run_summary(summary)
            write_summary(output, summary)
            print('END ' + name + ': ' + result['status'], flush=True)
            if progress:
                progress(dict(case=name, index=case_index, total=len(selected), phase='end', status=result['status']))
    except Exception as exc:
        summary.update(status='failed', error=str(exc))
    finally:
        try:
            restore_settings()
            summary['settingsRestored'] = settings is not None
        except OSError as exc:
            summary.update(status='failed', cleanupError=str(exc))
        write_summary(output, summary)
    return 0 if summary['status'] == 'passed' else 1 if summary['status'] in {'failed', 'blocked'} else 2


if __name__ == '__main__':
    sys.exit(main())
