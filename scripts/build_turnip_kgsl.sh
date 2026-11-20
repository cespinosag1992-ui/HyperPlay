#!/bin/bash
# =============================================================================
# HyperPlay - build_turnip_kgsl.sh
#
# Builds Mesa Turnip (freedreno Vulkan) with the KGSL backend for glibc and
# packages it as an AdrenoTools-style zip (meta.json + libvulkan_freedreno.so)
# that TurnipDriverManager imports (assets/bootstrap/turnip/ or a user import).
#
# Target: Adreno 830 (a8xx gen8, KGSL chip id 0x44050001), Mesa >= 26.3.
#
# Usage:
#   scripts/build_turnip_kgsl.sh [--arch aarch64|x86_64|i386] [--work DIR] [--out DIR]
#                                [--install-deps] [--package-only LIB.so]
#
#   --arch aarch64  (default) the driver the app uses: the container's aarch64
#                   Vulkan loader loads it; x86 Wine/DXVK reach it through the
#                   FEX Vulkan thunk.
#   --arch x86_64   a driver FEX runs as guest code (no thunk). Works too, but
#                   TurnipDriverManager only accepts aarch64 builds.
#   --arch i386     (32-bit) the driver for i386 processes (classic-WoW64 Wine,
#                   32-bit games): FEX has no 32-bit Vulkan thunk, so the i386
#                   Vulkan loader of the FEX rootfs loads this build as guest code.
#                   Patch 0003 gives msm_kgsl.h LP64 layouts on __i386__ (FEX
#                   forwards KGSL ioctls raw to a 64-bit kernel ABI). Cross build:
#                   i686-linux-gnu-gcc + SYSROOT with the i386 -dev packages.
#   --install-deps  apt-get the build dependencies (Ubuntu 24.04, root).
#   --package-only  skip the build and only package an existing .so.
#
# Where to run it: natively on aarch64 Ubuntu 24.04 (what was used: inside the
# phone's own container, `proot -0`, ~4 min on the SM8750), or on any Linux
# with a cross compiler: set CROSS_PREFIX (e.g. aarch64-linux-gnu-) and
# SYSROOT (target -dev packages extracted with `dpkg -x`).
#
# Host tools: python3 (+mako, yaml, packaging), ninja, pkg-config, bison, flex,
# glslangValidator (glslang-tools), meson >= 1.5 (downloaded from PyPI and
# pinned below when the system one is older, e.g. Ubuntu 24.04's 1.3.2).
# Patches: third_party/mesa-patches/*.patch are applied in order.
# =============================================================================
set -Eeuo pipefail

MESA_VERSION=26.3.0
MESA_URL="https://gitlab.freedesktop.org/mesa/mesa/-/archive/mesa-${MESA_VERSION}/mesa-mesa-${MESA_VERSION}.tar.gz"
MESA_SHA256=d9b055a42fc744e8a1eca722193023100d78e969e222a49f68f33cbda07543ad
MESON_VERSION=1.9.1
MESON_URL="https://files.pythonhosted.org/packages/9c/07/b48592d325cb86682829f05216e4efb2dc881762b8f1bafb48b57442307a/meson-${MESON_VERSION}-py3-none-any.whl"
MESON_SHA256=f824ab770c041a202f532f69e114c971918ed2daff7ea56583d80642564598d0

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH=aarch64
WORK="${HOME}/hp_mesa"
OUT="${REPO_DIR}/dist"
INSTALL_DEPS=0
PACKAGE_ONLY=""

die() { printf 'build_turnip_kgsl: ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '[turnip] %s\n' "$*" >&2; }

while (( $# )); do
  case "$1" in
    --arch) ARCH="$2"; shift 2 ;;
    --work) WORK="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --install-deps) INSTALL_DEPS=1; shift ;;
    --package-only) PACKAGE_ONLY="$2"; shift 2 ;;
    -h|--help) sed -n '2,39p' "$0"; exit 0 ;;
    *) die "opción desconocida: $1" ;;
  esac
