#!/usr/bin/env bash
set -euo pipefail
nala_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$nala_root" -B "$nala_root/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$@"
cmake --build "$nala_root/build" --parallel "${NALA_BUILD_JOBS:-4}"
