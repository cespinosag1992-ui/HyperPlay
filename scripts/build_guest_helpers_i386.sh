#!/bin/bash
# =============================================================================
# HyperPlay - build_guest_helpers_i386.sh  (32-bit)
#
# i386 builds of the guest libraries FEX runs inside 32-bit (i386) processes of
# the classic-WoW64 Wine runtimes (32-bit games). Same sources as the x86_64 ones:
#   app/src/main/assets/bootstrap/wxfix/i386/libhp_wxfix.so            (execmod workaround, required)
#   app/src/main/assets/bootstrap/sdlpad/i386/libhp_sdlpad.so          (gamepads for winebus/SDL)
#   app/src/main/assets/bootstrap/audio/i386/libasound_module_pcm_hyperplay.so  (ALSA -> AAudio)
# install_engine.sh / init_hyperplay_env.sh / audio/setup_audio.sh install them in
# /usr/lib/i386-linux-gnu/... of the FEX rootfs; /etc/ld.so.preload uses /usr/$LIB/... so
# each process loads the build of its own architecture.
#
# Needs an i686 cross gcc (Ubuntu gcc-i686-linux-gnu) and, for the ALSA plugin, a
# sysroot with the i386 libasound2-dev + libasound2t64 packages extracted (dpkg -x).
# What was used: the phone's own container (aarch64 Ubuntu 24.04, `proot -0`) with
# SYSROOT=/root/hp_mesa/sysroot-i386 (same sysroot as build_turnip_kgsl.sh --arch i386).
#
# Usage: scripts/build_guest_helpers_i386.sh [--cc i686-linux-gnu-gcc] [--sysroot DIR]
# =============================================================================
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="${CC:-i686-linux-gnu-gcc}"
SYSROOT="${SYSROOT:-}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cc) CC="$2"; shift 2 ;;
    --sysroot) SYSROOT="$2"; shift 2 ;;
    -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
    *) echo "argumento desconocido: $1" >&2; exit 2 ;;
  esac
done
command -v "$CC" >/dev/null || { echo "falta $CC (paquete gcc-i686-linux-gnu)" >&2; exit 1; }
STRIP="${CC%gcc}strip"
command -v "$STRIP" >/dev/null || STRIP=strip
A="${ROOT}/app/src/main/assets/bootstrap"
PROTO_DIR="${ROOT}/app/src/main/cpp/input"

out() { mkdir -p "$(dirname "$1")"; }

# 1. W^X / execmod shim (freestanding; syscall() + __errno_location resolved at run time).
W="$A/wxfix/i386/libhp_wxfix.so"; out "$W"
"$CC" -O2 -shared -fPIC -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
  -Wl,-soname,libhp_wxfix.so -o "$W.tmp" "$A/wxfix/hp_wxfix.c"
mv -f "$W.tmp" "$W"

# 2. SDL gamepad shim for winebus.
S="$A/sdlpad/i386/libhp_sdlpad.so"; out "$S"
"$CC" -O2 -shared -fPIC -Wall -Wextra -Werror -I "$PROTO_DIR" -Wl,-soname,libhp_sdlpad.so -Wl,--as-needed \
  -o "$S.tmp" "$A/sdlpad/hp_sdlpad.c" -lpthread -ldl
"$STRIP" --strip-unneeded "$S.tmp"
mv -f "$S.tmp" "$S"

# 3. ALSA PCM plugin.
[[ -n "$SYSROOT" && -f "$SYSROOT/usr/include/alsa/pcm_ioplug.h" ]] \
  || { echo "--sysroot sin usr/include/alsa (libasound2-dev i386 extraído)" >&2; exit 1; }
P="$A/audio/i386/libasound_module_pcm_hyperplay.so"; out "$P"
"$CC" -std=gnu11 -O2 -fPIC -DPIC -shared -Wall -Wextra -Werror -fvisibility=default \
  -I"$SYSROOT/usr/include" "$A/audio/hp_alsa_pcm.c" -o "$P.tmp" \
  -L"$SYSROOT/usr/lib/i386-linux-gnu" -l:libasound.so.2 -Wl,-soname,libasound_module_pcm_hyperplay.so -Wl,--no-undefined
"$STRIP" --strip-unneeded "$P.tmp"
mv -f "$P.tmp" "$P"

for f in "$W" "$S" "$P"; do
  printf '%s %s bytes, max %s\n' "$f" "$(stat -c %s "$f")" \
    "$("${CC%gcc}objdump" -T "$f" 2>/dev/null | grep -o 'GLIBC_2\.[0-9.]*' | sort -uV | tail -1)"
done
