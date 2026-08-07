#!/usr/bin/env bash
# Build and install the two out-of-tree modules the A7S needs.
#
#   sudo ./build-modules.sh
#
# Runs ON THE BOARD. Builds:
#
#   vipcore.ko          NPU / VIP9000 -> /dev/vipcore   (npu-vipcore/vipcore)
#   dwmac-sun60iw2.ko   GMAC-210 ethernet glue          (gmac)
#
# aic8800 is NOT built here — WiFi and BT ship in-tree in the vendor kernel,
# and building them out-of-tree would shadow the working ones. See aic8800/.
#
# Both are plain `make -C $KDIR M=$PWD modules` builds. Install linux-headers
# and build-essential first; this script will try, but apt is not always there.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
KV="$(uname -r)"
KDIR="/lib/modules/$KV/build"
DEST="/lib/modules/$KV/extra"

say()  { printf '\033[1m[modules]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[modules] WARN:\033[0m %s\n' "$*"; }

[ "$(id -u)" -eq 0 ] || { warn "must run as root"; exit 1; }

if [ ! -d "$KDIR" ]; then
  say "installing kernel headers for $KV"
  DEBIAN_FRONTEND=noninteractive apt-get install -y "linux-headers-$KV" >/dev/null 2>&1 ||
    { warn "no linux-headers-$KV — cannot build modules"; exit 1; }
fi
[ -d "$KDIR" ] || { warn "$KDIR still missing"; exit 1; }

command -v make >/dev/null 2>&1 || {
  say "installing build-essential"
  DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential >/dev/null 2>&1
}

mkdir -p "$DEST"
built=0; failed=""

build_one() {  # build_one <name> <source dir> <produced .ko>
  local name="$1" src="$2" ko="$3"
  if [ ! -d "$src" ]; then
    warn "$name: source not found at $src"; failed="$failed $name"; return
  fi
  if [ -f "$DEST/$ko" ]; then
    say "$name already installed — skipping (rm $DEST/$ko to force)"
    built=$((built + 1)); return
  fi
  say "building $name"
  # Build out of a temp copy so the repo stays clean and a re-run starts fresh.
  local work; work="$(mktemp -d)"
  cp -a "$src"/. "$work"/
  if make -C "$KDIR" M="$work" modules >"$work/build.log" 2>&1; then
    if [ -f "$work/$ko" ]; then
      install -m 0644 "$work/$ko" "$DEST/$ko"
      say "  installed $DEST/$ko"
      built=$((built + 1))
    else
      warn "  build reported success but $ko was not produced"; failed="$failed $name"
    fi
  else
    warn "  build FAILED — tail of the log:"
    tail -12 "$work/build.log" | sed 's/^/    /'
    failed="$failed $name"
  fi
  rm -rf "$work"
}

build_one vipcore          "$REPO/npu-vipcore/vipcore" vipcore.ko
build_one dwmac-sun60iw2   "$REPO/gmac"                dwmac-sun60iw2.ko

say "running depmod"
depmod -a "$KV" || warn "depmod failed"

echo
say "summary: $built built/present${failed:+, failed:$failed}"

# vipcore needs etnaviv blocked or it never gets the node — see npu-vipcore/README.md
if [ -e /dev/vipcore ]; then
  say "  /dev/vipcore present"
else
  say "  /dev/vipcore not present yet (loads at next boot)"
  if lsmod 2>/dev/null | grep -q '^etnaviv'; then
    warn "  etnaviv is loaded and will claim the NPU node — see npu-vipcore/README.md"
  fi
fi

exit 0
