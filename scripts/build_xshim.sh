#!/bin/bash
# Builds assets/bootstrap/xshim/libhp_xshim.so (aarch64 glibc LD_PRELOAD for Xvfb/xkbcomp in
# direct mode). Needs gcc-aarch64-linux-gnu (Ubuntu 24.04: glibc 2.39, same as the rootfs).
set -Eeuo pipefail
cd "$(dirname "$0")/.."
src=app/src/main/assets/bootstrap/xshim/hp_xshim.c
out=app/src/main/assets/bootstrap/xshim/libhp_xshim.so
aarch64-linux-gnu-gcc -O2 -shared -fPIC -Wall -Wextra -Werror -Wl,-soname,libhp_xshim.so -o "$out.tmp" "$src" -ldl
mv -f "$out.tmp" "$out"
aarch64-linux-gnu-objdump -T "$out" | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -1
ls -la "$out"
