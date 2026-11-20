#!/bin/bash
# =============================================================================
# HyperPlay - build_xinput_dlls.sh
#
# Compila las DLL XInput de HyperPlay (windows/xinput/hp_xinput.c) como PE x64 y x86 con
# mingw-w64 y las deja en app/src/main/assets/bootstrap/xinput/{x64,x86}/:
#   xinput1_1.dll xinput1_2.dll xinput1_3.dll xinput1_4.dll xinput9_1_0.dll
# Además genera en build/xinput/{x64,x86}/ test_xinput.exe (imprime el estado, para Wine) y
# selfcheck_xinput.exe <dll> (prueba automática: hace de lado Android; Windows o Wine).
#
# Requisitos: x86_64-w64-mingw32-gcc e i686-w64-mingw32-gcc (paquetes Ubuntu
# gcc-mingw-w64-x86-64-win32 y gcc-mingw-w64-i686-win32). Si no están en PATH:
#   --prefix DIR   raíz donde están DIR/usr/bin/<triplete>-gcc (p. ej. paquetes extraídos con
#                  `apt-get download ... && dpkg -x pkg.deb DIR`, sin root).
# Uso: scripts/build_xinput_dlls.sh [--prefix DIR] [--out DIR]
# =============================================================================
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/windows/xinput"
PROTO_DIR="${ROOT}/app/src/main/cpp/input"
OUT="${ROOT}/app/src/main/assets/bootstrap/xinput"
BUILD="${ROOT}/build/xinput"
PREFIX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    *) echo "argumento desconocido: $1" >&2; exit 2 ;;
  esac
done

find_cc() {
  local triple="$1" c
  for c in "${PREFIX:+${PREFIX}/usr/bin/}${triple}-gcc" "${PREFIX:+${PREFIX}/usr/bin/}${triple}-gcc-win32"; do
    if command -v "$c" >/dev/null 2>&1 || [[ -x "$c" ]]; then echo "$c"; return 0; fi
  done
  echo "no se encuentra ${triple}-gcc (instala gcc-mingw-w64 o usa --prefix)" >&2
  return 1
}

DLLS=(xinput1_1 xinput1_2 xinput1_3 xinput1_4 xinput9_1_0)
CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -I "$PROTO_DIR" -fno-ident -ffunction-sections -fdata-sections)
LDFLAGS=(-shared -s -static-libgcc -Wl,--enable-stdcall-fixup -Wl,--kill-at -Wl,--gc-sections
         -Wl,--dynamicbase -Wl,--nxcompat -Wl,--no-insert-timestamp)

build_arch() {
  local arch="$1" triple="$2" cc bindir
  cc="$(find_cc "$triple")"
  bindir="$(dirname "$(command -v "$cc" || echo "$cc")")"
  # Paquetes extraídos fuera de /usr: gcc busca as/ld en su prefijo de instalación.
  local bflag=()
  [[ -n "$PREFIX" ]] && bflag=(-B "${PREFIX}/usr/bin/${triple}-" -B "${PREFIX}/usr/${triple}/bin/")
  mkdir -p "${OUT}/${arch}" "${BUILD}/${arch}"
  echo "== ${arch}: $("$cc" --version | head -1) (${bindir})"
  "$cc" "${bflag[@]}" "${CFLAGS[@]}" -c "${SRC}/hp_xinput.c" -o "${BUILD}/${arch}/hp_xinput.o"
  local name
  for name in "${DLLS[@]}"; do
    sed "s/@DLLNAME@/${name}.dll/" "${SRC}/hp_xinput.def.in" > "${BUILD}/${arch}/${name}.def"
    "$cc" "${bflag[@]}" "${BUILD}/${arch}/hp_xinput.o" "${BUILD}/${arch}/${name}.def" "${LDFLAGS[@]}" \
      -o "${OUT}/${arch}/${name}.dll" -lkernel32
  done
  "$cc" "${bflag[@]}" -std=c11 -O2 -Wall -Wextra -Werror -s "${SRC}/test_xinput.c" -o "${BUILD}/${arch}/test_xinput.exe"
  "$cc" "${bflag[@]}" "${CFLAGS[@]}" -s "${SRC}/selfcheck_xinput.c" -o "${BUILD}/${arch}/selfcheck_xinput.exe"
  ls -l "${OUT}/${arch}"
}

build_arch x64 x86_64-w64-mingw32
build_arch x86 i686-w64-mingw32

# Librería invitada x86_64 (glibc, FEX rootfs) que da a winebus un mando HID por slot.
SDLPAD="${ROOT}/app/src/main/assets/bootstrap/sdlpad"
HOSTCC="${HOSTCC:-gcc}"
"$HOSTCC" -O2 -shared -fPIC -Wall -Wextra -Werror -I "$PROTO_DIR" -Wl,-soname,libhp_sdlpad.so -Wl,--as-needed \
  -o "${SDLPAD}/libhp_sdlpad.so" "${SDLPAD}/hp_sdlpad.c" -lpthread -ldl
strip --strip-unneeded "${SDLPAD}/libhp_sdlpad.so"
echo "== sdlpad: $("$HOSTCC" --version | head -1) -> ${SDLPAD}/libhp_sdlpad.so"
echo "DLL en ${OUT}; test_xinput.exe en ${BUILD}/{x64,x86}"