done
[[ "$ARCH" == aarch64 || "$ARCH" == x86_64 || "$ARCH" == i386 ]] || die "--arch debe ser aarch64, x86_64 o i386"
# 32-bit: GNU triplet / meson cpu_family / multiarch dir differ from the package arch name.
case "$ARCH" in
  i386) GNU_ARCH=i686; MESON_CPU_FAMILY=x86; MULTIARCH=i386-linux-gnu ;;
  *)    GNU_ARCH="$ARCH"; MESON_CPU_FAMILY="$ARCH"; MULTIARCH="${ARCH}-linux-gnu" ;;
esac

# Revisión del paquete: súbela al cambiar los parches (TurnipDriverManager solo re-adopta el driver
# incluido en el APK si cambia el nombre del zip). r2 = parches 0001 + 0002 (ritmo de la WSI sw).
# 32-bit: r3 = 0001 + 0002 + 0003 (0003 only changes i386 builds; the 64-bit packages stay r2).
if [[ "$ARCH" == i386 ]]; then PKG_REV=3; else PKG_REV=2; fi
PKG_NAME="turnip-kgsl-${MESA_VERSION}-r${PKG_REV}-${ARCH}-glibc"

package() {   # package <libvulkan_freedreno.so>
  local lib="$1"
  [[ -f "$lib" ]] || die "no existe $lib"
  mkdir -p "$OUT"
  python3 -I - "$lib" "$OUT/${PKG_NAME}.zip" "$MESA_VERSION" "$ARCH" "$PKG_REV" <<'EOF'
import json, sys, zipfile
lib, out, ver, arch = sys.argv[1:5]
meta = {
    "schemaVersion": 1,
    "name": "Turnip KGSL",
    "description": f"Mesa {ver} Turnip (freedreno Vulkan), KGSL backend, glibc {arch}, Adreno 830 (a8xx)",
    "author": "Mesa (MIT); built by HyperPlay scripts/build_turnip_kgsl.sh",
    "packageVersion": sys.argv[5],
    "vendor": "Mesa",
    "driverVersion": ver,
    "minApi": 28,
    "libraryName": "libvulkan_freedreno.so",
}
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr("meta.json", json.dumps(meta, indent=2) + "\n")
    z.write(lib, "libvulkan_freedreno.so")
print(out)
EOF
  (cd "$OUT" && sha256sum "${PKG_NAME}.zip" > "${PKG_NAME}.zip.sha256")
  log "paquete: $OUT/${PKG_NAME}.zip ($(stat -c %s "$OUT/${PKG_NAME}.zip") bytes)"
}

if [[ -n "$PACKAGE_ONLY" ]]; then
  package "$PACKAGE_ONLY"
  exit 0
fi

# ---- Dependencies -------------------------------------------------------------
if (( INSTALL_DEPS )); then
  apt-get update
  apt-get install -y --no-install-recommends gcc g++ libc6-dev pkg-config ninja-build \
    python3-mako python3-yaml python3-packaging glslang-tools bison flex curl patch \
    libdrm-dev libexpat1-dev zlib1g-dev libzstd-dev libx11-dev libx11-xcb-dev libxcb1-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-sync-dev \
    libxcb-xfixes0-dev libxcb-keysyms1-dev libxshmfence-dev libxrandr-dev libxext-dev
fi
for t in python3 ninja pkg-config glslangValidator curl sha256sum patch; do
  command -v "$t" >/dev/null || die "falta $t"
done
python3 -c 'import mako, yaml, packaging' 2>/dev/null || die "faltan los módulos python mako/yaml/packaging"

mkdir -p "$WORK"
cd "$WORK"

fetch() {   # fetch <url> <file> <sha256>
  if [[ ! -f "$2" ]] || ! echo "$3  $2" | sha256sum -c --status; then
    log "descargando $(basename "$2")"
    curl -fL --retry 3 -o "$2.part" "$1"
    mv "$2.part" "$2"
  fi
  echo "$3  $2" | sha256sum -c --status || { rm -f "$2"; die "sha256 incorrecto en $2"; }
}

