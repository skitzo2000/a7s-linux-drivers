#!/usr/bin/env bash
# Install freshly built A7S kernel packages on a board and say what changed.
#   sudo ./install-on-board.sh linux-image-…deb linux-dtb-…deb linux-headers-…deb
# Same kernel release string as before (6.18.19-edge-sun60iw2), so this is a
# reinstall from apt's point of view: --reinstall --allow-downgrades. Reboot after.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
[ $# -ge 1 ] || { echo "usage: $0 <deb…>" >&2; exit 1; }
before=$(sha256sum /boot/Image | cut -c1-12)
DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall --allow-downgrades "$@"
after=$(sha256sum /boot/Image | cut -c1-12)
echo "/boot/Image $before -> $after"
kv=$(uname -r)
echo "in-tree DVB-USB: $(ls /lib/modules/$kv/kernel/drivers/media/usb/dvb-usb-v2/ 2>/dev/null | tr '\n' ' ')"
for m in dvb_usb_v2.ko dvb-usb-rtl28xxu.ko; do
  [ -f /lib/modules/$kv/extra/$m ] && { rm -f /lib/modules/$kv/extra/$m; echo "removed extra/$m (now in-tree)"; }
done
depmod -a "$kv"
echo "reboot to run the new kernel; then: uname -v, iw dev wlan0 set type monitor (with wlan0 unmanaged), ls /dev/swradio0"
