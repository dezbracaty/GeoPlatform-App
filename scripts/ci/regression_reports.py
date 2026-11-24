"""Keep completed CI summaries and evidence needed to investigate failures."""
from collections import defaultdict
import json
from pathlib import Path
import re
import sys

from common import remove_owned_tree


FINAL_STATUSES = ('passed', 'failed', 'blocked', 'not_verified')
ATTEMPT_STAGES = ('initial', 'serial-recheck', 'serial-recheck-2')
REVIEW_IMAGES = ('actual.png', 'expected.png', 'raw-diff.png', 'diff.png',
                 'actual-view.png', 'expected-view.png')


def read_summary(path):
    if not path.is_file() or path.is_symlink() or path.is_junction() or path.resolve() != path:
        return {}
    value = json.loads(path.read_text(encoding='utf-8'))
    return value if isinstance(value, dict) else {}


def completed_journal(output):
    run = output / 'journals/run'
    if not run.is_dir() or run.resolve() != run or run.is_junction():
        return None
    paths = [p for p in [run / 'summary.json', *run.glob('session_*/summary.json')]
             if p.is_file()]
    if len(paths) != 1:
        return None
    path = paths[0]
    summary = read_summary(path)
    if (summary.get('status') not in FINAL_STATUSES or summary.get('stage') not in (None, 'finished') or
            summary.get('runComplete') is not True or summary.get('cancelled') or summary.get('cleanupError') or
            summary.get('candidateCollection') or summary.get('screenshotReviewPendingCount', 0) != 0):
        return None
    planned, cases = summary.get('plannedCases'), summary.get('cases')
    if not isinstance(planned, list) or not planned or not isinstance(cases, list):
        return None
    if not all(isinstance(name, str) and re.fullmatch(r'[a-z0-9][a-z0-9_-]*', name) for name in planned):
        return None
    statuses = {case.get('id'): case.get('status') for case in cases
                if isinstance(case, dict) and isinstance(case.get('id'), str)}
    if (len(planned) != len(cases) or len(statuses) != len(cases) or
            set(statuses) != set(planned) or
            summary.get('completedCaseCount') != len(planned) or
            any(status not in FINAL_STATUSES for status in statuses.values())):
        return None
    return path, summary


def discard(path):
    if not path.exists() and not path.is_symlink() and not path.is_junction():
        return
    if path.resolve() != path or path.is_symlink() or path.is_junction():
        raise RuntimeError(f'Refusing to prune redirected report data: {path}')
    if path.is_dir():
        remove_owned_tree(path, path.parent)
    else:
        path.unlink(missing_ok=True)


def owned_file(directory, relative):
    """Use only portable, unredirected file references inside this attempt."""
    if not isinstance(relative, str) or not relative or '\\' in relative:
        return None
    path = Path(relative)
    if path.anchor or '..' in path.parts:
        return None
    path = directory / path
    if path.resolve() == path and not path.is_symlink() and not path.is_junction() and path.is_file():
        return path
    return None


def screenshot_steps(steps):
    return {step['index']: step for step in steps if
            (step.get('kind') == 'verify.screenshot' or step.get('actionCode') == 'screenshot.capture') and
            type(step.get('index')) is int and step['index'] > 0}


def case_attempts(case):
    """Limit evidence lookup to the recorded initial run and its serial rechecks."""
    name = case['id']
    if 'attempts' not in case:
        return [Path(name)] if case.get('result', f'{name}/case_result.json') == f'{name}/case_result.json' else []
    attempts = case['attempts']
    if not isinstance(attempts, list) or not attempts or len(attempts) > len(ATTEMPT_STAGES):
        return []
    directories = []
    for stage, attempt in zip(ATTEMPT_STAGES, attempts):
        directory = Path(name) if stage == 'initial' else Path(stage) / name
        if (not isinstance(attempt, dict) or attempt.get('stage') != stage or
                attempt.get('result') != (directory / 'case_result.json').as_posix()):
            return []
        directories.append(directory)
    if case.get('result') != attempts[-1]['result'] or case['status'] != attempts[-1].get('status'):
        return []
    return directories


def checkpoint_rows(rows):
    if not isinstance(rows, list):
        return {}
    return {row['index']: row for row in rows if isinstance(row, dict) and
            type(row.get('index')) is int and row['index'] > 0}


def batch_review_rows(summary_path, summary):
    """Use an earlier index only when it identifies this batch and attempt."""
    run_id = summary.get('reviewRunId')
    if not isinstance(run_id, str) or summary.get('screenshotReviewManifest') != 'screenshot_review.json':
        return {}
    try:
        manifest = read_summary(summary_path.with_name('screenshot_review.json'))
    except (OSError, ValueError):
        return {}
    if (manifest.get('schemaVersion') != 1 or manifest.get('runId') != run_id or
            manifest.get('scope') != 'batch-screenshot-review' or
            not isinstance(manifest.get('checkpoints'), list)):
        return {}
    by_attempt = defaultdict(list)
    for row in manifest['checkpoints']:
        if isinstance(row, dict) and isinstance(row.get('case'), str):
            stage = row.get('attemptStage', 'initial')
            if stage in ATTEMPT_STAGES:
                by_attempt[row['case'], stage].append(row)
    return by_attempt


