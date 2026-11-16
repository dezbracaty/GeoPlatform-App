#!/usr/bin/env python3
"""Run versioned App acceptance assets through JournalSystem and business checks."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import threading
from datetime import datetime
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait

from acceptance import load_case, run_case, refresh_case_comparisons, write_case_result, write_summary
from verification_methods import METHODS, SUPPLEMENTAL_VALIDATORS, VALIDATORS
from screenshot_review import checkpoint_declarations, collect_screenshots


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
                         'pending' if 'pending' in statuses else
                         'not_verified' if not summary['runComplete'] or 'not_verified' in statuses else 'passed')


def main(argv=None, *, session=None, session_factory=None, cancel_event=None,
         progress=None, between_cases=None, pause_requested=None):
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
    parser.add_argument('--jobs', type=int, default=3, help='Concurrent App instances; default 3')
    parser.add_argument('--capture-candidates', action='store_true',
                        help='Capture DB/screenshots at existing checkpoints for review; never adopts standards or passes formal acceptance')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--exports', type=Path, help='Actual App export directory; files must be generated in this run')
    parser.add_argument('--settings-file', type=Path, help='App INI to restore after the entire batch')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
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
    workers = min(args.jobs, len(selected)) if session is None else 1
    cancel_event = cancel_event or getattr(session, 'cancel_event', None) or threading.Event()
    summary = dict(schemaVersion=1, status='not_verified', plannedCases=selected, cases=[],
                   parallelWorkers=workers, runningCases=[], serialRetriedCases=[],
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
    reviews = ThreadPoolExecutor(max_workers=1, thread_name_prefix='journal-review')
    comparisons = {}
    pending_reviews = []
    waiting_reviews = []

    def finish_comparisons(changed):
        for case in summary['cases']:
            case_output = output / case['id']
            if str(case_output) in changed:
                case['status'] = refresh_case_comparisons(case_output)['status']
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
        changed = set()
        for comparison in comparisons.values():
            changed.update(comparison.collect())
        finish_comparisons(changed)
        reviews_changed = finish_reviews()
        if changed or reviews_changed:
            write_summary(output, summary)

    def notify(name, phase, *, status=None, attempt=None):
        attempt = attempt or ('parallel' if workers > 1 else 'serial')
        print(('BEGIN ' if phase == 'begin' else 'END ') + name +
              (': ' + status if status else '') + ' [' + attempt + ']', flush=True)
        if progress:
            event = dict(case=name, index=selected.index(name) + 1, total=len(selected), phase=phase,
                         completedCount=len(summary['cases']), activeCases=summary['runningCases'][:], attempt=attempt)
            if status is not None:
                event['status'] = status
            progress(event)

    def execute(name, destination, *, retry=False):
        case_exports = args.exports
        if args.exports is not None and (len(selected) > 1 or retry):
            case_exports = args.exports / output.name
            if retry:
                case_exports /= '.serial-retry'
            case_exports /= name
            case_exports.mkdir(parents=True, exist_ok=False)
        replay = (session_factory(name) if session_factory else session if session is not None else
                  JournalExecutionSession(cancel_event=cancel_event))
        comparison = ScreenshotComparisons()
        review = []
        try:
            result = run_case(args.assets, name, args.application, destination, case_exports, replay, load_journal,
                              review_screenshots=True, environment_overrides=environment,
                              review_submit=lambda *request: review.append(request), comparisons=comparison,
                              **(dict(capture_candidates=True) if args.capture_candidates else {}))
        finally:
            # Replay is over; pixel workers may finish while the next App starts.
            changed = comparison.close(wait=False)
        return result, comparison, review, changed

    def drain_evidence():
        changed = set()
        for comparison in comparisons.values():
            changed.update(comparison.close())
        finish_comparisons(changed)
        finish_reviews()
        for _, future in pending_reviews:
            try:
                future.result()
            except Exception:
                pass  # finish_reviews records the diagnostic on its owning case.
        finish_reviews()

    try:
        next_index = 0
        active = {}
        write_summary(output, summary)
        with ThreadPoolExecutor(max_workers=workers, thread_name_prefix='journal-replay') as replays:
            try:
                while active or next_index < len(selected):
                    if cancel_event.is_set():
                        summary['cancelled'] = True
                        if not active:
                            break
                    should_pause = pause_requested is not None and pause_requested()
                    if not active and between_cases is not None and next_index and next_index < len(selected):
                        between_cases(refresh_pending)
                        should_pause = False
                    while (next_index < len(selected) and len(active) < workers
                           and not cancel_event.is_set() and not should_pause):
                        name = selected[next_index]
                        next_index += 1
                        summary['runningCases'].append(name)
                        notify(name, 'begin')
                        active[replays.submit(execute, name, output / name)] = name
                    if not active:
                        break
                    done, _ = wait(active, timeout=.1, return_when=FIRST_COMPLETED)
                    for future in sorted(done, key=lambda value: selected.index(active[value])):
                        name = active.pop(future)
                        result, comparison, review, changed = future.result()
                        comparisons[name] = comparison
                        waiting_reviews.extend(review)
                        summary['runningCases'].remove(name)
                        summary['cases'].append(dict(id=name, status=result['status'], result=f'{name}/case_result.json'))
                        summary['cases'].sort(key=lambda row: selected.index(row['id']))
                        finish_comparisons(changed)
                        refresh_pending()
                        write_summary(output, summary)
                        status = next(row['status'] for row in summary['cases'] if row['id'] == name)
                        notify(name, 'end', status=status)
            except BaseException:
                cancel_event.set()
                raise
        drain_evidence()
        # Retry only after every parallel App and its image work have finished.
        for row in summary['cases']:
            execution = output / row['id'] / 'execution_result.json'
            if execution.is_file() and json.loads(execution.read_text(encoding='utf-8')).get('applicationKeptOpen'):
                raise RuntimeError('An owned test application is still open; serial retry cannot start')
        failed = [row for row in summary['cases'] if row['status'] == 'failed'] if workers > 1 else []
        for row in failed:
            if cancel_event.is_set():
                summary['cancelled'] = True
                break
            if between_cases is not None and pause_requested is not None and pause_requested():
                between_cases(refresh_pending)
                if cancel_event.is_set():
                    summary['cancelled'] = True
                    break
            name = row['id']
            summary['runningCases'] = [name]
            notify(name, 'begin', attempt='serial-retry')
            retry_output = output / '.serial-retry' / name
            result, comparison, review, _ = execute(name, retry_output, retry=True)
            comparison.close()
            if result.get('executionResultPath'):
                result = refresh_case_comparisons(retry_output)
            waiting_reviews.extend(review)
            finish_reviews()
            for directory, future in pending_reviews:
                if directory == retry_output:
                    try:
                        future.result()
                    except Exception:
                        pass
            finish_reviews()
            result = json.loads((retry_output / 'case_result.json').read_text(encoding='utf-8'))
            archived = output / '.parallel-failures' / name
            archived.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(output / name, archived)
            shutil.move(retry_output, output / name)
            attempts = [dict(mode='parallel', status=row['status'],
                             result=f'.parallel-failures/{name}/case_result.json'),
                        dict(mode='serial', status=result['status'], result=f'{name}/case_result.json')]
            row.update(status=result['status'], attempts=attempts)
            result['attempts'] = attempts
            write_case_result(output / name, result,
                              json.loads((output / name / 'checks.json').read_text(encoding='utf-8')))
            summary['serialRetriedCases'].append(name)
            summary['runningCases'] = []
            update_run_summary(summary)
            write_summary(output, summary)
            notify(name, 'end', status=row['status'], attempt='serial-retry')
        if cancel_event.is_set():
            summary['cancelled'] = True
            raise InterruptedError('Replay cancelled by caller')
    except KeyboardInterrupt:
        cancel_event.set()
        summary.update(status='failed', cancelled=True, error='Replay cancelled by caller')
    except Exception as exc:
        summary.update(status='failed', error=str(exc))
    finally:
        try:
            restore_settings()
            summary['settingsRestored'] = settings is not None
        except OSError as exc:
            summary.update(status='failed', cleanupError=str(exc))
        try:
            drain_evidence()
            finish_reviews()
            reviews.shutdown(wait=True)
            finish_reviews()
        except Exception as exc:
            summary.update(status='failed', screenshotReviewError=str(exc))
        finally:
            reviews.shutdown(wait=True)
        if summary.get('error') or summary.get('cleanupError'):
            summary['status'] = 'failed'
        write_summary(output, summary)
        try:
            JournalSessionRepository(playbacks=JournalSessionRepository.default_paths(args.application)['playbacks']).remember_result(output)
        except OSError as exc:
            print(f'无法记录最近结果入口：{exc}', file=sys.stderr)
    return 0 if summary['status'] == 'passed' else 1 if summary['status'] in {'failed', 'blocked'} else 2


if __name__ == '__main__':
    sys.exit(main())
