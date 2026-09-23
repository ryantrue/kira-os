#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
idf.py update-dependencies
idf.py fullclean
bash scripts/configure.sh
idf.py build
echo
echo "dependencies.lock changes:"
git --no-pager diff --stat -- dependencies.lock || true
echo "Review dependencies.lock and hardware-test rev1.3 before committing an upgrade."