def prune_screenshots(directory, declarations, batch_rows):
    result = read_summary(directory / 'case_result.json')
    if (result.get('id') != directory.name or result.get('status') not in FINAL_STATUSES or
            'executionResultPath' not in result):
        return
    execution_path = result['executionResultPath']
    if execution_path is None:
        steps = {}
    else:
        if execution_path != 'execution_result.json':
            return
        execution = read_summary(directory / execution_path)
        if (not isinstance(execution.get('results'), list) or execution.get('acceptance') == 'pending' or
                any(not isinstance(step, dict) or step.get('status') == 'pending' for step in execution['results'])):
            return
        steps = screenshot_steps(execution['results'])

    # The per-attempt manifest takes precedence over a possibly stale index.
    try:
        review = read_summary(directory / 'review/screenshots.json').get('checkpoints', [])
    except (OSError, ValueError):
        review = []
    rows = checkpoint_rows(batch_rows)
    rows.update(checkpoint_rows(review))
    candidates, keep = set(), set()
    for index in checkpoint_rows(declarations).keys() | steps.keys() | rows.keys():
        step = steps.get(index, {})
        row = rows.get(index, {})
        evidence = step.get('evidence', [])
        comparison = step.get('comparison', {})
        if not isinstance(evidence, list) or len(evidence) > 3 or not isinstance(comparison, dict):
            return
        # The executor writes [actual] or [expected, actual, diff?].
        actual = evidence[1 if len(evidence) >= 2 else 0] if evidence else None
        review_dir = f'review/checkpoint-{index:04d}'
        stem = f'screenshots/screenshot_{index:03d}'
        images = {path for value in (
            *evidence, f'{stem}_expected.png', f'{stem}_diff.png',
            *(f'{review_dir}/{name}' for name in REVIEW_IMAGES),
        ) if (path := owned_file(directory, value)) is not None}
        candidates.update(images)
        failed = (step.get('status') == 'failed' or comparison.get('status') == 'failed' or
                  row.get('automaticStatus') == 'failed' or row.get('executionStatus') == 'failed' or
                  row.get('comparisonStatus') == 'capture_error')
        if failed and any(path.stat().st_size for value in (actual, f'{review_dir}/actual.png')
                          if (path := owned_file(directory, value)) is not None):
            keep.update(images)
    for path in sorted(candidates - keep):
        discard(path)


def compact_regression_report(output):
    """Prune one finished platform attempt after its result is written to disk."""
    output = Path(output).absolute()
    if output.resolve() != output or output.is_junction():
        raise RuntimeError(f'Refusing to prune redirected report directory: {output}')
    report = read_summary(output / 'source-regression.json') or read_summary(output / 'summary.json')
    if report.get('status') not in ('passed', 'failed') or report.get('interrupted'):
        return
    journal = completed_journal(output)
    if journal is None:
        return
    summary_path, summary = journal
    run = output / 'journals/run'
    passed = {case['id'] for case in summary['cases'] if case['status'] == 'passed'}
    exports = run.parent / 'exports'
    if summary_path.parent != run:
        exports /= summary_path.parent.name

    if report['status'] == 'passed' and (summary['status'] != 'passed' or len(passed) != len(summary['plannedCases'])):
        return

    # Recoveries count as passed. For a final failure, judge each recorded
    # attempt independently, leaving JSON, logs and non-screenshot artifacts.
    for case in summary['cases']:
        if case['id'] in passed:
            paths = case_attempts(case) if 'attempts' in case else [
                Path(stage) / case['id'] for stage in ('', *ATTEMPT_STAGES[1:], '.parallel-failures')]
            for path in paths:
                discard(summary_path.parent / path)
                discard(exports / path)

    if report['status'] == 'passed' and not summary.get('screenshotReviewError'):
        # Keep the summaries at the same paths the workflow uploads.
        for name in ('sdk-build.log', 'sdk-consumer.log', 'sdk-package', 'consumer-receipt.json',
                     'journal-receipt.json', 'arm64', 'x86_64', 'x64'):
            discard(output / name)
        for name in ('regression.log', 'regression-inputs.json', 'server-state.json'):
            discard(run.parent / name)
        for name in ('report.html', 'screenshot_review.json'):
            discard(summary_path.parent / name)
        for stage in ('', *ATTEMPT_STAGES[1:]):
            discard(summary_path.parent / stage / '.declarations')
        return

    declarations = summary.get('screenshotCheckpoints', {})
    if not isinstance(declarations, dict):
        declarations = {}
    review_rows = batch_review_rows(summary_path, summary)
    for case in summary['cases']:
        if case['id'] not in passed:
            for attempt in case_attempts(case):
                try:
                    stage = attempt.parent.name or 'initial'
                    prune_screenshots(summary_path.parent / attempt, declarations.get(case['id'], []),
                                      review_rows.get((case['id'], stage), []))
                except (OSError, ValueError) as error:
                    print(f'Could not read screenshot evidence for {attempt}: {error}', file=sys.stderr)
    # Older attempts recorded the two Orca suites as separate phases.
    for phase in report.get('phases', []):
        match = re.fullmatch(r'(arm64|x86_64|x64)/(ctest|original48|extended26)', phase.get('name', ''))
        if phase.get('status') == 'passed' and match:
            arch, test = match.groups()
            discard(output / arch / ('orca-gcode' if test == 'ctest' else test))
