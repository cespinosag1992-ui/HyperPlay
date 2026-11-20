#!/bin/bash
# =============================================================================
# HyperPlay - build_direct_bins.sh <dir-with-rootfs-binaries>
#
# Direct mode (no PRoot) needs a few aarch64 glibc programs of the rootfs to be exec()'d
# straight from the app. Android forbids exec of files in app data (W^X, targetSdk >= 29),
# so copies live in the APK (nativeLibraryDir is executable) as lib*.so, with:
#   - PT_INTERP -> a copy of the rootfs's own loader patched by the app (DirectLoader in EngineMode.kt:
#     set_robust_list/rseq/clone3/faccessat2 return -ENOSYS instead of trapping in Android's seccomp filter, which
#     kills the process before any SIGSYS handler exists):
#     /data/data/<pkg>/files/rootfs/opt/hyperplay/direct/ld-linux-aarch64.so.1 (/data/data/<pkg> is
#     the current user's data dir in the app's mount namespace)
#   - DT_RPATH  -> opt/hyperplay/direct (patched libc.so.6, same LdSoPatch) and the rootfs's library
#     dirs (DT_RPATH also applies to dlopen: FEX host thunks, Vulkan)
# FEX must be exec'd this way (not via "ld.so FEX"): it re-executes itself through
# /proc/self/exe for every x86 execve.
#
# Input: FEX FEXServer Xvfb xkbcomp bash, as copied from <rootfs>/usr/bin (e.g.
#   adb exec-out run-as com.murdoc820.hyperplay cat files/rootfs/usr/bin/FEX > dir/FEX).
# Output: app/src/main/jniLibs/arm64-v8a/libhpx_*.so and
#   app/src/main/assets/bootstrap/direct/origins.sha256 (sha256 of each original + its rootfs
#   path: direct mode is only used when the installed rootfs has exactly those binaries).
# Needs patchelf (apt install patchelf).
# =============================================================================
set -Eeuo pipefail
in="$(cd "$1" && pwd)"
cd "$(dirname "$0")/.."
pkg=com.murdoc820.hyperplay
root=/data/data/$pkg/files/rootfs
out=app/src/main/jniLibs/arm64-v8a
sums=app/src/main/assets/bootstrap/direct/origins.sha256
mkdir -p "$out" "$(dirname "$sums")"
# SIGSYS -> ENOSYS (seccomp de las apps de Android): DT_NEEDED de cada binario, ruta absoluta en el
# rootfs (BootstrapAssets la copia desde assets/bootstrap/direct/).
sigsys_src=app/src/main/assets/bootstrap/direct/hp_sigsys.c
sigsys_so=app/src/main/assets/bootstrap/direct/libhp_sigsys.so
aarch64-linux-gnu-gcc -O2 -shared -fPIC -Wall -Wextra -Werror -Wl,-soname,libhp_sigsys.so -o "$sigsys_so" "$sigsys_src"
sigsys_needed="$root/opt/hyperplay/bootstrap/direct/libhp_sigsys.so"
: > "$sums.tmp"
for pair in FEX:fex FEXServer:fexserver Xvfb:xvfb xkbcomp:xkbcomp bash:bash; do
  src="${pair%%:*}" name="${pair##*:}"
  [[ -f "$in/$src" ]] || { echo "falta $in/$src" >&2; exit 1; }
  cp -f "$in/$src" "$out/libhpx_$name.so.tmp"
  patchelf --set-interpreter "$root/opt/hyperplay/direct/ld-linux-aarch64.so.1" \
    --force-rpath --set-rpath "$root/opt/hyperplay/direct:$root/usr/lib/aarch64-linux-gnu:$root/lib/aarch64-linux-gnu" \
    --add-needed "$sigsys_needed" \
    "$out/libhpx_$name.so.tmp"
  mv -f "$out/libhpx_$name.so.tmp" "$out/libhpx_$name.so"
  printf '%s usr/bin/%s\n' "$(sha256sum "$in/$src" | cut -d' ' -f1)" "$src" >> "$sums.tmp"
done
mv -f "$sums.tmp" "$sums"
cat "$sums"
ls -la "$out"/libhpx_*.so
