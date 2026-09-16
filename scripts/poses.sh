#!/usr/bin/env bash
# Render one PNG per form into build/poses for comparison against the
# reference animation. Needs a real display: the body is a GPU shader.
set -euo pipefail
nala_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
"$nala_root/scripts/build.sh"
exec "$nala_root/build/nala" --preview --poses "$nala_root/build/poses"
