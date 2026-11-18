#!/usr/bin/env python3
"""Run versioned App acceptance assets through JournalSystem and business checks."""
import argparse
import configparser
import json
import os
from pathlib import Path
import sys
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, FIRST_COMPLETED, wait

from acceptance import application_dependencies, digest, load_case, run_case, refresh_case_comparisons, write_case_result, write_summary
from verification_methods import METHODS, SUPPLEMENTAL_VALIDATORS, VALIDATORS
from screenshot_review import checkpoint_declarations, collect_screenshots


DEFAULT_PARALLELISM = 3


def validate_parallelism(value):
    if type(value) is not int or value < 1:
        raise ValueError('并行数量必须为正整数')
    return value


def regression_environment(*, background=False):
    """Keep parallel Mac children in the background; serial replay uses native focus."""
    if sys.platform == 'win32':
        return dict(QT_ENABLE_HIGHDPI_SCALING='0', QT_SCALE_FACTOR='1', QT_FONT_DPI='96')
    if sys.platform == 'darwin':
        return dict(QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM='1' if background else '0')
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
                         'pending' if 'pending' in statuses else
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
    value = '0' if theme == 'light' else '1'
    if config.get('General', 'darkMode', fallback=None) == value:
        return
    if not config.has_section('General'):
        config.add_section('General')
    config.set('General', 'darkMode', value)
    settings.parent.mkdir(parents=True, exist_ok=True)
    with settings.open('w', encoding='utf-8') as stream:
        config.write(stream)



