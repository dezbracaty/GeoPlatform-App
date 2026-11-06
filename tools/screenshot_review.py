"""Batch-owned screenshot evidence, display-only differences and human review."""
import hashlib
import html
import json
from pathlib import Path
from urllib.parse import quote

from screenshot_comparison import comparison_images, raw_comparison_images
import uuid


WINDOW_SIZE = (1382, 954)


def checkpoint_declarations(document):
    rows = []
    for index, entry in enumerate(document['entries'], 1):
        payload = entry['payload']
        if entry['kind'] != 'verify.screenshot' and not (
                entry['kind'] == 'action' and payload.get('code') == 'screenshot.capture'):
            continue
        rows.append(dict(index=index, atMs=entry['atMs'], kind=entry['kind'],
                         label=payload.get('label') or payload.get('params', {}).get('filepath') or f'截图 {index}',
                         regions=payload.get('regions') or ([payload['region']] if 'region' in payload else None)))
    return rows


def snapshot_checkpoints(case, asset_root, verify_hashes=True):
    """Freeze standards before playback, including checkpoints never reached."""
    rows = checkpoint_declarations(case['document'])
    for row in rows:
        if row['kind'] != 'verify.screenshot':
            continue
        payload = case['document']['entries'][row['index'] - 1]['payload']
        try:
            baseline = (case['journal'].parent / payload['baseline']).resolve()
            relative = baseline.relative_to(asset_root).as_posix()
            frozen = baseline.read_bytes()
            digest = hashlib.sha256(frozen).hexdigest()
            if verify_hashes and case['hashes']['baselines'].get(relative) != digest:
                raise ValueError('Screenshot standard changed after preflight')
            row.update(baselineBytes=frozen, baselineSha256=digest)
        except Exception as exc:
            row['baselineError'] = str(exc)
    return rows


def collect_screenshots(checkpoints, execution, output):
    output = Path(output).resolve()
    rows = []
    steps = {step['index']: step for step in execution.get('results', [])}
    for checkpoint in checkpoints:
        row = {key: value for key, value in checkpoint.items() if key != 'baselineBytes'}
        destination = output / 'review' / f"checkpoint-{row['index']:04d}"
        destination.mkdir(parents=True, exist_ok=True)
        standard = row['kind'] == 'verify.screenshot'
        row.update(executionStatus='not_run', comparisonStatus='not_run', reviewStatus='not_applicable')
        if 'baselineBytes' in checkpoint:
            (destination / 'expected.png').write_bytes(checkpoint['baselineBytes'])
            row['expected'] = (destination / 'expected.png').relative_to(output).as_posix()
        if row.get('baselineError'):
            row.update(comparisonStatus='unavailable', message=row['baselineError'])
        step = steps.get(row['index'])
        if step:
            row['executionStatus'] = step['status']
            row['automaticStatus'] = step['status']
            evidence = step.get('evidence', [])
            actual = evidence[1] if standard and len(evidence) >= 2 else evidence[0] if not standard and evidence else None
            if actual and step['status'] in {'passed', 'failed'}:
                try:
                    source = (output / actual).resolve()
                    if not source.is_relative_to(output):
                        raise ValueError('Screenshot evidence escapes this run')
                    data = source.read_bytes()
                    (destination / 'actual.png').write_bytes(data)
                    row.update(actual=(destination / 'actual.png').relative_to(output).as_posix(),
                               actualSha256=hashlib.sha256(data).hexdigest())
                    if standard:
                        row.update(reviewStatus='pending')
                        try:
                            comparison = comparison_images(destination / 'expected.png', destination / 'actual.png',
                                                           destination, row.get('regions'))
                        except Exception as hint_error:
                            comparison = raw_comparison_images(destination / 'expected.png', destination / 'actual.png',
                                                               destination, row.get('regions'))
                            comparison['visualHintError'] = str(hint_error)
                        for key in ['expectedView', 'actualView', 'diff', 'rawDiff']:
                            if key in comparison:
                                comparison[key] = (destination / comparison[key]).relative_to(output).as_posix()
                        row.update(comparison)
                        if comparison['comparisonStatus'] == 'unavailable':
                            row['reviewStatus'] = 'not_applicable'
                    else:
                        row['comparisonStatus'] = 'diagnostic'
                except Exception as exc:
                    row.update(comparisonStatus='capture_error', reviewStatus='not_applicable', message=str(exc))
            else:
                row['message'] = step.get('message', 'Screenshot unavailable')
                if step['status'] == 'failed':
                    row['comparisonStatus'] = 'capture_error'
        else:
            row['message'] = execution.get('error') or '未执行到此检查点'
        rows.append(row)
    document = dict(schemaVersion=1, scope='Human visual review; not automatic acceptance', checkpoints=rows)
    path = output / 'review' / 'screenshots.json'
    path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    return dict(path=path.relative_to(output).as_posix(), checkpointCount=len(rows),
                pendingCount=sum(row['reviewStatus'] == 'pending' for row in rows),
                notRunCount=sum(row['executionStatus'] == 'not_run' for row in rows),
                captureErrorCount=sum(row['comparisonStatus'] == 'capture_error' for row in rows))


