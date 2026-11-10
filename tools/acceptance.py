"""Asset contracts and business acceptance around the maintained Journal session.

No App RPCs, input scheduling or Journal acceptance rules are implemented here.
"""
import base64
import hashlib
import html
import inspect
import json
import re
import shutil
import struct
import subprocess
import time
from pathlib import Path
from urllib.parse import quote

from acceptance_checks import validate_expectations, verify_job, validate_project_gcode_expectations, verify_project_gcode
from verification_methods import SUPPLEMENTAL_VALIDATORS, journal_checks, method_results, verification_plan
from screenshot_review import snapshot_checkpoints, collect_screenshots, write_batch_report


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
    require(type(metadata.get('schemaVersion')) is int and metadata['schemaVersion'] == 3 and metadata.get('id') == name, 'Expected scenario metadata v3 with DB or screenshot acceptance')
    for key in ['name', 'testPurpose', 'scope', 'acceptanceTarget']:
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
    model_paths = {item['modelId']: models[item['modelId']]['absolutePath'] for item in inputs if 'modelId' in item}
    imports = set()
    for entry in document['entries']:
        payload = entry['payload']
        if entry['kind'] == 'action' and payload['code'] == 'file.import':
            relative = payload.get('params', {}).get('filePath')
            require(isinstance(relative, str) and '\\' not in relative and ':' not in relative and not Path(relative).is_absolute(), 'Nonportable Journal import')
            path = (journal.parent / relative).resolve()
            require(path.is_relative_to(root), f'Journal import escapes asset repository: {relative}')
            imports.add(path)
        elif entry['kind'] == 'input.key' and isinstance(payload.get('text'), str):
            binding = re.fullmatch(r'\$\{inputDirectory:([^}]+)\}', payload['text'])
            if binding:
                require(payload.get('target') == 'fileBrowserLocation' and binding[1] in model_paths, 'Undeclared picker input binding')
                imports.add(model_paths[binding[1]])
        elif entry['kind'] == 'input.dragDrop' and 'text/uri-list' in payload.get('mime', {}):
            value = base64.b64decode(payload['mime']['text/uri-list'], validate=True).decode('utf8')
            binding = re.fullmatch(r'\$\{inputUri:([^}]+)\}\r\n', value)
            require(binding is not None and binding[1] in model_paths, 'Undeclared drop input binding')
            imports.add(model_paths[binding[1]])
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
        validate_check(check, artifacts, metadata.get('expected'))
    plan = verification_plan(metadata, document)
    baselines = {}
    for reference in metadata.get('dbBaselines', []):
        require(set(reference) == {'entryIndex', 'file'}, 'Invalid DB baseline reference')
        index = reference['entryIndex']
        require(type(index) is int and 1 <= index <= len(document['entries']), 'Invalid DB baseline checkpoint')
        checkpoint = document['entries'][index - 1]
        require(checkpoint['kind'] == 'verify.db', 'DB baseline must reference verify.db')
        baseline = inside(root, reference['file'])
        require(baseline.is_file(), 'Missing standard DB file')
        require(base64.b64decode(checkpoint['payload']['expectedSerialized'], validate=True) == baseline.read_bytes(),
                'Standard DB file differs from the Journal expected state')
        baselines[reference['file']] = digest(baseline)
    for entry in document['entries']:
        if entry['kind'] == 'verify.screenshot':
            relative = entry['payload']['baseline']
            require(isinstance(relative, str) and '\\' not in relative and ':' not in relative and
                    not Path(relative).is_absolute(), 'Nonportable screenshot baseline')
            baseline = (journal.parent / relative).resolve()
            require(baseline.is_relative_to(root) and baseline.is_file(), 'Screenshot baseline must exist inside the asset repository')
            with baseline.open('rb') as stream:
                require(not stream.read(100).startswith(b'version https://git-lfs.github.com/spec/'),
                        'Standard screenshot is an unmaterialized LFS pointer; run git lfs pull')
            baselines[baseline.relative_to(root).as_posix()] = digest(baseline)
    return dict(id=name, metadata=metadata, journal=journal, document=document, verificationPlan=plan, modelPaths=model_paths,
                hashes=dict(catalog=digest(root / 'catalog.json'), metadata=digest(metadata_path),
                            journal=digest(journal), models=input_hashes, baselines=baselines,
                            modelCatalogs={item['path']: digest(inside(root, item['path']))
                                           for item in read_json(root / 'catalog.json')['modelCatalogs']}))


