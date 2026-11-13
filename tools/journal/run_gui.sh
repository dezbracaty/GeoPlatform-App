#!/bin/bash
# One launcher for local and remote browser clients. No dependency installation.
set -e
cd "$(dirname "$0")/../.."
exec "${GPLATFORM_JOURNAL_PYTHON:-python3}" tools/journal/journal_server.py "$@"