def write_batch_report(output, summary):
    """Write the portable batch manifest consumed by the native Python viewer."""
    output = Path(output).resolve()
    summary.setdefault('reviewRunId', uuid.uuid4().hex)
    results = {item['id']: item for item in summary['cases']}
    rows, cases = [], []
    for name in summary['plannedCases']:
        case = results.get(name, {})
        result_path = output / name / 'case_result.json'
        result = json.loads(result_path.read_text(encoding='utf8')) if result_path.is_file() else {}
        cases.append(dict(id=name, status=case.get('status', 'not_run'),
                          executionStatus=result.get('executionStatus', 'not_started'),
                          result=f'{name}/case_result.json' if result else None,
                          message=result.get('error', ''), methodResults=result.get('methodResults', [])))
        review = result.get('screenshotReview')
        if review:
            path = (output / name / review['path']).resolve()
            if not path.is_relative_to(output):
                raise ValueError('Screenshot review manifest escapes the batch directory')
            checkpoints = json.loads(path.read_text(encoding='utf8'))['checkpoints']
        else:
            checkpoints = [dict(item, executionStatus='not_run', comparisonStatus='not_run',
                                reviewStatus='not_applicable', message=result.get('error') or '场景未执行')
                           for item in summary.get('screenshotCheckpoints', {}).get(name, [])]
        for checkpoint in checkpoints:
            row = dict(checkpoint, case=name)
            row['id'] = hashlib.sha256(json.dumps([name, row['index'], row.get('baselineSha256'),
                row.get('actualSha256')], separators=(',', ':')).encode()).hexdigest()
            for key in ('expected', 'actual', 'expectedView', 'actualView', 'diff', 'rawDiff'):
                if row.get(key):
                    path = (output / name / row[key]).resolve()
                    if not path.is_relative_to(output):
                        raise ValueError('Screenshot image escapes the batch directory')
                    row[key] = path.relative_to(output).as_posix()
            rows.append(row)
    pending = sum(row['reviewStatus'] == 'pending' for row in rows)
    summary['screenshotReviewPendingCount'] = pending
    summary['screenshotReviewManifest'] = 'screenshot_review.json'
    manifest = dict(schemaVersion=1, scope='batch-screenshot-review', runId=summary['reviewRunId'],
                    status=summary['status'], error=summary.get('error') or summary.get('cleanupError'),
                    cases=cases, checkpoints=rows, pendingCount=pending)
    (output / 'screenshot_review.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    # Keep the existing portable HTML summary as an evidence index, not another reviewer UI.
    escape = html.escape
    links = ''.join('<tr><td>{}</td><td>{}</td><td>{}</td></tr>'.format(escape(case['id']),
        escape(case['status']), '<a href="{}/case_report.html">场景报告</a>'.format(quote(case['id']))
        if case['result'] else '未执行') for case in cases)
    images = ''.join('<tr class="checkpoint"><td>{} / #{} / {}</td><td>{}</td><td>{}</td></tr>'.format(
        escape(row['case']), row['index'], escape(row['label']), escape(row['executionStatus']),
        escape(row.get('message', ''))) for row in rows)
    body = ('<!doctype html><meta charset="utf-8"><title>批量回归结果</title>'
            '<style>body{{font:14px system-ui;margin:24px}}td,th{{padding:8px;text-align:left}}'
            'table{{border-collapse:collapse}}tr{{border-bottom:1px solid #ddd}}</style>'
            '<h1>批量回归：{}</h1><p>截图待审核：{}。使用 Python 回归结果窗口查看逐图对照与审核记录。</p>'
            '<p><a href="summary.json">执行汇总</a> · <a href="screenshot_review.json">截图对照清单</a></p>'
            '<h2>场景执行结果</h2><table>{}</table><h2>截图检查点</h2><table>{}</table>').format(
                escape(summary['status']), pending, links, images)
    (output / 'report.html').write_text(body, encoding='utf8')