def validate_check(check, artifacts, expected):
    validator = check.get('validator')
    require(validator in SUPPLEMENTAL_VALIDATORS, f'Unknown validator: {validator}')
    require(isinstance(expected, dict), f'{validator} requires independent expectations')
    if validator == 'gcode-job':
        require(check.get('artifacts') == ['gcode', 'package'] and all(k in artifacts for k in check['artifacts']), 'gcode-job requires gcode/package artifacts')
        validate_expectations(expected)
    elif validator == 'project-gcode':
        require(isinstance(check.get('artifacts'), list) and len(check['artifacts']) == 2 and all(k in artifacts for k in check['artifacts']), 'project-gcode requires G-code and package artifacts')
        require(check['id'] in expected.get('gcodeJobs', {}), 'project-gcode requires its independent per-job expectations')
        validate_project_gcode_expectations(expected['gcodeJobs'][check['id']])


def git_identity(directory):
    try:
        top = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', '--show-toplevel'],
                                      stderr=subprocess.DEVNULL, text=True, encoding='utf-8').strip()
        if Path(top).resolve() != Path(directory).resolve():
            return dict(commit=None, dirty=None)
        revision = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], stderr=subprocess.DEVNULL, text=True, encoding='utf-8').strip()
        status = subprocess.check_output(['git', '-C', str(directory), 'status', '--porcelain'], stderr=subprocess.DEVNULL, text=True, encoding='utf-8')
        return dict(commit=revision, dirty=bool(status.strip()))
    except (OSError, subprocess.CalledProcessError):
        return dict(commit=None, dirty=None)


def application_dependencies(application):
    """Record the deployed code and manifests actually accompanying this binary."""
    application = Path(application)
    directories = [application.parent]
    bundle = next((p for p in application.parents if p.suffix == '.app'), None)
    if bundle:
        directories += [bundle / 'Contents/Frameworks', bundle / 'Contents/Resources']
    records = []
    for directory in directories:
        if not directory.is_dir():
            continue
        for path in directory.rglob('*'):
            if not path.is_file():
                continue
            name = path.name.lower()
            # Runtime libraries and delivery manifests carry the App/SDK/resource identity.
            relevant = ('slicer' in name and path.suffix.lower() in {'.dll', '.dylib', '.so', '.json'}) or name in {'manifest.json', 'source-manifest.json', 'sdk-manifest.json', 'runtime-manifest.json'}
            if relevant:
                records.append(dict(path=str(path.resolve()), sha256=digest(path), bytes=path.stat().st_size))
    return records


def final_status(execution, checks, methods):
    if not execution.get('success') or execution.get('acceptance') == 'failed':
        return 'failed'
    required = [check for check in checks if check['required']]
    if any(check['status'] == 'failed' for check in required):
        return 'failed'
    if any(check['status'] != 'passed' for check in required):
        return 'not_verified'
    if any(method['status'] == 'failed' for method in methods):
        return 'failed'
    return 'passed' if methods and all(method['status'] == 'passed' for method in methods) else 'not_verified'


