#!/usr/bin/env bash
set -euo pipefail
cd "${GPLATFORM_BUILD_SOURCE_DIR:-$(dirname "$0")/..}"
preset=release
mode=build
cmake_tool=cmake
configure_args=()
while (($#)); do
    case "$1" in
        --preset) preset="${2:?Expected release or debug}"; shift 2 ;;
        --qt-root) export GPLATFORM_QT_ROOT="${2:?Expected Qt SDK root}"; shift 2 ;;
        --cmake) cmake_tool="${2:?Expected CMake executable}"; shift 2 ;;
        --configure-only) mode=configure; shift ;;
        --check) mode=check; shift ;;
        --) shift; configure_args+=("$@"); break ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done
case "$preset" in release|debug) ;; *) echo 'Preset must be release or debug' >&2; exit 1 ;; esac
command -v "$cmake_tool" >/dev/null || { echo 'Install CMake >= 3.24 on PATH or pass --cmake.' >&2; exit 1; }
if [[ "$(uname -s)" == Darwin ]]; then
    xcrun --find clang >/dev/null || { echo 'Install/select Xcode or Command Line Tools; check DEVELOPER_DIR.' >&2; exit 1; }
    xcrun --show-sdk-path >/dev/null || { echo 'No macOS SDK in the selected Apple developer directory.' >&2; exit 1; }
fi
if [[ "$mode" == check ]]; then
    "$cmake_tool" --preset check "${configure_args[@]}"
else
    "$cmake_tool" --preset "$preset" "${configure_args[@]}"
    if [[ "$mode" == build ]]; then
        "$cmake_tool" --build --preset "$preset"
    fi
fi
