"""Keep MSVC /showIncludes and Ninja's dependency prefix in the same encoding."""
import argparse
import json
import os
import re
import subprocess
import sys


def normalize_line(line, encoding):
    return line.decode(encoding, errors="replace").encode("utf-8")


def select_probe_header(encoding):
    for character in ("é", "依", "あ", "Ж", "가", "ก", "א", "ش", "α"):
        try:
            character.encode(encoding)
            return "gplatform_probe_" + character + ".h"
        except UnicodeEncodeError:
            pass
    return "gplatform_dependency_probe.h"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--encoding")
    parser.add_argument("--detect-header")
    parser.add_argument("--probe-header-name", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.probe_header_name:
        print(json.dumps(dict(header=select_probe_header("mbcs" if os.name == "nt" else "utf-8"))))
        return 0
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if args.detect_header:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            sys.stdout.buffer.write(result.stdout)
            return result.returncode
        # Non-ASCII sentinel distinguishes output encodings even if the MSVC
        # language pack is English. Never guess the encoding of individual paths.
        encodings = ["utf-8", "mbcs"] if os.name == "nt" else ["utf-8"]
        for encoding in encodings:
            try:
                sentinel = args.detect_header.encode(encoding)
            except UnicodeEncodeError:
                continue
            if sentinel not in result.stdout:
                continue
            text = result.stdout.decode(encoding)
            for line in text.splitlines():
                if args.detect_header not in line:
                    continue
                match = re.match(r"^(.+?: +)[A-Za-z]:", line)
                if match:
                    print(json.dumps(dict(encoding=encoding, prefix=match.group(1))))
                    return 0
        parser.error("Cannot detect compiler output encoding from the known header path")
    if not args.encoding:
        parser.error("--encoding is required outside the dependency probe")
    # A list preserves compiler/launcher arguments, spaces and quoting. Do not
    # invoke a shell or reinterpret build arguments as shell source.
    with subprocess.Popen(command, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT) as process:
        for line in iter(process.stdout.readline, b""):
            sys.stdout.buffer.write(normalize_line(line, args.encoding))
            sys.stdout.buffer.flush()
        return process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
