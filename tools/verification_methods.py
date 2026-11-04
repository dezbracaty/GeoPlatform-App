"""Scenario acceptance uses the existing DB or standard-screenshot comparison."""
METHODS = {
    'db': dict(name='DB comparison', evidence='Actual DB serialization and expected DB state'),
    'screenshot': dict(name='Standard screenshot comparison', evidence='Standard image, actual image and difference'),
}

VALIDATORS = {
    'journal-db': dict(methods=['db'], source='journal', kind='verify.db'),
    'journal-screenshot': dict(methods=['screenshot'], source='journal', kind='verify.screenshot'),
}

# Preserve the existing P3 file check as a separate specialized check. It does
# not prove a scenario's DB or screenshot result and cannot satisfy either mode.
SUPPLEMENTAL_VALIDATORS = {'gcode-job': dict(source='artifacts')}


def verification_plan(metadata, document):
    declaration = metadata.get('verification')
    if not isinstance(declaration, dict) or set(declaration) != {'requiredMethods'}:
        raise ValueError('verification must declare requiredMethods')
    required = declaration['requiredMethods']
    if (not isinstance(required, list) or len(required) != 1 or
            not isinstance(required[0], str) or required[0] not in METHODS):
        raise ValueError('Select exactly one acceptance type: db or screenshot')
    checks = []
    for check in metadata.get('checks', []):
        adapter = SUPPLEMENTAL_VALIDATORS.get(check.get('validator'))
        if not adapter or adapter['source'] != 'artifacts':
            raise ValueError(f'Unknown artifact validator: {check.get("validator")}')
        checks.append(dict(id=check['id'], validator=check['validator'], required=check['required'],
                           methods=[], supplemental=True))
    for validator, adapter in VALIDATORS.items():
        if adapter['source'] == 'journal':
            indices = [i for i, entry in enumerate(document['entries'], 1) if entry['kind'] == adapter['kind']]
            if indices:
                if adapter['methods'] != required:
                    raise ValueError('Comparison kind differs from the scenario acceptance type; use captures for diagnostics')
                checks.append(dict(id=validator, validator=validator, required=True,
                                   methods=adapter['methods'], entryIndices=indices))
    if len({c['id'] for c in checks}) != len(checks):
        raise ValueError('Check IDs collide with a registered Journal check')
    covered = {m for check in checks if check['required'] for m in check['methods']}
    missing = [m for m in required if m not in covered]
    return dict(requiredMethods=required, checks=checks, missingMethods=missing,
                readiness='missing_checks' if missing else 'ready')


def method_results(plan, checks):
    results = []
    for method in plan['requiredMethods']:
        relevant = [c for c in checks if c['required'] and method in c.get('methods', [])]
        expected_ids = {c['id'] for c in plan['checks'] if c['required'] and method in c['methods']}
        status = ('failed' if any(c['status'] == 'failed' for c in relevant) else
                  'passed' if expected_ids and expected_ids == {c['id'] for c in relevant} and
                  all(c['status'] == 'passed' for c in relevant) else 'not_verified')
        results.append(dict(method=method, status=status, checkIds=[c['id'] for c in relevant]))
    return results


def journal_checks(plan, execution):
    """Use the formal executor's per-entry assertions, preserving its raw report."""
    checks = []
    for check in plan['checks']:
        adapter = VALIDATORS.get(check['validator'])
        if adapter is None:
            continue
        selected = [r for r in execution.get('results', []) if r.get('index') in check['entryIndices']]
        passed = (len(selected) == len(check['entryIndices']) and
                  {r['index'] for r in selected} == set(check['entryIndices']) and
                  all(r.get('kind') == adapter['kind'] and r.get('status') == 'passed' for r in selected))
        failed = any(r.get('status') == 'failed' for r in selected)
        checks.append(dict(check, status='passed' if passed else 'failed' if failed else 'not_verified',
                           message='Formal Journal assertions', evidence=['execution_result.json']))
    return checks
