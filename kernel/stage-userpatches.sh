#!/usr/bin/env bash
# Stage this repo's kernel changes into an Armbian build checkout as userpatches,
# so `compile.sh kernel` produces the A7S edge kernel with everything here in it:
#
#   patches/0001 + 0100–0111      AXP8191 CPU-rail fix, the DP-over-USB-C series
#   aic8800/patches/0002          monitor mode on wlan0 (change_iface guard)
#   kernel config                 + CONFIG_DVB_USB_V2=m, CONFIG_DVB_USB_RTL28XXU=m
#                                 (an RTL-SDR becomes /dev/swradio0; see rtl-sdr/)
#
#   ./kernel/stage-userpatches.sh <armbian checkout>
#   cd <armbian checkout>
#   DOCKER_EXTRA_ARGS="--network=host" ./compile.sh kernel BOARD=radxa-cubie-a7s BRANCH=edge KERNEL_CONFIGURE=no PREFER_DOCKER=yes
#
# The checkout is NickAlilovic/build, branch Radxa-mainline-WIP-a7s (the boards ran
# commit 399778a). Output: output/debs/linux-{image,headers,dtb}-edge-sun60iw2_*.deb.
# Not staged: aic8800/patches/0001 (aic_btusb) — written against the LF BSP branches,
# the linux-6.18.z copy Armbian fetches is CRLF and every hunk fails (aic8800/README.md).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
armbian="${1:?usage: $0 <armbian build checkout>}"
[ -f "$armbian/compile.sh" ] || { echo "$armbian is not an Armbian build checkout" >&2; exit 1; }
dst="$armbian/userpatches/kernel/archive/sun60iw2-edge"
mkdir -p "$dst"
cp "$repo"/patches/0*.patch "$dst"/
cp "$repo"/aic8800/patches/0002-aic8800-monitor-mode-change-iface-guard.patch "$dst"/0201-aic8800-monitor-mode-change-iface-guard.patch
cfg="$armbian/userpatches/linux-sun60iw2-edge.config"
cp "$armbian/config/kernel/linux-sun60iw2-edge.config" "$cfg"
for opt in CONFIG_DVB_USB_V2 CONFIG_DVB_USB_RTL28XXU; do
  sed -i -e "/^# $opt is not set$/d" -e "/^$opt=/d" "$cfg"
  echo "$opt=m" >> "$cfg"
done
echo "staged $(ls "$dst" | wc -l) patches in $dst"
echo "config: $cfg ($(grep -cE '^CONFIG_DVB_USB(_V2|_RTL28XXU)=m' "$cfg") DVB-USB options)"
