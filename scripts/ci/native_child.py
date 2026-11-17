"""Own a test subtree; the maintained native builder stays intact."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def attached_build(command, log, timeout, env=None):
    # The supervising native_regression.py created this child's process group. Keep
    # CMake/Ninja in it so cancelling the wrapper also cancels the compiler tree.
    print('RUN ' + json.dumps(list(map(str, command))), flush=True)
    with Path(log).open('w', encoding='utf-8') as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, env=env)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f'Native command timed out; supervisor will reap the owned group: {log}', flush=True)
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                process.wait(timeout=15)
            # On POSIX, exit the child; the outside supervisor's finally reaps
            # this entire group, including descendants, before releasing its lock.
            raise SystemExit(124)
        if code:
            raise RuntimeError(f'Native command failed ({code}); see {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['native'])
    parser.add_argument('--source', type=Path, required=True)
    args, rest = parser.parse_known_args()
    source = args.source.resolve()
    sys.path.insert(0, str(source / 'scripts'))
    import build_orca_candidate_probe as builder
    # Only process supervision/logging changes. The builder still owns its
    # compiler arguments, adapter, sources, hashes and provenance contracts.
    builder.run = attached_build
    return builder.main(rest)



if __name__ == '__main__':
    sys.exit(main())
