"""Check the observation records produced by a real calibration UI workflow."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
from uuid import UUID


def record_directory(execution):
    # Journal's recordings directory is owned by AppDataLocation/Journal.
    data = Path(execution['journalPaths']['recordings']).parents[1]
    if sys.platform == 'win32':
        # CalibrationBridge uses AppLocalDataLocation, unlike Journal's roaming
        # AppDataLocation on Windows. Keep the actual organization/app suffix.
        data = Path(os.environ['LOCALAPPDATA']) / data.relative_to(Path(os.environ['APPDATA']))
    return data / 'calibration'


def validate_expectations(expected):
    if not isinstance(expected, list) or not expected:
        raise ValueError('calibration-observation requires independent expected records')
    for record in expected:
        if (not isinstance(record, dict) or type(record.get('mode')) is not int
                or type(record.get('applied')) is not bool
                or not isinstance(record.get('parameters'), dict)
                or not isinstance(record.get('observation'), dict)
                or record['observation'].get('observed') is not True):
            raise ValueError('Expected calibration record must declare mode, applied, parameters and observation')


def _matches(actual, expected):
    if isinstance(expected, dict):
        return (isinstance(actual, dict) and all(key in actual and _matches(actual[key], value)
                                                for key, value in expected.items()))
    return type(actual) is type(expected) and actual == expected


def verify_records(directory, started_ns, expected, output):
    validate_expectations(expected)
    directory, output = Path(directory), Path(output)
    files = sorted(path for path in directory.glob('*.json') if path.stat().st_mtime_ns >= started_ns)
    if len(files) != len(expected):
        raise ValueError(f'Expected {len(expected)} new calibration records; found {len(files)}')
    remaining, evidence = list(expected), []
    for path in files:
        raw = path.read_bytes()
        record = json.loads(raw)
        if (str(UUID(path.stem)) != path.stem or record.get('task') != path.stem
                or record.get('version') != 2):
            raise ValueError(f'Calibration record identity/version differs: {path.name}')
        if record.get('mode') == 5 and record.get('observation', {}).get('taskId') != record['task']:
            raise ValueError('Flow observation belongs to another calibration task')
        target = output / 'artifacts/calibration' / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        if target.read_bytes() != raw or path.read_bytes() != raw:
            raise ValueError('Calibration record changed during collection')
        match = next((item for item in remaining if _matches(record, item)), None)
        if match is None:
            raise ValueError(f'Calibration observation differs from independent expectations: {path.name}')
        remaining.remove(match)
        if record['applied'] and record.get('appliedRevision', 0) <= record.get('configurationRevision', 0):
            raise ValueError('Applied calibration did not advance the configuration revision')
        evidence.append(dict(path=target.relative_to(output).as_posix(),
                             sha256=hashlib.sha256(raw).hexdigest(), expected=match))
    return dict(status='passed', records=evidence)
