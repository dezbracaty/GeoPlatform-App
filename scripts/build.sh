#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd "$(dirname "$0")/.." && pwd)"
preset="${GPLATFORM_PRESET:-}"
if [[ -z "$preset" ]]; then
    preset=release
    if [[ "$(uname -s)" == Darwin ]]; then
        case "$(uname -m)" in
            arm64) preset=arm_release ;;
            x86_64) preset=x86_release ;;
        esac
    fi
fi
mode=build
cmake_tool=cmake
configure_args=()
while (($#)); do
    case "$1" in
        --source-dir) source_dir="${2:?Expected project source directory}"; shift 2 ;;
        --preset) preset="${2:?Expected a project configure/build preset}"; shift 2 ;;
        --cmake) cmake_tool="${2:?Expected CMake executable}"; shift 2 ;;
        --configure-only) mode=configure; shift ;;
        --) shift; configure_args+=("$@"); break ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done
cd "$source_dir"
case "$preset" in release|debug|arm_release|arm_debug|x86_release|x86_debug|universal_release|universal_debug) ;; *) echo 'Select a project configure/build preset.' >&2; exit 1 ;; esac
command -v "$cmake_tool" >/dev/null || { echo 'Install CMake >= 3.24 on PATH or pass --cmake.' >&2; exit 1; }
if [[ "$(uname -s)" == Darwin ]]; then
    xcrun --find clang >/dev/null || { echo 'Install/select Xcode or Command Line Tools; check DEVELOPER_DIR.' >&2; exit 1; }
    xcrun --show-sdk-path >/dev/null || { echo 'No macOS SDK in the selected Apple developer directory.' >&2; exit 1; }
fi
"$cmake_tool" --preset "$preset" ${configure_args[@]+"${configure_args[@]}"}
if [[ "$mode" == build ]]; then
    "$cmake_tool" --build --preset "$preset"
fi