# ---- Meson (>= 1.5) ---------------------------------------------------------------
MESON=(meson)
if ! command -v meson >/dev/null || ! python3 - "$(meson --version 2>/dev/null || echo 0)" <<'EOF'
import sys
from packaging.version import Version
sys.exit(0 if Version(sys.argv[1]) >= Version("1.5") else 1)
EOF
then
  fetch "$MESON_URL" "meson-${MESON_VERSION}-py3-none-any.whl" "$MESON_SHA256"
  rm -rf mesonpkg && mkdir mesonpkg
  python3 -I -m zipfile -e "meson-${MESON_VERSION}-py3-none-any.whl" mesonpkg
  printf '#!/usr/bin/env python3\nimport sys\nfrom mesonbuild import mesonmain\nsys.exit(mesonmain.main())\n' > mesonpkg/meson.py
  chmod +x mesonpkg/meson.py
  MESON=(python3 "$WORK/mesonpkg/meson.py")
fi
log "meson $("${MESON[@]}" --version)"

# ---- Mesa source + patches ----------------------------------------------------
fetch "$MESA_URL" "mesa-${MESA_VERSION}.tar.gz" "$MESA_SHA256"
SRC="$WORK/mesa-mesa-${MESA_VERSION}"
rm -rf "$SRC"
tar xzf "mesa-${MESA_VERSION}.tar.gz"
for p in "$REPO_DIR"/third_party/mesa-patches/*.patch; do
  [[ -f "$p" ]] || continue
  log "parche: $(basename "$p")"
  patch -d "$SRC" -p1 --forward < "$p"
done

# ---- Cross file (only when the host is not the target) ------------------------
CROSS=()
if [[ "$(uname -m)" != "$ARCH" ]]; then
  P="${CROSS_PREFIX:-${GNU_ARCH}-linux-gnu-}"
  command -v "${P}gcc" >/dev/null || die "host $(uname -m) != $ARCH y falta ${P}gcc (define CROSS_PREFIX/SYSROOT)"
  SR="${SYSROOT:-}"
  LIBD="${SR}/usr/lib/${MULTIARCH}"
  cat > "$WORK/cross-${ARCH}.ini" <<EOF
[binaries]
c = '${P}gcc'
cpp = '${P}g++'
ar = '${P}ar'
strip = '${P}strip'
pkg-config = 'pkg-config'

[properties]
sys_root = '${SR}'
pkg_config_libdir = ['${LIBD}/pkgconfig', '${SR}/usr/share/pkgconfig']

[built-in options]
c_args = ['-I${SR}/usr/include']
cpp_args = ['-I${SR}/usr/include']
c_link_args = ['-L${LIBD}', '-Wl,-rpath-link,${LIBD}']
cpp_link_args = ['-L${LIBD}', '-Wl,-rpath-link,${LIBD}']

[host_machine]
system = 'linux'
cpu_family = '${MESON_CPU_FAMILY}'
cpu = '${GNU_ARCH}'
endian = 'little'
EOF
  CROSS=(--cross-file "$WORK/cross-${ARCH}.ini")
fi

# ---- Build ------------------------------------------------------------------------
BUILD="$SRC/build-${ARCH}"
"${MESON[@]}" setup "$BUILD" "$SRC" "${CROSS[@]}" --wrap-mode=nodownload \
  -Dprefix=/usr -Dbuildtype=release -Dstrip=true \
  -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dgallium-drivers= \
  -Dplatforms=x11 -Dvulkan-beta=true \
  -Dglx=disabled -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dgbm=disabled \
  -Dllvm=disabled -Dvideo-codecs= -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
  -Dbuild-tests=false -Dzstd=enabled
ninja -C "$BUILD" src/freedreno/vulkan/libvulkan_freedreno.so
package "$BUILD/src/freedreno/vulkan/libvulkan_freedreno.so"