def write_case_result(output, result, checks):
    write_json(output / 'checks.json', checks)
    result['checksPath'] = 'checks.json'
    result['requiredCheckCount'] = sum(check['required'] for check in checks)
    result['passedCheckCount'] = sum(check['status'] == 'passed' for check in checks)
    result['supplementalResults'] = [check for check in checks if check.get('supplemental')]
    write_json(output / 'case_result.json', result)
    rows = ''.join('<tr><td>{}</td><td>{}</td><td>{}</td></tr>'.format(
        html.escape(('Supplemental: ' if check.get('supplemental') else '') + check['id']),
        html.escape(check['status']), html.escape(check.get('message', ''))) for check in checks)
    rows += ''.join('<tr><td>Method: {}</td><td>{}</td><td>{}</td></tr>'.format(
        html.escape(method['method']), html.escape(method['status']), html.escape(', '.join(method['checkIds'])))
        for method in result.get('methodResults', []))
    review_link = '<p><a href="../report.html">批次结果索引</a> | <a href="review/screenshots.json">截图证据清单</a></p>' if result.get('screenshotReview') else ''
    body = ('<!doctype html><meta charset="utf-8"><title>Application acceptance</title>'
            '<h1>{}</h1><p>Status: {} | Scope: {}</p><p>{}</p>'
            '<p><a href="case_result.json">Case result</a> | <a href="checks.json">Business checks</a></p>'
            '{}<table><tr><th>Check</th><th>Status</th><th>Details</th></tr>{}</table>').format(
        html.escape(result['id']), html.escape(result['status']), html.escape(result.get('scope', '')),
        html.escape(result.get('error') or result.get('screenshotReviewError', '')), '<p><a href="verification_report.html">Original Journal report</a></p>'
        if result.get('executionResultPath') else '', rows)
    (output / 'case_report.html').write_text(body + review_link, encoding='utf8')


def write_summary(output, summary):
    try:
        write_batch_report(output, summary)
    except Exception as exc:
        summary['screenshotReviewError'] = str(exc)
        # Auxiliary image processing must not remove the original batch report.
        rows = ''.join('<tr><td><a href="{}/case_report.html">{}</a></td><td>{}</td></tr>'.format(
            quote(case['id'], safe=''), html.escape(case['id']), html.escape(case['status']))
            for case in summary['cases'])
        (output / 'report.html').write_text(
            '<!doctype html><meta charset="utf-8"><title>Acceptance summary</title>'
            '<h1>{}</h1><p>{}</p><p>截图辅助对照不可用：{}</p>'
            '<a href="summary.json">Machine-readable summary</a><table>{}</table>'.format(
                html.escape(summary['status']), html.escape(summary.get('error') or summary.get('cleanupError') or ''),
                html.escape(str(exc)), rows), encoding='utf8')
    write_json(output / 'summary.json', summary)


def bind_deployment_resources(document, metadata, application):
    """Relocate declared, hash-pinned resource strings in expected DB bytes only."""
    resources = metadata.get('deploymentResources')
    if resources is None:
        return False
    require(isinstance(resources, dict) and resources, 'deploymentResources must be a nonempty mapping')
    captured = metadata.get('preconditions', {}).get('applicationDirectory')
    require(application is not None and isinstance(captured, str) and Path(captured).is_absolute(),
            'Deployment resources require an application and captured applicationDirectory')
    directory = Path(application).resolve().parent
    replacements = []
    for relative, expected_hash in resources.items():
        require(isinstance(relative, str) and relative.startswith('resources/')
                and not any(part in ('', '.', '..') for part in relative.split('/'))
                and not any(char in relative for char in '\\:'), 'Invalid deployment resource path')
        require(isinstance(expected_hash, str) and re.fullmatch(r'[0-9a-f]{64}', expected_hash),
                'Invalid deployment resource SHA256')
        resource = (directory / relative).resolve()
        require(resource.is_relative_to(directory) and resource.is_file(),
                f'Missing deployment resource: {relative}')
        require(digest(resource) == expected_hash, f'Deployment resource hash differs: {relative}')

        def token(value):
            encoded = str(value).encode('utf-8')
            return struct.pack('<Q', 11) + b'string.utf8' + struct.pack('<Q', len(encoded)) + encoded

        replacements.append((relative, token(Path(captured) / relative), token(directory / relative)))
    seen, changed = set(), False
    for entry in document['entries']:
        if entry['kind'] != 'verify.db':
            continue
        payload = entry['payload']
        raw = base64.b64decode(payload['expectedSerialized'], validate=True)
        require(payload['serializationFormat'] == 'transdb.cereal-portable-binary.v1'
                and raw[:5] == b'\x01SBDT', 'Deployment binding requires portable little-endian DB standards')
        original = raw
        for relative, before, after in replacements:
            count = raw.count(before)
            require(count <= 1, f'Ambiguous deployment resource in DB standard: {relative}')
            if count:
                seen.add(relative)
                raw = raw.replace(before, after, 1)
        if raw != original:
            payload['expectedSerialized'] = base64.b64encode(raw).decode('ascii')
            changed = True
    require(seen == set(resources), f'Deployment resources missing from DB standards: {set(resources) - seen}')
    return changed


