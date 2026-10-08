#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]]; then
    source "$IDF_PATH/export.sh"
elif [[ -f /opt/esp-idf/export.sh ]]; then
    source /opt/esp-idf/export.sh
elif [[ -f /opt/esp/idf/export.sh ]]; then
    source /opt/esp/idf/export.sh
else
    printf '%s\n' 'ESP-IDF is missing. Set IDF_PATH or run this script in esp-dev.' >&2
    exit 1
fi
cd "$project_root"
exec idf.py -B "$project_root/build/idf" "$@"
