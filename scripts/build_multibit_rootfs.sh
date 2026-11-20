#!/usr/bin/env bash
# =============================================================================
# HyperPlay - build_multibit_rootfs.sh
#
# Builds hyperplay-rootfs-arm64.tar.zst: an Ubuntu 24.04 (noble) aarch64/glibc
# rootfs that, under PRoot on Android (Snapdragon 8 Elite / Adreno 830), runs
# x86 (PE32) and x86_64 (PE32+) Windows games through:
#
#   FEX-Emu (aarch64, official PPA) + FEX x86 guest rootfs (Ubuntu 24.04)
#   + Wine WOW64 (Kron4ek amd64-wow64 build, no i386 multiarch needed)
#   + DXVK (x64/x32, includes d3d8.dll) + VKD3D-Proton (x64/x86)
#   + Xvfb (display bridge source, see CLAUDE.md 6.1)
#
# Every pinned artifact below can be overridden through the environment.
# Run `build_multibit_rootfs.sh -h` for usage.
# =============================================================================
set -Eeuo pipefail
shopt -s nullglob

# ----------------------------------------------------------------------------
# Pinned artifacts: single source of truth shared with the on-device installer
# (app/src/main/assets/bootstrap/engine_versions.env). Env overrides still win.
# ----------------------------------------------------------------------------
HP_VERSIONS_FILE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../app/src/main/assets/bootstrap/engine_versions.env"
[[ -f "$HP_VERSIONS_FILE" ]] || { echo "missing $HP_VERSIONS_FILE" >&2; exit 1; }
# shellcheck source=../app/src/main/assets/bootstrap/engine_versions.env
source "$HP_VERSIONS_FILE"
# aarch64 packages installed into the rootfs (all --no-install-recommends).
read -r -a ROOTFS_PACKAGES <<< "$HP_ENGINE_PACKAGES"

OUTPUT_NAME="hyperplay-rootfs-arm64.tar.zst"
HP_PREFIX="/opt/hyperplay"
HP_UID=1000
HP_USER="hyperplay"

# ----------------------------------------------------------------------------
# Logging
# ----------------------------------------------------------------------------
if [[ -t 2 ]]; then
  C_INFO=$'\033[1;36m'; C_WARN=$'\033[1;33m'; C_ERR=$'\033[1;31m'; C_OK=$'\033[1;32m'; C_RST=$'\033[0m'
else
  C_INFO=""; C_WARN=""; C_ERR=""; C_OK=""; C_RST=""
fi
log()  { printf '%s[hyperplay]%s %s\n' "$C_INFO" "$C_RST" "$*" >&2; }
ok()   { printf '%s[  ok  ]%s %s\n' "$C_OK" "$C_RST" "$*" >&2; }
warn() { printf '%s[ warn ]%s %s\n' "$C_WARN" "$C_RST" "$*" >&2; }
die()  { printf '%s[error ]%s %s\n' "$C_ERR" "$C_RST" "$*" >&2; exit 1; }
step() { printf '\n%s==> %s%s\n' "$C_INFO" "$*" "$C_RST" >&2; }

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Builds ${OUTPUT_NAME} (Ubuntu 24.04 arm64 + FEX-Emu + FEX x86 rootfs +
Wine ${WINE_VERSION} WOW64 + DXVK ${DXVK_VERSION} + VKD3D-Proton ${VKD3D_VERSION} + Xvfb).

Options:
  --out DIR           Output directory for the .tar.zst and .sha256 (default: \$PWD)
  --work DIR          Work directory; downloads are cached in DIR/cache
                      (default: /var/tmp/hyperplay-rootfs). Must be a native
                      Linux filesystem (not /mnt/c on WSL).
  --keep-work         Keep the staging rootfs (DIR/rootfs) after packing
  --skip-fex-rootfs   Do not download/extract the FEX x86 guest rootfs
                      (FEX config still points at ${HP_PREFIX}/fex-rootfs)
  -h, --help          Show this help and exit

Host requirements:
  - Linux, run as root (chroot + bind mounts)
  - aarch64 host, or x86_64 host with qemu-user-static + binfmt_misc
    registered for aarch64 (Debian/Ubuntu: apt install qemu-user-static
    binfmt-support)
  - curl tar zstd xz sha256sum chroot mount umount findmnt (util-linux)
  - ~10 GB free in the work directory

Pinned versions can be overridden via env, e.g.:
  WINE_VERSION=11.20 WINE_SHA256=<sum> $(basename "$0")
  UBUNTU_BASE_POINT=24.04.5 FEX_PKG_CANDIDATES="fex-emu-armv8.2" $(basename "$0")
EOF
}

# ----------------------------------------------------------------------------
# Arguments
# ----------------------------------------------------------------------------
OUT_DIR="${PWD}"
WORK_DIR="/var/tmp/hyperplay-rootfs"
KEEP_WORK=0
SKIP_FEX_ROOTFS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)            [[ $# -ge 2 ]] || die "--out needs a directory"; OUT_DIR="$2"; shift 2 ;;
    --out=*)          OUT_DIR="${1#*=}"; shift ;;
    --work)           [[ $# -ge 2 ]] || die "--work needs a directory"; WORK_DIR="$2"; shift 2 ;;
    --work=*)         WORK_DIR="${1#*=}"; shift ;;
    --keep-work)      KEEP_WORK=1; shift ;;
    --skip-fex-rootfs) SKIP_FEX_ROOTFS=1; shift ;;
    -h|--help)        usage; exit 0 ;;
    *)                usage >&2; die "Unknown option: $1" ;;
  esac