def materialize_journal(case, output, export_root, *, capture_candidates=False, application=None):
    """Bind declared portable output names; playback remains the formal Session.

    Preserve the authored document and separately hash the exact runtime JSON.
    Resolve declared input/output bindings through the shared Journal entries.
    """
    # The formal loader decorates in-memory entries with wire sequence and source
    # identity. Those fields must never be written back into a v2 Journal file.
    document = read_json(case['journal'])
    changed = bind_deployment_resources(document, case['metadata'], application)
    declared = {a['fileName'] for a in case['metadata'].get('artifacts', {}).values()}
    for index, entry in enumerate(document['entries'], 1):
        if capture_candidates and entry['kind'] == 'verify.db':
            entry.update(kind='capture.db', payload=dict(dbInstanceId=entry['payload']['dbInstanceId']))
            changed = True
        elif capture_candidates and entry['kind'] == 'verify.screenshot':
            require(entry['payload'].get('window') in (None, {'objectName': 'gplatform.main'}),
                    'Candidate screenshot capture supports the main application window only')
            entry.update(kind='action', payload=dict(code='screenshot.capture', params=dict(
                filepath=f'candidates/screenshot_{index:03d}.png', notify=False)))
            changed = True
        if entry['kind'] != 'action':
            if entry['kind'] == 'input.key':
                value = entry['payload'].get('text')
                binding = re.fullmatch(r'\$\{inputDirectory:([^}]+)\}', value or '')
                if binding:
                    require(entry['payload'].get('target') == 'fileBrowserLocation' and binding[1] in case.get('modelPaths', {}), 'Undeclared picker input binding')
                    entry['payload']['text'] = str(case['modelPaths'][binding[1]].parent)
                    changed = True
                elif value == '${exports}':
                    require(export_root is not None and entry['payload'].get('target') == 'fileBrowserLocation',
                            'Export-directory binding requires a real file-browser location input')
                    entry['payload']['text'] = str(export_root)
                    changed = True
                elif value in declared and entry['payload'].get('target') != 'fileBrowserFileName':
                    require(export_root is not None, 'Declared output input needs --exports')
                    entry['payload']['text'] = str(export_root / value)
                    changed = True
            elif entry['kind'] == 'input.dragDrop' and 'text/uri-list' in entry['payload'].get('mime', {}):
                value = base64.b64decode(entry['payload']['mime']['text/uri-list'], validate=True).decode('utf8')
                binding = re.fullmatch(r'\$\{inputUri:([^}]+)\}\r\n', value)
                require(binding is not None and binding[1] in case.get('modelPaths', {}), 'Undeclared drop input binding')
                entry['payload']['mime']['text/uri-list'] = base64.b64encode((case['modelPaths'][binding[1]].as_uri() + '\r\n').encode()).decode()
                changed = True
            continue
    if not changed:
        return case['journal'], digest(case['journal'])
    # Relative imports refer to the authored source, not the materialized location.
    for entry in document['entries']:
        if entry['kind'] == 'action' and entry['payload'].get('code') == 'file.import':
            params = entry['payload'].get('params', {})
            if params.get('filePath') and not Path(params['filePath']).is_absolute():
                params['filePath'] = str((case['journal'].parent / params['filePath']).resolve())
        elif entry['kind'] == 'verify.screenshot':
            baseline = entry['payload']['baseline']
            if not Path(baseline).is_absolute():
                entry['payload']['baseline'] = str((case['journal'].parent / baseline).resolve())
    path = output.parent / '.declarations' / output.name / 'events/Journal.json'
    path.parent.mkdir(parents=True, exist_ok=False)
    write_json(path, document)
    # The shared Session reads process/display setup beside the runtime Journal.
    # Materializing picker/export bindings must retain that same setup.
    write_json(path.parent.parent / 'metadata.json', case['metadata'])
    return path, digest(path)


