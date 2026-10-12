#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m venv .venv
.venv/bin/python -m pip install -e .
.venv/bin/python -m unittest discover -s tests -v
cmake -S . -B build/cloud -DBUILD_BWAPI_BOT=OFF
cmake --build build/cloud --parallel 2
ctest --test-dir build/cloud --output-on-failure
# Optional, when replay data is available to this environment:
# .venv/bin/python scripts/bootstrap.py