done

# ----------------------------------------------------------------------------
# Globals derived later
# ----------------------------------------------------------------------------
ROOTFS=""
CACHE=""
QEMU_COPIED=""          # path inside rootfs of a copied qemu interpreter
MOUNTS=()               # mount points we created, in mount order
CACHE_MNT="/mnt/hpcache"

# ----------------------------------------------------------------------------
# Mount handling / cleanup
# ----------------------------------------------------------------------------
is_mounted() { findmnt -rn --mountpoint "$1" >/dev/null 2>&1; }

do_mount() {   # do_mount <target> <mount args...>
  local target="$1"; shift
  mkdir -p "$target"
  mount "$@" "$target"
  MOUNTS+=("$target")
}

unmount_all() {
  local i target
  for (( i=${#MOUNTS[@]}-1; i>=0; i-- )); do
    target="${MOUNTS[$i]}"
    if is_mounted "$target"; then
      umount "$target" 2>/dev/null || umount -l "$target" 2>/dev/null || warn "Could not unmount $target"
    fi
  done
  MOUNTS=()
  # Belt and braces: anything still mounted below the rootfs (deepest first).
  if [[ -n "$ROOTFS" && -d "$ROOTFS" ]]; then
    local m
    while IFS= read -r m; do
      [[ -n "$m" ]] || continue
      umount "$m" 2>/dev/null || umount -l "$m" 2>/dev/null || warn "Could not unmount $m"
    done < <(awk -v r="$ROOTFS" 'index($2, r"/")==1 {print $2}' /proc/self/mounts | sort -r)
  fi
}

assert_no_mounts_below() {
  local dir="$1"
  if awk -v r="$dir" '$2==r || index($2, r"/")==1 {found=1} END {exit !found}' /proc/self/mounts; then
    die "Refusing to touch $dir: filesystems are still mounted below it"
  fi
}

on_err() {
  local rc=$? line=${1:-?} cmd=${2:-?}
  printf '%s[error ]%s line %s: "%s" exited with %s\n' "$C_ERR" "$C_RST" "$line" "$cmd" "$rc" >&2
}

on_exit() {
  local rc=$?
  set +e
  unmount_all
  if [[ -n "$QEMU_COPIED" && -f "$QEMU_COPIED" ]]; then rm -f "$QEMU_COPIED"; fi
  if [[ $rc -ne 0 ]]; then
    warn "Build failed (exit $rc). Mounts released; work dir kept at ${WORK_DIR} for inspection."
  fi
  exit "$rc"
}
trap 'on_err "$LINENO" "$BASH_COMMAND"' ERR
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Run a script inside the chroot as root with a clean environment.
# Usage: in_chroot '<bash script>' [args...]  ($1.. inside the script)
in_chroot() {
  local script="$1"; shift
  env -i \
    HOME=/root TERM="${TERM:-dumb}" LANG=C.UTF-8 LC_ALL=C.UTF-8 \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true \
    chroot "$ROOTFS" /bin/bash -Eeuo pipefail -c "$script" hp-chroot "$@"
}

# ----------------------------------------------------------------------------
# Downloads (cached, verified)
# ----------------------------------------------------------------------------
sha256_of() { sha256sum "$1" | awk '{print $1}'; }

# fetch <url> <dest> [sha256]
fetch() {
  local url="$1" dest="$2" want="${3:-}"
  if [[ -f "$dest" ]]; then
    if [[ -z "$want" ]]; then
      ok "cached $(basename "$dest")"; return 0
    fi
    if [[ "$(sha256_of "$dest")" == "$want" ]]; then
      ok "cached $(basename "$dest") (sha256 ok)"; return 0
    fi
    warn "cached $(basename "$dest") has wrong sha256, re-downloading"
    rm -f "$dest"
  fi
  log "downloading $url"
  curl -fL --retry 4 --retry-delay 3 --connect-timeout 30 -C - -o "$dest.part" "$url" \
    || { rm -f "$dest.part"; curl -fL --retry 4 --retry-delay 3 --connect-timeout 30 -o "$dest.part" "$url"; }
  if [[ -n "$want" ]]; then
    local got; got="$(sha256_of "$dest.part")"
    if [[ "$got" != "$want" ]]; then
      rm -f "$dest.part"
      die "sha256 mismatch for $(basename "$dest"): expected $want, got $got"
    fi
    ok "sha256 verified $(basename "$dest")"
  fi
  mv -f "$dest.part" "$dest"
}

# ----------------------------------------------------------------------------
# 1. Host checks
# ----------------------------------------------------------------------------
host_checks() {
  step "Checking host"
  [[ "$(uname -s)" == "Linux" ]] || die "This script must run on Linux (got $(uname -s))."
  [[ ${EUID} -eq 0 ]] || die "Root is required (chroot, bind mounts, ownership). Re-run with: sudo $0 [options]"

  local missing=() t
  for t in curl tar zstd xz sha256sum chroot mount umount findmnt awk sed sort stat df env; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
  done
  [[ ${#missing[@]} -eq 0 ]] || die "Missing host tools: ${missing[*]} (Debian/Ubuntu: apt install curl tar zstd xz-utils coreutils util-linux)"

  local arch; arch="$(uname -m)"
  case "$arch" in
    aarch64|arm64)
      ok "native aarch64 host"
      ;;
    x86_64|amd64)
      local bf=/proc/sys/fs/binfmt_misc/qemu-aarch64
      if [[ ! -e /proc/sys/fs/binfmt_misc/status ]]; then
        die "x86_64 host without binfmt_misc mounted. Run: mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc"
      fi
      [[ -f "$bf" ]] || die "x86_64 host: no binfmt handler for aarch64. Install qemu-user-static + binfmt-support (Debian/Ubuntu: apt install qemu-user-static binfmt-support), or run on an aarch64 machine."
      grep -q '^enabled' "$bf" || die "binfmt handler $bf is registered but disabled (echo 1 > $bf)."
      QEMU_INTERP="$(awk '/^interpreter/ {print $2}' "$bf")"
      QEMU_FLAGS="$(awk '/^flags:/ {print $2}' "$bf")"
      [[ -n "$QEMU_INTERP" ]] || die "Cannot read interpreter from $bf"
      if [[ "$QEMU_FLAGS" == *F* ]]; then
        ok "x86_64 host, aarch64 via binfmt ($QEMU_INTERP, fix-binary flag set)"
      else
        [[ -x "$QEMU_INTERP" ]] || die "binfmt interpreter $QEMU_INTERP is not executable on the host"
        ok "x86_64 host, aarch64 via binfmt ($QEMU_INTERP, will be copied into the chroot)"
      fi
      warn "Emulated build: apt and unsquashfs run under qemu-user and will be slow."
      ;;
    *)
      die "Unsupported host architecture: $arch (need aarch64, or x86_64 with qemu-user-static)"
      ;;
  esac

  mkdir -p "$WORK_DIR" "$OUT_DIR"
  WORK_DIR="$(cd "$WORK_DIR" && pwd -P)"
  OUT_DIR="$(cd "$OUT_DIR" && pwd -P)"
  [[ "$WORK_DIR" != *[[:space:]]* ]] || die "Work directory path must not contain whitespace: $WORK_DIR"
  [[ "$WORK_DIR" != "/" ]] || die "Work directory cannot be /"

  local fstype; fstype="$(stat -f -c %T "$WORK_DIR")"
  case "$fstype" in
    v9fs|9p|drvfs|fuseblk|vfat|msdos|ntfs|ntfs3|exfat|smb2|cifs)
      die "Work directory $WORK_DIR is on '$fstype', which cannot hold a Linux rootfs (ownership/devices/symlinks). Use --work on ext4/btrfs/xfs, e.g. /var/tmp/hyperplay-rootfs." ;;
  esac
  local avail_gb; avail_gb="$(df -P -BG "$WORK_DIR" | awk 'NR==2 {gsub("G","",$4); print $4}')"
  if [[ "$avail_gb" =~ ^[0-9]+$ ]] && (( avail_gb < 10 )); then
    warn "Only ${avail_gb} GB free in $WORK_DIR; the build needs about 10 GB."
  fi

  CACHE="$WORK_DIR/cache"
  ROOTFS="$WORK_DIR/rootfs"
  mkdir -p "$CACHE"
  ok "work dir: $WORK_DIR  (cache: $CACHE)"
  ok "out dir:  $OUT_DIR"
}

# ----------------------------------------------------------------------------
# 2. Ubuntu Base
# ----------------------------------------------------------------------------
UBUNTU_BASE_TARBALL=""
fetch_ubuntu_base() {
  step "Ubuntu Base ${UBUNTU_SUITE} arm64"
  local sums="$CACHE/ubuntu-base-SHA256SUMS"
  curl -fsSL --retry 4 -o "$sums.part" "$UBUNTU_BASE_BASEURL/SHA256SUMS"
  mv -f "$sums.part" "$sums"

  local name
  if [[ "$UBUNTU_BASE_POINT" == "auto" ]]; then
    name="$(curl -fsSL --retry 4 "$UBUNTU_BASE_BASEURL/" \
      | grep -oE 'ubuntu-base-24\.04(\.[0-9]+)?-base-arm64\.tar\.gz' | sort -uV | tail -n1)"
    [[ -n "$name" ]] || die "Could not find ubuntu-base-24.04.*-base-arm64.tar.gz in $UBUNTU_BASE_BASEURL/"
  else
    name="ubuntu-base-${UBUNTU_BASE_POINT}-base-arm64.tar.gz"
  fi
  local want
  want="$(awk -v n="$name" '{f=$2; sub(/^\*/, "", f); if (f==n) print $1}' "$sums")"
  [[ "$want" =~ ^[0-9a-f]{64}$ ]] || die "No SHA256 for $name in $UBUNTU_BASE_BASEURL/SHA256SUMS"
  fetch "$UBUNTU_BASE_BASEURL/$name" "$CACHE/$name" "$want"
  UBUNTU_BASE_TARBALL="$CACHE/$name"
  UBUNTU_BASE_NAME="${name%.tar.gz}"
}

fetch_components() {
  step "Downloading pinned components"
  # Cross-check the pinned Wine sum against upstream's published sha256sums.txt.
  local wsums="$CACHE/wine-${WINE_VERSION}-sha256sums.txt" upstream
  fetch "$WINE_SUMS_URL" "$wsums"
  upstream="$(awk -v n="$WINE_ASSET" '{f=$2; sub(/^\*/, "", f); if (f==n) print $1}' "$wsums")"
  [[ -n "$upstream" ]] || die "$WINE_ASSET is not listed in $WINE_SUMS_URL"
  [[ "$upstream" == "$WINE_SHA256" ]] || die "WINE_SHA256 ($WINE_SHA256) differs from upstream sha256sums.txt ($upstream) for $WINE_ASSET"
  fetch "$WINE_URL"  "$CACHE/$WINE_ASSET" "$WINE_SHA256"
  fetch "$DXVK_URL"  "$CACHE/dxvk-${DXVK_VERSION}.tar.gz" "$DXVK_SHA256"
  fetch "$VKD3D_URL" "$CACHE/vkd3d-proton-${VKD3D_VERSION}.tar.zst" "$VKD3D_SHA256"

  local key="$CACHE/fex-emu-ppa-${FEX_PPA_FINGERPRINT}.asc"
  fetch "$FEX_PPA_KEY_URL" "$key"
  grep -q 'BEGIN PGP PUBLIC KEY BLOCK' "$key" || { rm -f "$key"; die "FEX PPA key download did not return an armored key"; }

  if [[ $SKIP_FEX_ROOTFS -eq 0 ]]; then
    # FEX publishes XXH3-64 (not sha256); it is verified inside the chroot.
    FEX_ROOTFS_FILE="$CACHE/$(basename "$FEX_ROOTFS_URL")"
    fetch "$FEX_ROOTFS_URL" "$FEX_ROOTFS_FILE"
  fi
}

# ----------------------------------------------------------------------------
# 3. Rootfs + chroot
# ----------------------------------------------------------------------------
extract_base() {
  step "Extracting base rootfs"
  if [[ -e "$ROOTFS" ]]; then
    assert_no_mounts_below "$ROOTFS"
    log "removing previous staging rootfs $ROOTFS"
    rm -rf --one-file-system "$ROOTFS"
  fi
  mkdir -p "$ROOTFS"
  tar --numeric-owner -xpzf "$UBUNTU_BASE_TARBALL" -C "$ROOTFS"
  ok "extracted $(basename "$UBUNTU_BASE_TARBALL")"
}

prepare_chroot() {
  step "Preparing chroot"
  if [[ "$(uname -m)" == "x86_64" || "$(uname -m)" == "amd64" ]] && [[ "${QEMU_FLAGS:-}" != *F* ]]; then
    mkdir -p "$ROOTFS$(dirname "$QEMU_INTERP")"
    cp -f "$QEMU_INTERP" "$ROOTFS$QEMU_INTERP"
    QEMU_COPIED="$ROOTFS$QEMU_INTERP"
  fi

  do_mount "$ROOTFS/proc" -t proc proc
  do_mount "$ROOTFS/sys"  -t sysfs sysfs
  do_mount "$ROOTFS/dev"  --bind /dev
  if [[ -d /dev/pts ]]; then do_mount "$ROOTFS/dev/pts" --bind /dev/pts; fi
  do_mount "$ROOTFS/run"  -t tmpfs -o mode=0755 tmpfs
  do_mount "$ROOTFS$CACHE_MNT" --bind "$CACHE"
  mount -o remount,bind,ro "$ROOTFS$CACHE_MNT"

  # DNS for the build; replaced by a static file at the end (no resolved in PRoot).
  rm -f "$ROOTFS/etc/resolv.conf"
  if [[ -s /etc/resolv.conf ]] && ! grep -qE '^nameserver[[:space:]]+127\.0\.0\.53' /etc/resolv.conf; then
    cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
  elif [[ -s /run/systemd/resolve/resolv.conf ]]; then
    cp -L /run/systemd/resolve/resolv.conf "$ROOTFS/etc/resolv.conf"
  else
    printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$ROOTFS/etc/resolv.conf"
  fi

  # Never start services inside the chroot.
  printf '#!/bin/sh\nexit 101\n' > "$ROOTFS/usr/sbin/policy-rc.d"
  chmod 0755 "$ROOTFS/usr/sbin/policy-rc.d"

  # Keep docs/man/foreign translations out of every package we install.
  mkdir -p "$ROOTFS/etc/dpkg/dpkg.cfg.d"
  cat > "$ROOTFS/etc/dpkg/dpkg.cfg.d/01-hyperplay-slim" <<'EOF'
path-exclude=/usr/share/doc/*
path-include=/usr/share/doc/*/copyright
path-exclude=/usr/share/man/*
path-exclude=/usr/share/info/*
path-exclude=/usr/share/lintian/*
path-exclude=/usr/share/linda/*
path-exclude=/usr/share/locale/*
path-include=/usr/share/locale/locale.alias
path-include=/usr/share/locale/en/*
path-include=/usr/share/locale/en_US/*
path-include=/usr/share/locale/es/*
path-include=/usr/share/locale/es_ES/*
EOF
  cat > "$ROOTFS/etc/apt/apt.conf.d/99-hyperplay" <<'EOF'
APT::Install-Recommends "false";
APT::Install-Suggests "false";
Acquire::Retries "4";
Dpkg::Use-Pty "0";
EOF

  # Ubuntu ports sources (deb822). ca-certificates is not yet installed, and
  # ports.ubuntu.com is served over plain http with signed Release files.
  rm -f "$ROOTFS/etc/apt/sources.list"
  cat > "$ROOTFS/etc/apt/sources.list.d/ubuntu.sources" <<EOF
Types: deb
URIs: ${UBUNTU_PORTS_MIRROR}
Suites: ${UBUNTU_SUITE} ${UBUNTU_SUITE}-updates ${UBUNTU_SUITE}-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
  ok "chroot ready"
}

install_packages() {
  step "Installing aarch64 packages"
  if [[ "$FEX_ROOTFS_URL" == *.ero ]]; then ROOTFS_PACKAGES+=(erofs-utils); fi
  in_chroot '
    apt-get update
    apt-get -y -o Dpkg::Options::=--force-confnew dist-upgrade
    apt-get -y install "$@"
  ' "${ROOTFS_PACKAGES[@]}"
  ok "base packages installed"

  step "Adding FEX-Emu PPA (signed-by keyring, no apt-key)"
  install -D -m 0644 "$CACHE/fex-emu-ppa-${FEX_PPA_FINGERPRINT}.asc" "$ROOTFS/tmp/fex-emu-ppa.asc"
  in_chroot '
    want="$1"; suite="$2"; uri="$3"
    export GNUPGHOME="$(mktemp -d)"
    got="$(gpg --batch --with-colons --show-keys /tmp/fex-emu-ppa.asc | awk -F: "\$1==\"fpr\" {print \$10; exit}")"
    if [ "$got" != "$want" ]; then
      echo "FEX PPA key fingerprint mismatch: expected $want, got ${got:-none}" >&2
      exit 1
    fi
    install -d -m 0755 /etc/apt/keyrings
    gpg --batch --yes --dearmor -o /etc/apt/keyrings/fex-emu.gpg /tmp/fex-emu-ppa.asc
    chmod 0644 /etc/apt/keyrings/fex-emu.gpg
    rm -rf "$GNUPGHOME" /tmp/fex-emu-ppa.asc
    cat > /etc/apt/sources.list.d/fex-emu-fex.sources <<EOF2
Types: deb
URIs: $uri
Suites: $suite
Components: main
Signed-By: /etc/apt/keyrings/fex-emu.gpg
EOF2
    apt-get update
  ' "$FEX_PPA_FINGERPRINT" "$UBUNTU_SUITE" "$FEX_PPA_URI"

  step "Installing FEX-Emu"
  FEX_PKG="$(in_chroot '
    for p in "$@"; do
      c="$(apt-cache policy "$p" 2>/dev/null | awk "/Candidate:/ {print \$2}")"
      if [ -n "$c" ] && [ "$c" != "(none)" ]; then echo "$p"; exit 0; fi
    done
    exit 1
  ' $FEX_PKG_CANDIDATES)" || die "None of [$FEX_PKG_CANDIDATES] is available from $FEX_PPA_URI ($UBUNTU_SUITE)"
  log "selected $FEX_PKG"
  in_chroot 'apt-get -y install "$1"' "$FEX_PKG"
  [[ -x "$ROOTFS/usr/bin/FEXInterpreter" ]] || die "$FEX_PKG installed but /usr/bin/FEXInterpreter is missing"
  FEX_PKG_VERSION="$(in_chroot 'dpkg-query -W -f="\${Version}" "$1"' "$FEX_PKG")"
  ok "$FEX_PKG $FEX_PKG_VERSION"

  step "Generating locales (en_US, es_ES)"
  in_chroot '
    locale-gen en_US.UTF-8 es_ES.UTF-8
    update-locale LANG=en_US.UTF-8
  '
}

# ----------------------------------------------------------------------------
# 4. FEX x86 guest rootfs + config
# ----------------------------------------------------------------------------
install_fex_rootfs() {
  local dest="$HP_PREFIX/fex-rootfs"
  mkdir -p "$ROOTFS$dest"
  if [[ $SKIP_FEX_ROOTFS -eq 1 ]]; then
    warn "--skip-fex-rootfs: $dest left empty; provide an x86_64 rootfs there before running games"
    FEX_ROOTFS_DESC="external"
    return 0
  fi
  step "FEX x86 guest rootfs"
  local img; img="$(basename "$FEX_ROOTFS_FILE")"
  in_chroot '
    img="$1"; want="$2"; dest="$3"
    # xxhsum 0.8.x imprime "XXH3 (<archivo>) = <hash>": tomamos el último bloque de 16 hex.
    got="$(xxhsum -H3 "'"$CACHE_MNT"'/$img" | grep -oE "[0-9a-fA-F]{16}" | tail -n1)"
    got="${got#XXH3_}"
    norm() { printf "%s" "$1" | tr "A-F" "a-f" | sed "s/^0*//"; }
    if [ "$(norm "$got")" != "$(norm "$want")" ]; then
      echo "XXH3 mismatch for $img: expected $want, got $got" >&2
      exit 3
    fi
    echo "XXH3 verified: $img"
    rm -rf "$dest"
    case "$img" in
      *.sqsh) unsquashfs -q -f -d "$dest" "'"$CACHE_MNT"'/$img" ;;
      *.ero)  fsck.erofs --extract="$dest" "'"$CACHE_MNT"'/$img" ;;
      *)      echo "Unknown FEX rootfs image type: $img" >&2; exit 4 ;;
    esac
    rm -rf "$dest/usr/share/doc" "$dest/usr/share/man" "$dest/usr/share/info"
  ' "$img" "$FEX_ROOTFS_XXH3" "$dest" || {
    local rc=$?
    if [[ $rc -eq 3 ]]; then rm -f "$FEX_ROOTFS_FILE"; die "FEX rootfs hash mismatch; cached image deleted, re-run to download again"; fi
    die "FEX rootfs extraction failed (exit $rc)"
  }
  [[ -e "$ROOTFS$dest/usr/bin" || -L "$ROOTFS$dest/usr/bin" ]] || die "Extracted FEX rootfs at $dest looks empty"
  FEX_ROOTFS_DESC="$(basename "$FEX_ROOTFS_URL") xxh3:${FEX_ROOTFS_XXH3} ($FEX_ROOTFS_URL)"
  ok "FEX rootfs extracted to $dest"
}

