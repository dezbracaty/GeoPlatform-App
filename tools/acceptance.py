"""Asset contracts and business acceptance around the maintained Journal session.

No App RPCs, input scheduling or Journal acceptance rules are implemented here.
"""
import hashlib
import html
import inspect
import json
import re
import shutil
import subprocess
import time
from pathlib import Path

from acceptance_checks import validate_expectations, verify_job


class ContractError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ContractError(message)


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2, default=str) + '\n', encoding='utf8')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def inside(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative and ':' not in relative,
            f'Expected portable relative path: {relative!r}')
    require(not Path(relative).is_absolute(), f'Absolute asset path: {relative}')
    result = (Path(root) / relative).resolve()
    require(result.is_relative_to(Path(root).resolve()), f'Asset path escapes its repository: {relative}')
    return result


def load_catalog(root):
    root = Path(root).resolve()
    catalog = read_json(root / 'catalog.json')
    require(type(catalog['schemaVersion']) is int and catalog['schemaVersion'] == 1, 'Unsupported asset catalog version')
    require(catalog['scenarioRoot'] == 'scenarios', 'Unsupported scenario layout')
    models = {}
    for source in catalog['modelCatalogs']:
        path = inside(root, source['path'])
        manifest = read_json(path)
        for item in manifest['fixtures']:
            identifier = source['namespace'] + '/' + item['id']
            require(identifier not in models, f'Duplicate model ID: {identifier}')
            require(re.fullmatch('[0-9a-f]{64}', item['sha256']) is not None, f'Invalid model hash: {identifier}')
            require(type(item['byteCount']) is int and item['byteCount'] >= 0, f'Invalid byte count: {identifier}')
            model = (path.parent / item['path']).resolve()
            require(model.is_relative_to(root), f'Model escapes asset repository: {identifier}')
            models[identifier] = dict(item, absolutePath=model)
    return models


def load_case(root, name, journal_loader):
    root = Path(root).resolve()
    require(re.fullmatch('[a-z0-9][a-z0-9_-]*', name) is not None, f'Invalid scenario ID: {name}')
    directory = inside(root, 'scenarios/' + name)
    metadata_path = directory / 'metadata.json'
    metadata = read_json(metadata_path)
    require(type(metadata.get('schemaVersion')) is int and metadata['schemaVersion'] == 1 and metadata.get('id') == name, 'Invalid scenario identity/version')
    for key in ['name', 'testPurpose', 'scope']:
        require(isinstance(metadata.get(key), str) and metadata[key].strip(), f'Missing {key}')
    require(isinstance(metadata.get('inputs'), list), 'inputs must be an array')
    require(isinstance(metadata.get('unverifiedScopes'), list) and all(isinstance(s, str) for s in metadata['unverifiedScopes']),
            'unverifiedScopes must be a string array')
    journal = directory / 'events/Journal.json'
    document = journal_loader(journal)
    models = load_catalog(root)
    inputs, declared_paths, input_hashes = metadata.get('inputs', []), set(), {}
    for item in inputs:
        if 'modelId' in item:
            require(set(item) == {'modelId'} and isinstance(item['modelId'], str), 'Invalid model input')
            require(item['modelId'] in models, f'Unknown model ID: {item["modelId"]}')
            model = models[item['modelId']]
            path = model['absolutePath']
            require(path.is_file(), f'Missing model: {path}')
            with path.open('rb') as stream:
                require(not stream.read(100).startswith(b'version https://git-lfs.github.com/spec/'), f'Unmaterialized LFS model: {path}')
            actual = digest(path)
            require(path.stat().st_size == model['byteCount'] and actual == model['sha256'], f'Model integrity mismatch: {item["modelId"]}')
            declared_paths.add(path)
            input_hashes[item['modelId']] = actual
        else:
            require(set(item) == {'action', 'params'}, 'Invalid generated input fields')
            require(isinstance(item.get('action'), str) and isinstance(item.get('params'), dict), 'Invalid generated input')
            require(any(e['kind'] == 'action' and e['payload'].get('code') == item['action'] and
                        e['payload'].get('params', {}) == item['params'] for e in document['entries']),
                    f'Generated input does not match Journal: {item["action"]}')
    imports = set()
    for entry in document['entries']:
        payload = entry['payload']
        if entry['kind'] == 'action' and payload['code'] == 'file.import':
            relative = payload.get('params', {}).get('filePath')
            require(isinstance(relative, str) and '\\' not in relative and ':' not in relative and not Path(relative).is_absolute(), 'Nonportable Journal import')
            path = (journal.parent / relative).resolve()
            require(path.is_relative_to(root), f'Journal import escapes asset repository: {relative}')
            imports.add(path)
    require(imports == declared_paths, 'Journal imports and declared model inputs differ')
    artifacts = metadata.get('artifacts', {})
    filenames = set()
    for identifier, artifact in artifacts.items():
        require(set(artifact) == {'location', 'fileName'}, 'Invalid artifact fields')
        require(artifact.get('location') == 'exports', f'Unknown artifact location: {identifier}')
        filename = artifact.get('fileName')
        require(isinstance(filename, str) and filename not in {'', '.', '..'} and Path(filename).name == filename and
                '\\' not in filename and ':' not in filename, f'Invalid artifact filename: {identifier}')
        require(filename not in filenames, 'Artifact filenames must be distinct')
        filenames.add(filename)
    identifiers = set()
    for check in metadata.get('checks', []):
        require(set(check) == {'id', 'validator', 'required', 'artifacts'}, 'Invalid check fields')
        require(isinstance(check.get('id'), str) and check['id'] and check['id'] not in identifiers, 'Missing/duplicate check ID')
        identifiers.add(check['id'])
        require(type(check.get('required')) is bool, 'Check required must be boolean')
        require(check.get('validator') == 'gcode-job', f'Unknown validator: {check.get("validator")}')
        require(check.get('artifacts') == ['gcode', 'package'] and all(k in artifacts for k in check['artifacts']), 'gcode-job requires gcode/package artifacts')
        require(isinstance(metadata.get('expected'), dict), 'gcode-job requires independent expectations')
        validate_expectations(metadata['expected'])
    return dict(id=name, metadata=metadata, journal=journal, document=document,
                hashes=dict(catalog=digest(root / 'catalog.json'), metadata=digest(metadata_path),
                            journal=digest(journal), models=input_hashes,
                            modelCatalogs={item['path']: digest(inside(root, item['path']))
                                           for item in read_json(root / 'catalog.json')['modelCatalogs']}))


