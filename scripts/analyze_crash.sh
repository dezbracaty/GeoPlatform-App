#!/usr/bin/env bash
# Analyze an existing dump; discovery does not depend on build cache layout.
set -euo pipefail
usage() {
    echo "Usage: $0 [--stackwalk EXECUTABLE] [--symbols DIRECTORY] DUMP_FILE"
}
stackwalk="${MINIDUMP_STACKWALK:-minidump_stackwalk}"
symbols=""
dump_file=""
while (($#)); do
    case "$1" in
        --stackwalk) stackwalk="${2:?Expected stackwalk executable}"; shift 2 ;;
        --symbols) symbols="${2:?Expected symbols directory}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) usage >&2; exit 1 ;;
        *) if [[ -n "$dump_file" ]]; then usage >&2; exit 1; fi
           dump_file="$1"; shift ;;
    esac
done
if [[ -z "$dump_file" || ! -f "$dump_file" ]]; then
    echo 'Provide an existing dump file.' >&2
    usage >&2
    exit 1
fi
if ! stackwalk="$(command -v "$stackwalk")"; then
    echo 'minidump_stackwalk was not found. Add it to PATH or pass --stackwalk EXECUTABLE.' >&2
    exit 1
fi
if [[ -n "$symbols" ]]; then
    [[ -d "$symbols" ]] || { echo "Symbols directory does not exist: $symbols" >&2; exit 1; }
    exec "$stackwalk" "$dump_file" "$symbols"
fi
exec "$stackwalk" "$dump_file"
