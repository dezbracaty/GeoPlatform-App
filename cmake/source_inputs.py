"""Record and verify the managed fork files shared by SDK producers and Apps."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def git(directory, *args):
    return subprocess.check_output(['git', '-C', str(directory), *args]).decode('utf-8')


def managed_files(directory):
    files = []
    for entry in git(directory, 'ls-files', '--stage', '-z').split('\0'):
        if entry:
            metadata, name = entry.split('\t', 1)
            if metadata.split()[0] in {'100644', '100755', '120000'}:
                files.append(name)
    return files


def content_digest(files):
    canonical = json.dumps(files, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(canonical.encode('utf-8')).hexdigest()


def file_hashes(directory, names):
    result = {}
    for name in sorted(names):
        relative = Path(name)
        path = directory / relative
        if relative.is_absolute() or '..' in relative.parts or not path.is_file():
            raise RuntimeError(f'Missing or invalid managed fork file: {path}')
        result[name] = digest(path)
    return result


def summary(manifest):
    return {fork: {key: value for key, value in record.items() if key != 'files'}
            for fork, record in manifest.items()}


def record(third_party, inventory):
    manifest = {}
    for fork, names in sorted(inventory.items()):
        directory = third_party / fork
        files = file_hashes(directory, names)
        manifest[fork] = {
            'revision': git(directory, 'rev-parse', 'HEAD').strip(),
            'dirty': bool(git(directory, 'status', '--porcelain', '--untracked-files=no').strip()),
            'diff_sha256': hashlib.sha256(git(directory, 'diff', '--binary', 'HEAD').encode('utf-8')).hexdigest(),
            'content_sha256': content_digest(files),
            'files': files,
        }
    return manifest


def verify(third_party, manifest, expected, *, check_revisions=True):
    if manifest is not None:
        for fork, declared in sorted(manifest.items()):
            if content_digest(declared['files']) != declared['content_sha256']:
                raise RuntimeError(f'{fork} managed manifest does not match its declared content digest')
    # Git checkouts are shared across platforms; the exported manifest can
    # describe another platform's checkout bytes. Verify actual inputs against
    # the selected SDK without rewriting tracked files or creating CI commits.
    if expected and check_revisions and all((third_party / fork / '.git').exists() for fork in expected):
        inventory = {fork: managed_files(third_party / fork) for fork in expected}
        if manifest is not None and summary(manifest) == expected:
            for fork, names in inventory.items():
                if set(names) != set(manifest[fork]['files']):
                    raise RuntimeError(f'{fork} managed file inventory differs from the SDK input')
        actual = record(third_party, inventory)
        for fork, declared in sorted(expected.items()):
            observed = actual[fork]
            if observed['content_sha256'] != declared['content_sha256']:
                raise RuntimeError(f'{fork} managed content differs from the SDK input')
            if observed['revision'] != declared['revision']:
                raise RuntimeError(f'{fork} revision {observed["revision"]} does not match the SDK input')
            if summary({fork: observed})[fork] != declared:
                raise RuntimeError(f'{fork} working tree provenance does not match the SDK input')
        return
    if manifest is None:
        raise RuntimeError('Fork source manifest is missing; initialize all Git forks or use a complete exported App package')
    if not expected or summary(manifest) != expected:
        raise RuntimeError('Fork provenance does not match the SDK input manifest')
    revisions_file = third_party / 'source-revisions.json'
    revisions = json.loads(revisions_file.read_text(encoding='utf-8')) if revisions_file.exists() else {}
    for fork, declared in sorted(manifest.items()):
        directory = third_party / fork
        if content_digest(declared['files']) != expected[fork]['content_sha256']:
            raise RuntimeError(f'{fork} managed manifest does not match the SDK content digest')
        actual = file_hashes(directory, declared['files'])
        if actual != declared['files']:
            changed = next(name for name in actual if actual[name] != declared['files'][name])
            raise RuntimeError(f'{fork} managed file differs from the SDK input: {changed}')
        if not check_revisions:
            continue
        if (directory / '.git').exists():
            revision = git(directory, 'rev-parse', 'HEAD').strip()
            if set(managed_files(directory)) != set(declared['files']):
                raise RuntimeError(f'{fork} managed file inventory differs from the SDK input')
        else:
            revision = revisions.get(fork)
        if revision != expected[fork]['revision']:
            raise RuntimeError(f'{fork} revision {revision} does not match the SDK input')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['record', 'verify'])
    parser.add_argument('--third-party', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--inventory', type=Path)
    parser.add_argument('--summary', type=Path)
    parser.add_argument('--metadata', type=Path)
    args = parser.parse_args()
    if args.operation == 'record':
        manifest = record(args.third_party, json.loads(args.inventory.read_text(encoding='utf-8')))
        args.manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        args.summary.write_text(json.dumps(summary(manifest), ensure_ascii=False, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    else:
        manifest = json.loads(args.manifest.read_text(encoding='utf-8')) if args.manifest.exists() else None
        verify(args.third_party, manifest,
               json.loads(args.metadata.read_text(encoding='utf-8')).get('SOURCE_INPUTS'))


if __name__ == '__main__':
    main()
