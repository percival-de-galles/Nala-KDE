#!/usr/bin/env bash
# Behaviour checks. These run offscreen, so they skip the appearance
# assertions -- the offscreen platform has no GPU surface and the body is a
# shader. Use poses.sh to check how she looks.
set -euo pipefail
nala_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
"$nala_root/scripts/build.sh"
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
    "$nala_root/build/nala" --self-test \
    --capture-dir "$nala_root/build/test-captures"