def git_identity(directory):
    try:
        revision = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], stderr=subprocess.DEVNULL, text=True).strip()
        status = subprocess.check_output(['git', '-C', str(directory), 'status', '--porcelain'], stderr=subprocess.DEVNULL, text=True)
        return dict(commit=revision, dirty=bool(status.strip()))
    except (OSError, subprocess.CalledProcessError):
        return dict(commit=None, dirty=None)


def final_status(execution, checks):
    if not execution.get('success') or execution.get('acceptance') == 'failed':
        return 'failed'
    required = [check for check in checks if check['required']]
    if any(check['status'] == 'failed' for check in required):
        return 'failed'
    if any(check['status'] != 'passed' for check in required):
        return 'not_verified'
    return 'passed' if required or execution.get('acceptance') == 'passed' else 'not_verified'


def write_case_result(output, result, checks):
    write_json(output / 'checks.json', checks)
    result['checksPath'] = 'checks.json'
    result['requiredCheckCount'] = sum(check['required'] for check in checks)
    result['passedCheckCount'] = sum(check['status'] == 'passed' for check in checks)
    write_json(output / 'case_result.json', result)
    rows = ''.join('<tr><td>{}</td><td>{}</td><td>{}</td></tr>'.format(
        html.escape(check['id']), html.escape(check['status']), html.escape(check.get('message', ''))) for check in checks)
    body = ('<!doctype html><meta charset="utf-8"><title>Application acceptance</title>'
            '<h1>{}</h1><p>Status: {} | Scope: {}</p><p>{}</p>'
            '<p><a href="case_result.json">Case result</a> | <a href="checks.json">Business checks</a></p>'
            '{}<table><tr><th>Check</th><th>Status</th><th>Details</th></tr>{}</table>').format(
        html.escape(result['id']), html.escape(result['status']), html.escape(result.get('scope', '')),
        html.escape(result.get('error', '')), '<p><a href="verification_report.html">Original Journal report</a></p>'
        if result.get('executionResultPath') else '', rows)
    (output / 'case_report.html').write_text(body, encoding='utf8')


def write_summary(output, summary):
    write_json(output / 'summary.json', summary)
    rows = ''.join('<tr><td><a href="{}/case_report.html">{}</a></td><td>{}</td></tr>'.format(
        html.escape(c['id'], quote=True), html.escape(c['id']), html.escape(c['status'])) for c in summary['cases'])
    (output / 'report.html').write_text('<!doctype html><meta charset="utf-8"><title>Acceptance summary</title>'
        '<h1>{}</h1><p>{}</p><a href="summary.json">Machine-readable summary</a><table>{}</table>'.format(
            html.escape(summary['status']), html.escape(summary.get('error', '') or summary.get('cleanupError', '')), rows), encoding='utf8')