def main(argv=None, *, session=None, session_factory=None, progress=None, between_cases=None, pause_requested=None):
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
    parser.add_argument('--jobs', type=int, default=1,
                        help='Maximum simultaneous independent App instances')
    parser.add_argument('--recheck-failed-serial', action='store_true',
                        help='Re-run failed scenarios once serially after all initial comparisons finish')
    parser.add_argument('--capture-candidates', action='store_true',
                        help='Capture DB/screenshots at existing checkpoints for review; never adopts standards or passes formal acceptance')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--exports', type=Path, help='Actual App export directory; files must be generated in this run')
    parser.add_argument('--settings-file', type=Path, help='Actual App INI for declared theme preparation (Windows/macOS default is detected)')
    args = parser.parse_args(argv)
    try:
        validate_parallelism(args.jobs)
    except ValueError as exc:
        parser.error(str(exc))
    if args.jobs > 1 and session is not None and session_factory is None:
        parser.error('Parallel replay requires an independent session for each case')
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
    from journal_runner import ScreenshotComparisons
    from session_repository import JournalSessionRepository, discover_scenarios
    try:
        names = [directory.name for directory in discover_scenarios(args.assets / 'scenarios')]
    except ValueError as exc:
        parser.error(str(exc))
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
    environment = regression_environment(background=args.jobs > 1)
    if environment is not None:
        summary['environmentOverrides'] = environment
    update_run_summary(summary)
    summary.update(parallelism=args.jobs, stage='initial', serialRecheck=args.recheck_failed_serial,
                   applicationSha256=None)
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
    reviews = ThreadPoolExecutor(max_workers=1, thread_name_prefix='journal-review')
    comparisons = ScreenshotComparisons()
    pending_reviews = []
    waiting_reviews = []
    finished_outputs = set()

    def submit_review(checkpoints, execution, case_output):
        waiting_reviews.append((checkpoints, execution, case_output))

    def finish_comparisons(changed):
        for case in summary['cases']:
            for attempt in case.get('attempts', [case]):
                case_output = (output / attempt['result']).parent
                if str(case_output) in changed:
                    attempt['status'] = refresh_case_comparisons(case_output)['status']
            case['status'] = case['attempts'][-1]['status']
            case['outcome'] = ('passed_after_recheck' if len(case['attempts']) > 1 and
                               case['status'] == 'passed' else case['status'])
        update_run_summary(summary)

    def finish_reviews():
        changed = False
        for request in waiting_reviews[:]:
            checkpoints, execution, case_output = request
            if any(step.get('status') == 'pending' for step in execution.get('results', [])):
                continue
            pending_reviews.append((case_output, reviews.submit(
                collect_screenshots, checkpoints, execution, case_output)))
            waiting_reviews.remove(request)
        for case_output, future in pending_reviews[:]:
            if not future.done():
                continue
            result = json.loads((case_output / 'case_result.json').read_text(encoding='utf-8'))
            try:
                result['screenshotReview'] = future.result()
                result['reviewStatus'] = 'pending' if result['screenshotReview']['pendingCount'] else 'not_applicable'
            except Exception as exc:
                result.update(reviewStatus='failed', screenshotReviewError=str(exc))
            checks = json.loads((case_output / 'checks.json').read_text(encoding='utf-8'))
            write_case_result(case_output, result, checks)
            pending_reviews.remove((case_output, future))
            changed = True
        return changed

    def refresh_pending():
        changed = comparisons.collect(outputs=finished_outputs)
        finish_comparisons(changed)
        reviews_changed = finish_reviews()
        if changed or reviews_changed:
            write_summary(output, summary)

    def execute_case(case_index, name, stage, jobs):
        if session is not None and getattr(session, 'cancel_event', None) is not None and session.cancel_event.is_set():
            raise InterruptedError('Replay cancelled by caller')
        if progress:
            progress(dict(case=name, index=case_index, total=len(selected), phase='begin', stage=stage))
        print('BEGIN ' + name, flush=True)
        case_output = output / name if stage == 'initial' else output / 'serial-recheck' / name
        case_exports = args.exports
        if args.exports is not None and (len(selected) > 1 or args.recheck_failed_serial):
            case_exports = (args.exports / output.name / name if stage == 'initial' else
                            args.exports / output.name / 'serial-recheck' / name)
            case_exports.mkdir(parents=True, exist_ok=False)
        instance = session_factory(name) if session_factory else session if session is not None else JournalExecutionSession()
        return run_case(args.assets, name, args.application, case_output, case_exports,
                          instance, load_journal,
                          review_screenshots=True, environment_overrides=regression_environment(background=jobs > 1),
                          review_submit=submit_review, comparisons=comparisons, batch_report=output / 'report.html',
                          **(dict(capture_candidates=True) if args.capture_candidates else {}))

    def check_cancelled():
        if session is not None and getattr(session, 'cancel_event', None) is not None and session.cancel_event.is_set():
            summary['cancelled'] = True
            raise InterruptedError('Replay cancelled by caller')

    def case_theme(name):
        try:
            metadata = json.loads((args.assets / 'scenarios' / name / 'metadata.json').read_text(encoding='utf-8'))
            return metadata.get('preconditions', {}).get('theme')
        except (OSError, ValueError, TypeError):
            return None  # run_case records preparation blockers.

    def check_cleanup():
        for case in summary['cases']:
            path = (output / case['result']).parent / 'execution_result.json'
            execution = json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
            if execution.get('cleanupErrors'):
                summary['cleanupError'] = '; '.join(execution['cleanupErrors'])
                raise RuntimeError(summary['cleanupError'])

    def run_stage(names, jobs, stage):
        nonlocal comparisons
        summary['stage'] = stage
        write_summary(output, summary)
        index = 0
        futures = {}
        errors = []
        theme = None

        def cancelled():
            return session is not None and getattr(session, 'cancel_event', None) is not None and session.cancel_event.is_set()

        with ThreadPoolExecutor(max_workers=jobs, thread_name_prefix='journal-replay') as replays:
            while index < len(names) or futures:
                drained = not futures
                if drained:
                    if errors:
                        raise errors[0]
                    check_cancelled()
                    if index >= len(names):
                        break
                    if index and between_cases is not None:
                        between_cases(refresh_pending)
                        check_cancelled()
                    theme = case_theme(names[index])
                    prepare_theme(settings, theme)
                # Refill immediately within the same shared theme. A pause
                # request stops refilling, then waits for active peers to drain.
                pause_pending = (not drained and between_cases is not None and
                                 (pause_requested is None or pause_requested()))
                while (not errors and not cancelled() and not pause_pending and
                       len(futures) < jobs and index < len(names) and case_theme(names[index]) == theme):
                    name = names[index]
                    if stage == 'serial-recheck':
                        first = next(c for c in summary['cases'] if c['id'] == name)['attempts'][0]
                        original = json.loads((output / first['result']).read_text(encoding='utf-8'))
                        if (digest(args.application) != summary['applicationSha256'] or
                                sorted(application_dependencies(args.application), key=lambda d: d['path']) !=
                                sorted(original['provenance']['deployedDependencies'], key=lambda d: d['path']) or
                                load_case(args.assets, name, load_journal)['hashes'] != original['provenance']['inputs']):
                            raise RuntimeError('Program or scenario inputs changed before serial recheck: ' + name)
                    futures[replays.submit(execute_case, selected.index(name) + 1, name, stage, jobs)] = name
                    index += 1
                if not futures:
                    check_cancelled()
                    continue
                completed, _ = wait(futures, return_when=FIRST_COMPLETED)
                for future in completed:
                    name = futures.pop(future)
                    try:
                        result = future.result()
                    except Exception as exc:
                        errors.append(exc)
                        continue  # Drain peers and retain their completed evidence.
                    relative = f'{name}/case_result.json' if stage == 'initial' else f'serial-recheck/{name}/case_result.json'
                    attempt = dict(stage=stage, parallelism=jobs, status=result['status'], result=relative)
                    case = next((c for c in summary['cases'] if c['id'] == name), None)
                    if case is None:
                        case = dict(id=name, attempts=[])
                        summary['cases'].append(case)
                    case['attempts'].append(attempt)
                    case.update(status=result['status'], result=relative)
                    finished_outputs.add(str((output / relative).parent.resolve()))
                    summary['cases'].sort(key=lambda c: selected.index(c['id']))
                    update_run_summary(summary)
                    finish_reviews()
                    write_summary(output, summary)
                    print('END ' + stage + ' ' + name + ': ' + result['status'], flush=True)
                    if progress:
                        progress(dict(case=name, index=selected.index(name) + 1, total=len(selected),
                                      phase='end', status=result['status'], stage=stage))
                try:
                    check_cleanup()
                except Exception as exc:
                    errors.append(exc)
                # Finished scenes can be finalized while other writers replay;
                # their output identities prevent mutating a live execution.
                finish_comparisons(comparisons.collect(outputs=finished_outputs))
                finish_reviews()
                write_summary(output, summary)
        if errors:
            raise errors[0]
        # Failure selection must see completed pixel checks, not pending results.
        finish_comparisons(comparisons.close())
        finish_reviews()
        wait([future for _, future in pending_reviews])
        finish_reviews()
        write_summary(output, summary)
        check_cleanup()
        check_cancelled()

    try:
        try:
            summary['applicationSha256'] = digest(args.application)
        except OSError:
            pass  # run_case records missing/unreadable programs as preparation blockers.
        run_stage(selected, args.jobs, 'initial')
        failed = [c['id'] for c in summary['cases'] if c['status'] == 'failed']
        summary['initialFailedCases'] = failed
        if args.recheck_failed_serial and failed and not args.capture_candidates:
            if between_cases is not None:
                between_cases(refresh_pending)
            check_cancelled()
            comparisons = ScreenshotComparisons()
            run_stage(failed, 1, 'serial-recheck')
        summary['stage'] = 'finished'
    except Exception as exc:
        summary.update(status='failed', error=str(exc))
        if isinstance(exc, InterruptedError) or (session is not None and
                getattr(session, 'cancel_event', None) is not None and session.cancel_event.is_set()):
            summary['cancelled'] = True
    finally:
        try:
            finish_comparisons(comparisons.close())
            finish_reviews()
            reviews.shutdown(wait=True)
            finish_reviews()
        except Exception as exc:
            summary.update(status='failed', screenshotReviewError=str(exc))
        finally:
            reviews.shutdown(wait=True)
        if summary.get('error') or summary.get('cleanupError'):
            summary['status'] = 'failed'
        summary['recheckPassedCount'] = sum(c.get('outcome') == 'passed_after_recheck' for c in summary['cases'])
        write_summary(output, summary)
        try:
            JournalSessionRepository(playbacks=JournalSessionRepository.default_paths(args.application)['playbacks']).remember_result(output)
        except OSError as exc:
            print(f'无法记录最近结果入口：{exc}', file=sys.stderr)
    return 0 if summary['status'] == 'passed' else 1 if summary['status'] in {'failed', 'blocked'} else 2


if __name__ == '__main__':
    sys.exit(main())
