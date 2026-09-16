#!/usr/bin/env bash
# Record a scripted sequence at 60 fps into build/film, one PNG per frame, for
# comparing her motion against the reference frame by frame. Needs a display.
set -euo pipefail
nala_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
"$nala_root/scripts/build.sh"
exec "$nala_root/build/nala" --preview --film "$nala_root/build/film"