def run_case(root, name, application, output, exports, session, journal_loader):
    """Run one fresh session; preserve its report and evaluate declared business checks."""
    root, output, application = Path(root).resolve(), Path(output).resolve(), Path(application).resolve()
    if output.exists():
        raise FileExistsError(output)
    result = dict(schemaVersion=1, id=name, status='blocked', scope='', executionResultPath=None,
                  provenance=dict(assets=git_identity(root), applicationPath=str(application)), unverifiedScopes=[])
    checks = []
    try:
        case = load_case(root, name, journal_loader)
        metadata = case['metadata']
        result.update(scope=metadata['scope'], unverifiedScopes=metadata.get('unverifiedScopes', []))
        result['provenance'].update(inputs=case['hashes'], runnerSha256=digest(__file__),
                                    verifierSha256=digest(Path(__file__).with_name('acceptance_checks.py')),
                                    entrySha256=digest(Path(__file__).with_name('run_acceptance.py')),
                                    journalSessionSha256=digest(inspect.getfile(type(session))),
                                    journalLoaderSha256=digest(inspect.getfile(journal_loader)),
                                    appSource=git_identity(Path(__file__).resolve().parents[1]))
        require(application.is_file(), f'Missing application: {application}')
        result['provenance']['applicationSha256'] = digest(application)
        library = application.parent / 'libslicer.dll'
        if library.is_file():
            result['provenance']['deployedLibrary'] = dict(path=str(library), sha256=digest(library))
        require(not metadata.get('artifacts') or exports is not None, 'Declared exports require --exports pointing to the App save directory')
        export_root = Path(exports).resolve() if exports is not None else None
    except Exception as exc:
        output.mkdir(parents=True)
        result['error'] = str(exc)
        write_case_result(output, result, checks)
        return result
    started = time.time_ns()
    try:
        execution = session.run(case['journal'], application=application, output_dir=output, startup_timeout=120)
        result['executionResultPath'] = 'execution_result.json'
        result['journalAcceptance'] = execution.get('acceptance')
        result['completedEntries'] = execution.get('completedEntries', 0)
        require(execution.get('outputPath') and Path(execution['outputPath']).resolve() == output, 'Journal output directory differs from requested run')
        require(execution.get('success'), execution.get('error') or 'Journal execution or cleanup failed')
        require(execution.get('completedEntries') == len(case['document']['entries']), 'Journal did not complete every step')
        require(execution.get('journalSha256') == case['hashes']['journal'], 'Journal changed after preflight')
        require(load_case(root, name, journal_loader)['hashes'] == case['hashes'], 'Assets or expectations changed during execution')
        archived = {}
        for identifier, artifact in metadata.get('artifacts', {}).items():
            source = export_root / artifact['fileName']
            require(source.is_file(), f'Missing exported artifact: {source}')
            require(source.stat().st_mtime_ns >= started, f'Stale artifact from an earlier run: {source}')
            target = output / 'artifacts' / artifact['fileName']
            target.parent.mkdir(exist_ok=True)
            before = digest(source)
            shutil.copy2(source, target)
            require(digest(target) == before == digest(source), f'Artifact changed during collection: {source}')
            archived[identifier] = target
        if archived:
            result['artifacts'] = {key: dict(path=p.relative_to(output).as_posix(), sha256=digest(p), bytes=p.stat().st_size)
                                   for key, p in archived.items()}
        for check in metadata.get('checks', []):
            item = dict(id=check['id'], required=check['required'], status='failed', validator=check['validator'])
            try:
                # A fixed registry of maintained validators; no code/commands from metadata.
                report = verify_job(archived['gcode'], archived['package'], metadata['expected'])
                evidence = output / 'reports' / (str(len(checks) + 1) + '-gcode-job.json')
                evidence.parent.mkdir(exist_ok=True)
                write_json(evidence, report)
                item.update(status='passed', message='Exported print job matches independent expectations',
                            evidence=[evidence.relative_to(output).as_posix()])
            except Exception as exc:
                item['message'] = str(exc)
            checks.append(item)
        result['status'] = final_status(execution, checks)
    except Exception as exc:
        output.mkdir(parents=True, exist_ok=True)
        result.update(status='failed', error=str(exc))
        done = {check['id'] for check in checks}
        checks += [dict(id=c['id'], required=c['required'], status='not_run', message='Execution or artifact collection failed')
                   for c in metadata.get('checks', []) if c['id'] not in done]
    result['elapsedSeconds'] = round((time.time_ns() - started) / 1e9, 2)
    write_case_result(output, result, checks)
    return result
