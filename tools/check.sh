#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$project_root/tests" -B "$project_root/build/host" -G Ninja -DC3_SANITIZE=ON
cmake --build "$project_root/build/host"
ctest --test-dir "$project_root/build/host" --output-on-failure