create_user() {
  step "Creating user ${HP_USER} (uid ${HP_UID})"
  in_chroot '
    user="$1"; uid="$2"
    existing="$(getent passwd "$uid" | cut -d: -f1 || true)"
    if [ -n "$existing" ] && [ "$existing" != "$user" ]; then
      userdel -r "$existing" 2>/dev/null || userdel "$existing"
    fi
    grp="$(getent group "$uid" | cut -d: -f1 || true)"
    if [ -n "$grp" ] && [ "$grp" != "$user" ]; then groupdel "$grp"; fi
    if ! getent passwd "$user" >/dev/null; then
      getent group "$user" >/dev/null || groupadd -g "$uid" "$user"
      useradd -m -u "$uid" -g "$uid" -s /bin/bash -d "/home/$user" "$user"
    fi
    for g in audio video input render; do
      if getent group "$g" >/dev/null; then usermod -aG "$g" "$user"; fi
    done
    passwd -d "$user" >/dev/null
  ' "$HP_USER" "$HP_UID"
  ok "user ${HP_USER} ready"
}

write_fex_config() {
  step "Writing FEX configuration"
  local rootfs_path="$HP_PREFIX/fex-rootfs" cfg thunks=""
  cfg="$(cat <<EOF
{
  "Config": {
    "RootFS": "${rootfs_path}"
  }
}
EOF
)"
  if [[ "$HP_FEX_VULKAN_THUNKS" == "1" ]]; then
    local host_thunk=( "$ROOTFS"/usr/lib/*/fex-emu/HostThunks/libvulkan-host.so "$ROOTFS"/usr/lib/fex-emu/HostThunks/libvulkan-host.so )
    if [[ ${#host_thunk[@]} -gt 0 ]]; then
      thunks="$(printf '{\n  "ThunksDB": {\n    "Vulkan": 1\n  }\n}\n')"
      ok "FEX Vulkan host thunk found: ${host_thunk[0]#"$ROOTFS"}"
    else
      warn "FEX package ships no libvulkan-host.so; Vulkan thunks not enabled (guest x86 Vulkan from FEX rootfs will be used)"
    fi
  fi

  local home owner
  for home in /root "/home/$HP_USER"; do
    install -d -m 0755 "$ROOTFS$home/.fex-emu"
    printf '%s\n' "$cfg" > "$ROOTFS$home/.fex-emu/Config.json"
    if [[ -n "$thunks" ]]; then printf '%s\n' "$thunks" > "$ROOTFS$home/.fex-emu/ThunksDB.json"; fi
  done
  owner="${HP_UID}:${HP_UID}"
  chown -R "$owner" "$ROOTFS/home/$HP_USER/.fex-emu"
  install -d -m 0755 "$ROOTFS/etc/hyperplay"
  printf '%s\n' "$cfg" > "$ROOTFS/etc/hyperplay/fex_config.json"
  if [[ -n "$thunks" ]]; then printf '%s\n' "$thunks" > "$ROOTFS/etc/hyperplay/fex_thunksdb.json"; fi
  ok "FEX RootFS -> $rootfs_path"
}

# ----------------------------------------------------------------------------
# 5/6. Wine WOW64, DXVK, VKD3D-Proton
# ----------------------------------------------------------------------------
install_wine_dxvk_vkd3d() {
  step "Installing Wine ${WINE_VERSION} WOW64 -> /opt/wine"
  rm -rf "$ROOTFS/opt/wine"
  mkdir -p "$ROOTFS/opt/wine"
  tar --no-same-owner -xJf "$CACHE/$WINE_ASSET" -C "$ROOTFS/opt/wine" --strip-components=1
  if [[ ! -x "$ROOTFS/opt/wine/bin/wine" && ! -x "$ROOTFS/opt/wine/bin/wine64" ]]; then
    die "Wine archive did not provide /opt/wine/bin/wine"
  fi
  # Older scripts call wine64; WOW64 builds only ship "wine".
  if [[ ! -e "$ROOTFS/opt/wine/bin/wine64" && -x "$ROOTFS/opt/wine/bin/wine" ]]; then
    ln -s wine "$ROOTFS/opt/wine/bin/wine64"
  fi
  [[ -d "$ROOTFS/opt/wine/lib/wine/x86_64-windows" ]] || die "Wine archive lacks lib/wine/x86_64-windows"
  [[ -d "$ROOTFS/opt/wine/lib/wine/i386-windows" ]] || die "Wine archive lacks lib/wine/i386-windows (not a WOW64 build?)"
  ok "Wine installed"

  step "Installing DXVK ${DXVK_VERSION}"
  local tmp; tmp="$(mktemp -d "$WORK_DIR/tmp.XXXXXX")"
  tar -xzf "$CACHE/dxvk-${DXVK_VERSION}.tar.gz" -C "$tmp"
  local dx="$tmp/dxvk-${DXVK_VERSION}"
  [[ -d "$dx/x64" && -d "$dx/x32" ]] || die "DXVK archive layout unexpected (no x64/x32)"
  rm -rf "$ROOTFS$HP_PREFIX/dxvk"
  mkdir -p "$ROOTFS$HP_PREFIX/dxvk"
  cp -a "$dx/x64" "$dx/x32" "$ROOTFS$HP_PREFIX/dxvk/"
  if [[ -f "$dx/x32/d3d8.dll" ]]; then
    ok "DXVK ships d3d8.dll (built-in d3d8to9): no separate d3d8to9 needed"
  else
    warn "DXVK ${DXVK_VERSION} has no d3d8.dll; DirectX 8 titles need d3d8to9 separately"
  fi
  rm -rf "$tmp"

  step "Installing VKD3D-Proton ${VKD3D_VERSION}"
  tmp="$(mktemp -d "$WORK_DIR/tmp.XXXXXX")"
  zstd -dc "$CACHE/vkd3d-proton-${VKD3D_VERSION}.tar.zst" | tar -xf - -C "$tmp"
  local vk="$tmp/vkd3d-proton-${VKD3D_VERSION}"
  [[ -d "$vk/x64" && -d "$vk/x86" ]] || die "VKD3D-Proton archive layout unexpected (no x64/x86)"
  rm -rf "$ROOTFS$HP_PREFIX/vkd3d"
  mkdir -p "$ROOTFS$HP_PREFIX/vkd3d"
  cp -a "$vk/x64" "$vk/x86" "$ROOTFS$HP_PREFIX/vkd3d/"
  rm -rf "$tmp"
  chown -R 0:0 "$ROOTFS$HP_PREFIX/dxvk" "$ROOTFS$HP_PREFIX/vkd3d"
  ok "DXVK -> $HP_PREFIX/dxvk/{x64,x32}, VKD3D-Proton -> $HP_PREFIX/vkd3d/{x64,x86}"
}

# ----------------------------------------------------------------------------
# 7. HyperPlay layout, profile, marker
# ----------------------------------------------------------------------------
write_layout() {
  step "HyperPlay layout (Xvfb/SHM bridge, profile, marker)"
  install -d -m 1777 "$ROOTFS/tmp" "$ROOTFS/tmp/.X11-unix"
  install -d -m 0777 "$ROOTFS/tmp/hp_fb"
  install -d -m 0755 "$ROOTFS$HP_PREFIX/bin"
  cat > "$ROOTFS$HP_PREFIX/bin/README" <<'EOF'
HyperPlay copies libhyperplay_display_bridge.so (from the APK nativeLibraryDir)
into this directory at runtime. It runs next to `Xvfb :0 -fbdir /tmp/hp_fb`,
shares the Xvfb XWD framebuffer fd over /tmp/hyperplay_display.sock and
injects gamepad input events into the guest.
EOF

  cat > "$ROOTFS/etc/profile.d/hyperplay.sh" <<EOF
# HyperPlay guest environment (generated by build_multibit_rootfs.sh)
case ":\${PATH}:" in
  *:/opt/wine/bin:*) ;;
  *) export PATH="/opt/wine/bin:${HP_PREFIX}/bin:\${PATH}" ;;
esac
export WINEDEBUG=-all
export MESA_VK_WSI_PRESENT_MODE=mailbox
export HYPERPLAY_DXVK_DIR=${HP_PREFIX}/dxvk
export HYPERPLAY_VKD3D_DIR=${HP_PREFIX}/vkd3d
export HYPERPLAY_FEX_ROOTFS=${HP_PREFIX}/fex-rootfs
EOF
  chmod 0644 "$ROOTFS/etc/profile.d/hyperplay.sh"

  local mesa_ver xvfb_ver
  mesa_ver="$(in_chroot 'dpkg-query -W -f="\${Version}" mesa-vulkan-drivers')"
  xvfb_ver="$(in_chroot 'dpkg-query -W -f="\${Version}" xvfb')"
  cat > "$ROOTFS$HP_PREFIX/.full" <<EOF
HYPERPLAY_ROOTFS=full
HYPERPLAY_ROOTFS_ARCH=aarch64
BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
UBUNTU_BASE=${UBUNTU_BASE_NAME}
FEX_PACKAGE=${FEX_PKG}
FEX_VERSION=${FEX_PKG_VERSION}
FEX_ROOTFS=${FEX_ROOTFS_DESC}
FEX_ROOTFS_PRESENT=$([[ $SKIP_FEX_ROOTFS -eq 1 ]] && echo 0 || echo 1)
WINE=${WINE_ASSET%.tar.xz}
WINE_MODE=wow64
DXVK=${DXVK_VERSION}
VKD3D_PROTON=${VKD3D_VERSION}
MESA_VULKAN_DRIVERS=${mesa_ver}
XVFB=${xvfb_ver}
EOF
  chmod 0644 "$ROOTFS$HP_PREFIX/.full"
  ok "marker $HP_PREFIX/.full written"
}

# ----------------------------------------------------------------------------
# 8. Slim down, unmount, pack
# ----------------------------------------------------------------------------
slim_rootfs() {
  step "Cleaning caches, docs, man pages, extra locales"
  in_chroot '
    apt-get -y autoremove --purge
    apt-get clean
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*.bin /var/cache/debconf/*-old /var/lib/dpkg/*-old
    find /usr/share/doc -mindepth 1 ! -name copyright ! -type d -delete 2>/dev/null || true
    find /usr/share/doc -mindepth 1 -type d -empty -delete 2>/dev/null || true
    rm -rf /usr/share/man/* /usr/share/info/* /usr/share/lintian/* /usr/share/linda/*
    find /usr/share/locale -mindepth 1 -maxdepth 1 \
      ! -name locale.alias ! -name en ! -name en_US ! -name es ! -name es_ES \
      -exec rm -rf {} +
    find /var/log -type f -exec truncate -s 0 {} +
    rm -rf /tmp/* /var/tmp/* /root/.cache /root/.gnupg
  '
  install -d -m 1777 "$ROOTFS/tmp/.X11-unix"
  install -d -m 0777 "$ROOTFS/tmp/hp_fb"
  rm -f "$ROOTFS/usr/sbin/policy-rc.d"
  # Static DNS: PRoot has no systemd-resolved; the app may overwrite this.
  rm -f "$ROOTFS/etc/resolv.conf"
  printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$ROOTFS/etc/resolv.conf"
  ok "rootfs slimmed"
}

pack_rootfs() {
  step "Unmounting and packing"
  unmount_all
  assert_no_mounts_below "$ROOTFS"
  if [[ -n "$QEMU_COPIED" && -f "$QEMU_COPIED" ]]; then rm -f "$QEMU_COPIED"; QEMU_COPIED=""; fi
  rmdir "$ROOTFS$CACHE_MNT" 2>/dev/null || true

  local out="$OUT_DIR/$OUTPUT_NAME"
  rm -f "$out.part"
  tar --numeric-owner --xattrs -C "$ROOTFS" -cf - . \
    | zstd -19 -T0 --long=27 -q -f -o "$out.part"
  mv -f "$out.part" "$out"
  ( cd "$OUT_DIR" && sha256sum "$OUTPUT_NAME" > "$OUTPUT_NAME.sha256" )

  local size sum
  size="$(du -h "$out" | awk '{print $1}')"
  sum="$(awk '{print $1}' "$OUT_DIR/$OUTPUT_NAME.sha256")"
  ok "output:  $out"
  ok "size:    $size ($(stat -c %s "$out") bytes)"
  ok "sha256:  $sum  (also in $OUTPUT_NAME.sha256)"
  log "Decompress with a zstd window of >= 2^27 (zstd --long=27 -d, or set the decoder's windowLogMax to 27)."

  if [[ $KEEP_WORK -eq 0 ]]; then
    assert_no_mounts_below "$ROOTFS"
    rm -rf --one-file-system "$ROOTFS"
    log "staging rootfs removed (downloads kept in $CACHE)"
  else
    log "staging rootfs kept at $ROOTFS"
  fi
}

smoke_test() {
  step "Smoke tests"
  in_chroot '
    for p in xvfb x11-xserver-utils mesa-vulkan-drivers vulkan-tools libvulkan1 locales procps "$1"; do
      dpkg-query -W -f="\${Status}\n" "$p" | grep -q "install ok installed" || { echo "package $p not installed" >&2; exit 1; }
    done
    command -v Xvfb >/dev/null
    command -v FEXInterpreter >/dev/null
  ' "$FEX_PKG"
  if [[ "$(uname -m)" == "aarch64" || "$(uname -m)" == "arm64" ]]; then
    if in_chroot 'FEXInterpreter --version' >/dev/null 2>&1; then
      ok "FEXInterpreter runs natively"
    else
      warn "FEXInterpreter --version failed on this host (CPU feature level?); the rootfs is still packed"
    fi
  fi
  ok "smoke tests passed"
}

main() {
  host_checks
  fetch_ubuntu_base
  fetch_components
  extract_base
  prepare_chroot
  install_packages
  install_fex_rootfs
  create_user
  write_fex_config
  install_wine_dxvk_vkd3d
  write_layout
  smoke_test
  slim_rootfs
  pack_rootfs
  log "Done."
}

main "$@"
