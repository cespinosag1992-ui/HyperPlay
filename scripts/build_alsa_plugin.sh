#!/bin/bash
# =============================================================================
# HyperPlay - build_alsa_plugin.sh
#
# Builds the x86_64 ALSA PCM plugin "hyperplay" (assets/bootstrap/audio/hp_alsa_pcm.c)
# that Wine's winealsa uses inside the FEX guest:
#   app/src/main/assets/bootstrap/audio/libasound_module_pcm_hyperplay.so
#
# Needs an x86_64 Linux host (WSL Ubuntu 24.04) with gcc. The ALSA headers come from
# Ubuntu's own packages (libasound2-dev + libasound2t64, same 1.2.11 as the FEX rootfs),
# downloaded without root with `apt-get download` into --prefix (default ~/alsadev).
#
# Usage: scripts/build_alsa_plugin.sh [--prefix DIR] [--test]
#   --test  also builds and runs app/src/test/cpp/HpAlsaPluginTest.c (fake server).
# =============================================================================
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${HOME}/alsadev"
RUN_TEST=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --test) RUN_TEST=1; shift ;;
    *) echo "argumento desconocido: $1" >&2; exit 2 ;;
  esac
done

[[ "$(uname -m)" == x86_64 ]] || { echo "se necesita un host x86_64" >&2; exit 1; }
SYSROOT="${PREFIX}/root"
if [[ ! -f "${SYSROOT}/usr/include/alsa/pcm_ioplug.h" ]]; then
  mkdir -p "${PREFIX}/debs"
  ( cd "${PREFIX}/debs" && apt-get download libasound2-dev libasound2t64 )
  for d in "${PREFIX}"/debs/*.deb; do dpkg -x "$d" "$SYSROOT"; done
fi
LIBDIR="${SYSROOT}/usr/lib/x86_64-linux-gnu"
SRC="${ROOT}/app/src/main/assets/bootstrap/audio/hp_alsa_pcm.c"
OUT="${ROOT}/app/src/main/assets/bootstrap/audio/libasound_module_pcm_hyperplay.so"

gcc -std=gnu11 -O2 -fPIC -DPIC -shared -Wall -Wextra -Werror -fvisibility=default \
  -I"${SYSROOT}/usr/include" "$SRC" -o "$OUT.tmp" \
  -L"$LIBDIR" -l:libasound.so.2 -Wl,-soname,libasound_module_pcm_hyperplay.so -Wl,--no-undefined
strip --strip-unneeded "$OUT.tmp"
mv -f "$OUT.tmp" "$OUT"
echo "ok: $OUT ($(stat -c %s "$OUT") bytes)"

if (( RUN_TEST )); then
  T="$(mktemp -d)"
  gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"${SYSROOT}/usr/include" \
    "${ROOT}/app/src/test/cpp/HpAlsaPluginTest.c" -o "$T/hp_alsa_test" -L"$LIBDIR" -l:libasound.so.2 -lpthread -lm
  LD_LIBRARY_PATH="$LIBDIR" "$T/hp_alsa_test" "$OUT"
fi
