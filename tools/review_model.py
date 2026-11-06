"""Portable evidence model and separate, hash-bound human review decisions."""
from datetime import datetime, timezone
import getpass
import hashlib
import json
import os
from pathlib import Path
import tempfile


class ReviewBatch:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.reload()

    def reload(self):
        path = self.directory / 'screenshot_review.json'
        document = json.loads(path.read_text(encoding='utf8'))
        if (document.get('schemaVersion') != 1 or document.get('scope') != 'batch-screenshot-review' or
                not isinstance(document.get('runId'), str) or not isinstance(document.get('checkpoints'), list)):
            raise ValueError('Unsupported batch screenshot review manifest')
        summary_path = self.directory / 'summary.json'
        if summary_path.is_file():
            summary = json.loads(summary_path.read_text(encoding='utf8'))
            if summary.get('reviewRunId') != document['runId']:
                raise ValueError('Automatic summary belongs to a different batch')
            # A later auxiliary write can fail while the formal summary advances.
            # Never show a previous partial manifest's status as the final verdict.
            document['status'] = summary['status']
            document['screenshotReviewError'] = summary.get('screenshotReviewError')
            prior = {case['id']: case for case in document.get('cases', [])}
            completed = {case['id']: case for case in summary.get('cases', [])}
            document['cases'] = [dict(prior.get(name, {}), **completed.get(name, dict(id=name, status='not_run')))
                                 for name in summary.get('plannedCases', list(prior))]
        rows = document['checkpoints']
        if len({row['id'] for row in rows}) != len(rows):
            raise ValueError('Duplicate screenshot checkpoint identity')
        self.document, self.rows = document, rows
        self._fingerprints = {}
        self._decisions = {}
        self.review_path = self.directory / 'review-decisions.json'
        if self.review_path.exists():
            saved = json.loads(self.review_path.read_text(encoding='utf8'))
            if saved.get('schemaVersion') != 1 or saved.get('runId') != document['runId']:
                raise ValueError('Human review record belongs to a different batch')
            valid = {row['id'] for row in rows}
            self._decisions = {key: value for key, value in saved.get('reviews', {}).items() if key in valid}

    def path(self, row, key):
        relative = row.get(key)
        if not relative:
            return None
        if not isinstance(relative, str) or Path(relative).is_absolute() or '\\' in relative:
            raise ValueError('Image path must be relative to the batch directory')
        path = (self.directory / relative).resolve()
        if not path.is_relative_to(self.directory):
            raise ValueError('Image path escapes the batch directory')
        return path if path.is_file() else None

    def can_review(self, row):
        try:
            return (row.get('reviewStatus') == 'pending' and row.get('kind') == 'verify.screenshot' and
                    row.get('executionStatus') in {'captured', 'passed', 'failed'} and
                    row.get('comparisonStatus') in {'different', 'identical'} and
                    self._intact(row))
        except (ValueError, OSError):
            return False

    def _intact(self, row):
        for key, digest_key in [('expected', 'baselineSha256'), ('actual', 'actualSha256')]:
            path = self.path(row, key)
            if path is None or not row.get(digest_key):
                return False
            stat = path.stat()
            fingerprint = (str(path), stat.st_mtime_ns, stat.st_size, row[digest_key])
            if fingerprint not in self._fingerprints:
                self._fingerprints[fingerprint] = hashlib.sha256(path.read_bytes()).hexdigest() == row[digest_key]
            if not self._fingerprints[fingerprint]:
                return False
        return True

    def decision(self, row):
        if not self.can_review(row):
            return dict(decision='not_applicable', note='')
        record = self._decisions.get(row['id'], {})
        if (record.get('baselineSha256') != row.get('baselineSha256') or
                record.get('actualSha256') != row.get('actualSha256')):
            return dict(decision='pending', note='')
        return dict(record) if record else dict(decision='pending', note='')

    def save(self, row, decision, note, reviewer=None):
        if not self.can_review(row) or row not in self.rows:
            raise ValueError('Incomplete or incompatible screenshot evidence cannot be reviewed')
        if decision not in {'pending', 'accepted', 'defect'}:
            raise ValueError('Invalid human review decision')
        if not isinstance(note, str) or len(note) > 4000:
            raise ValueError('Review note must be text of at most 4000 characters')
        reviewer = getpass.getuser() if reviewer is None else reviewer.strip()
        if not reviewer:
            raise ValueError('Reviewer name is required')
        # Recheck raw bytes so review cannot silently apply to replaced evidence.
        for key, digest_key in [('expected', 'baselineSha256'), ('actual', 'actualSha256')]:
            path = self.path(row, key)
            if not row.get(digest_key) or hashlib.sha256(path.read_bytes()).hexdigest() != row[digest_key]:
                raise ValueError('Screenshot evidence changed; regenerate the batch comparison')
        record = dict(decision=decision, note=note, reviewer=reviewer,
                      reviewedAt=datetime.now(timezone.utc).isoformat(),
                      baselineSha256=row['baselineSha256'], actualSha256=row['actualSha256'])
        # Preserve independently saved checkpoints if another viewer is open.
        if self.review_path.exists():
            current = json.loads(self.review_path.read_text(encoding='utf8'))
            if current.get('runId') != self.document['runId']:
                raise ValueError('Review batch identity changed')
            self._decisions.update(current.get('reviews', {}))
        self._decisions[row['id']] = record
        saved = dict(schemaVersion=1, runId=self.document['runId'], reviews=self._decisions)
        handle, name = tempfile.mkstemp(prefix='.review-decisions-', suffix='.json', dir=self.directory)
        try:
            with os.fdopen(handle, 'w', encoding='utf8') as stream:
                stream.write(json.dumps(saved, ensure_ascii=False, indent=2)+'\n')
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, self.review_path)
        finally:
            Path(name).unlink(missing_ok=True)
        return dict(record)