def run_case(root, name, application, output, exports, session, journal_loader, *, review_screenshots=False,
             environment_overrides=None, capture_candidates=False, review_submit=None):
    """Run one fresh session; preserve its report and evaluate declared business checks."""
    root, output, application = Path(root).resolve(), Path(output).resolve(), Path(application).resolve()
    if output.exists():
        raise FileExistsError(output)
    result = dict(schemaVersion=3, id=name, status='blocked', scope='', executionResultPath=None,
                  provenance=dict(assets=git_identity(root), applicationPath=str(application)), unverifiedScopes=[],
                  verificationPlan=None, methodResults=[])
    if capture_candidates:
        result['candidateCollection'] = True
    checks = []
    checkpoints = []
    execution = {}
    try:
        case = load_case(root, name, journal_loader)
        metadata = case['metadata']
        if review_screenshots:
            checkpoints = snapshot_checkpoints(case, root)
            require(not any(row.get('baselineError') for row in checkpoints), 'Screenshot standards changed after preflight')
        result.update(scope=metadata['scope'], unverifiedScopes=metadata.get('unverifiedScopes', []),
                      verificationPlan=case['verificationPlan'])
        result['methodResults'] = method_results(case['verificationPlan'], [])
        result['provenance'].update(inputs=case['hashes'], runnerSha256=digest(__file__),
                                    verifierSha256=digest(Path(__file__).with_name('acceptance_checks.py')),
                                    methodsSha256=digest(Path(__file__).with_name('verification_methods.py')),
                                    entrySha256=digest(Path(__file__).with_name('run_acceptance.py')),
                                    journalSessionSha256=digest(inspect.getfile(type(session))),
                                    journalLoaderSha256=digest(inspect.getfile(journal_loader)),
                                    appSource=git_identity(Path(__file__).resolve().parents[1]))
        if environment_overrides is not None:
            result['provenance']['environmentOverrides'] = dict(environment_overrides)
        app_root = Path(__file__).resolve().parents[1]
        if review_screenshots:
            result['provenance']['screenshotComparisonSha256'] = digest(Path(__file__).with_name('screenshot_comparison.py'))
            result['provenance']['screenshotTextSha256'] = digest(Path(__file__).with_name('screenshot_text.py'))
        result['provenance']['slicerSource'] = git_identity(app_root / 'ThirdParty/OrcaSlicer')
        result['provenance']['sdkSource'] = git_identity(app_root / 'ThirdParty/libs/GPlatformSDK')
        if (app_root.parent / 'Core').is_dir():
            result['provenance']['hostSource'] = git_identity(app_root.parent)
        require(application.is_file(), f'Missing application: {application}')
        result['provenance']['applicationSha256'] = digest(application)
        result['provenance']['deployedDependencies'] = application_dependencies(application)
        library = application.parent / 'libslicer.dll'
        if library.is_file():
            result['provenance']['deployedLibrary'] = dict(path=str(library), sha256=digest(library))
        require(not metadata.get('artifacts') or exports is not None, 'Declared exports require --exports pointing to the App save directory')
        export_root = Path(exports).resolve() if exports is not None else None
        # A new case must never adopt a valid artifact left by a previous run.
        existing = [export_root / a['fileName'] for a in metadata.get('artifacts', {}).values()
                    if (export_root / a['fileName']).exists()]
        require(not existing, f'Export destination already exists: {existing}')
        runtime_journal, runtime_hash = materialize_journal(case, output, export_root,
                                                         capture_candidates=capture_candidates,
                                                         application=application)
        result['provenance']['runtimeJournal'] = dict(path=str(runtime_journal), sha256=runtime_hash)
    except Exception as exc:
        output.mkdir(parents=True)
        result['error'] = str(exc)
        if review_screenshots:
            if not checkpoints:
                try:
                    journal = inside(root, f'scenarios/{name}/events/Journal.json')
                    checkpoints = snapshot_checkpoints(dict(journal=journal, document=journal_loader(journal)),
                                                       root, verify_hashes=False)
                except Exception:
                    pass  # Invalid/unreadable declarations remain explicit preflight failures.
            try:
                result['screenshotReview'] = collect_screenshots(checkpoints, dict(error=str(exc)), output)
            except Exception as review_error:
                result['screenshotReviewError'] = str(review_error)
        result['executionStatus'] = 'not_started'
        write_case_result(output, result, checks)
        return result
    started = time.time_ns()
    try:
        setup = dict(environment_overrides=environment_overrides) if environment_overrides is not None else {}
        execution = session.run(runtime_journal, application=application, output_dir=output, startup_timeout=120, **setup)
        result['executionResultPath'] = 'execution_result.json'
        result['journalAcceptance'] = execution.get('acceptance')
        result['completedEntries'] = execution.get('completedEntries', 0)
        checks.extend(journal_checks(case['verificationPlan'], execution))
        require(execution.get('outputPath') and Path(execution['outputPath']).resolve() == output, 'Journal output directory differs from requested run')
        failed_step = next((step for step in execution.get('results', []) if step.get('status') == 'failed'), None)
        reason = (execution.get('error') or (failed_step or {}).get('message') or
                  '; '.join(execution.get('cleanupErrors', [])) or 'Journal execution or cleanup failed')
        if failed_step:
            result['failedStep'] = {key: failed_step.get(key) for key in ['index', 'kind', 'message']}
        require(execution.get('success'), reason)
        require(execution.get('completedEntries') == len(case['document']['entries']), 'Journal did not complete every step')
        require(execution.get('journalSha256') == runtime_hash and digest(runtime_journal) == runtime_hash, 'Journal changed after preflight')
        require(load_case(root, name, journal_loader)['hashes'] == case['hashes'], 'Assets or expectations changed during execution')
        inputs = output / 'inputs'
        inputs.mkdir()
        for source, filename, expected_hash in [
                (case['journal'], 'authored-Journal.json', case['hashes']['journal']),
                (runtime_journal, 'executed-Journal.json', runtime_hash),
                (case['journal'].parent.parent / 'metadata.json', 'metadata.json', case['hashes']['metadata'])]:
            target = inputs / filename
            shutil.copy2(source, target)
            require(digest(target) == expected_hash == digest(source), 'Scenario declaration changed during evidence collection')
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
            item = dict(id=check['id'], required=check['required'], status='failed', validator=check['validator'],
                        methods=[], supplemental=True)
            try:
                # A fixed registry of maintained validators; no code/commands from metadata.
                if check['validator'] == 'gcode-job':
                    report = verify_job(archived['gcode'], archived['package'], metadata['expected'])
                elif check['validator'] == 'project-gcode':
                    identifiers = check['artifacts']
                    report = verify_project_gcode(archived[identifiers[0]], archived[identifiers[1]], metadata['expected']['gcodeJobs'][check['id']])
                evidence = output / 'reports' / (str(len(checks) + 1) + '-' + check['validator'] + '.json')
                evidence.parent.mkdir(exist_ok=True)
                write_json(evidence, report)
                item.update(status='passed', message='Exported artifacts match independent expectations',
                            evidence=[evidence.relative_to(output).as_posix()])
            except Exception as exc:
                item['message'] = str(exc)
            checks.append(item)
        result['methodResults'] = method_results(case['verificationPlan'], checks)
        result['status'] = final_status(execution, checks, result['methodResults'])
    except Exception as exc:
        output.mkdir(parents=True, exist_ok=True)
        result.update(status='failed', error=str(exc))
        done = {check['id'] for check in checks}
        checks += [dict(c, status='not_run', message='Execution or artifact collection failed')
                   for c in case['verificationPlan']['checks'] if c['id'] not in done]
        result['methodResults'] = method_results(case['verificationPlan'], checks)
    result['elapsedSeconds'] = round((time.time_ns() - started) / 1e9, 2)
    result['executionStatus'] = ('completed' if execution.get('success') and
        execution.get('completedEntries') == len(case['document']['entries']) else 'failed')
    if review_screenshots and review_submit is not None:
        result['reviewStatus'] = 'processing'
        write_case_result(output, result, checks)
        review_submit(checkpoints, execution, output)
        return result
    if review_screenshots:
        try:
            result['screenshotReview'] = collect_screenshots(checkpoints, execution, output)
            result['reviewStatus'] = 'pending' if result['screenshotReview']['pendingCount'] else 'not_applicable'
        except Exception as review_error:
            result['screenshotReviewError'] = str(review_error)

    write_case_result(output, result, checks)
    return result
