#!/usr/bin/env bash
set -euo pipefail
nala_script="$(readlink -f -- "${BASH_SOURCE[0]}")"
nala_root="$(cd -- "$(dirname -- "$nala_script")/.." && pwd)"
if [[ ! -x "$nala_root/build/nala" ]]; then "$nala_root/scripts/build.sh"; fi
exec "$nala_root/build/nala" "$@"
