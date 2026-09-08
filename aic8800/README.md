# AIC8800D80 — WiFi and Bluetooth fixes

WiFi and Bluetooth on the A7S are an AIC8800D80 hanging off USB. The driver ships in-tree in the
vendor kernel, so on a stock image it comes up and connects and you don't have to build anything.

Two bugs in it are worth your time anyway. One hangs the box. The other quietly transmits about
2 dB above the calibrated ceiling. Both are in every AIC drop I've looked at, including the newest
(`RELEASE_DATE 2026_0123_5f7be68d`), so upgrading the driver doesn't get you out of either.

`driver/` is that newest drop, USB side only, staged here so the patch has something to apply to
and so you can rebuild without going hunting. It's AICSemi's code, unmodified.

## 1. The Bluetooth hang

**Symptom:** hold a key on a Bluetooth keyboard, and the box locks up and doesn't come back.

`aic_btusb` doesn't use the kernel Bluetooth stack. It keeps a private 500-entry ring, filled from
URB completion and drained by a chardev `read()`. Two defects turn a sustained HCI RX burst into
an unrecoverable hang:

**It leaks every dropped skb.** `hci_send_to_stack()` hands over a fresh `pskb_copy()` and never
looks at it again; `hci_recv_frame()` frees only the original. When the ring fills, `aic_enqueue()`
logs a warning and returns without freeing the copy. One skb leaked per packet, for as long as the
burst lasts.

The vendor's only backpressure drops BLE *advertising reports* once the queue gets near full. HID
input arrives as ACL data, not advertising reports — so a held key walks straight past the guard.

**And the reader spins.** When `aic_dequeue_try()` fails on a non-empty queue (its partial-read
`pskb_copy(GFP_ATOMIC)` couldn't allocate), the `wait_event` condition is still true, so the read
loop burns 100% CPU in the syscall forever. That's precisely when the box is already out of
memory.

The fix in [`patches/`](patches/) frees the dropped skb, rate-limits the warning (at flood rate the
unthrottled printk is its own denial of service), and bails out of the read loop with `-ENOMEM`
instead of spinning. The free is deferred past `spin_unlock_irqrestore()` because `aic_enqueue()`
runs from URB completion with interrupts off.

The patch is written against the in-tree path, so from your kernel source root:

```sh
patch -p1 < aic8800/patches/0001-aic_btusb-free-dropped-skbs-and-stop-the-reader-spinning.patch
```

The copy staged here in `driver/` is the same file three directories shallower, so apply it from
`aic8800/driver/` with `-p5` instead:

```sh
cd aic8800/driver
patch -p5 < ../patches/0001-aic_btusb-free-dropped-skbs-and-stop-the-reader-spinning.patch
```

**Line endings.** The AIC drop on BSP branches `linux-6.18.x` / `linux-6.18.y` is LF, and so is the
copy in `driver/` — the patch targets those and applies clean. The copy on `linux-6.18.z` is CRLF
and every hunk fails on it. Convert that file first (`sed -i 's/\r$//'`) or take the driver from an
LF branch.

## 2. WiFi loads the wrong calibration table

**Symptom, if you turn the debug level up enough to see it:**

```
rwnx_load_firmware: aic_userconfig_8800d80.txt file failed to open
```

The driver builds the path `/lib/firmware/aic_userconfig_8800d80.txt`, missing the `aic8800D80/`
subdirectory the file actually lives in. It falls back to the built-in txpower table, which is
*higher* than the board's calibrated one — 20 vs 18 on `lvl_11b_11ag_1m_2g4`, 16 vs 14, 15 vs 13,
14 vs 12. Overdriving the PA costs you EVM at high MCS, which means retries, less throughput,
flakier links, and battery.

The subdir append is skipped when `ANDROID_PLATFORM` is defined, and it's defined because
`CONFIG_PLATFORM_ALLWINNER` is a *kernel* config symbol that the deck's kernel sets. kbuild pulls
in `auto.conf` before the driver Makefile's `CONFIG_PLATFORM_ALLWINNER ?= n` runs, so the `?=` is a
no-op and the Allwinner branch fires. Reading the Makefile alone will not show you this, and it
hits out-of-tree builds too.

The trap is the name: `PLATFORM_ALLWINNER` means *Allwinner Android BSP*, not *Allwinner silicon*.
The A7S is Allwinner hardware running Debian, so the flag is just wrong for us.

**Zero-risk fix**, and the one I'm running:

```sh
ln -sf aic8800D80/aic_userconfig_8800d80.txt /lib/firmware/aic_userconfig_8800d80.txt
ln -sf aic8800D80/aic_powerlimit_8800d80.txt /lib/firmware/aic_powerlimit_8800d80.txt
```

Survives reboots, needs no module swap. Re-apply after a reimage.

**Proper fix**, if you'd rather rebuild — override the symbol on the command line, where it
outranks `auto.conf`:

```sh
make -C driver/usb/aic_load_fw  KDIR=/lib/modules/$(uname -r)/build \
     CONFIG_PLATFORM_ALLWINNER=n CONFIG_PLATFORM_UBUNTU=y
make -C driver/usb/aic8800_fdrv KDIR=/lib/modules/$(uname -r)/build \
     KBUILD_EXTRA_SYMBOLS=$PWD/driver/usb/aic_load_fw/Module.symvers \
     CONFIG_PLATFORM_ALLWINNER=n CONFIG_PLATFORM_UBUNTU=y
```

Check it took: `grep -c DANDROID_PLATFORM driver/usb/aic8800_fdrv/.aicwf_compat_8800d80.o.cmd`
must be 0.

**Replace both modules as a pair.** The newer `aic8800_fdrv` needs `get_flash_bin_size` and
`get_flash_bin_crc`, and only the newer `aic_load_fw` exports them. Building `fdrv` alone dies at
MODPOST.

Dropping the define also flips `usb_driver.supports_autosuspend` from 1 to 0. On an always-on deck
whose USB is shared with Bluetooth, that's a change in the right direction.

The full writeup, including everything I checked before calling it — is in
[`USERCONFIG-PATH.md`](USERCONFIG-PATH.md).

## 3. Monitor mode is refused (`set type monitor` → -EIO)

The phy advertises `monitor` and every 5 GHz channel, and NetworkManager leaves the
interface alone once it is unmanaged — yet `iw dev wlan0 set type monitor` fails with
`Operation not permitted`/`-EIO` and dmesg says `Monitor+Data interface support
(MON_DATA) disabled`. Adding a second interface (`iw phy phy0 interface add mon0 type
monitor`) fails too (`-22`, the interface combinations exclude it), so the only way to a
monitor interface is to switch `wlan0` itself.

The guard in `rwnx_cfg80211_change_iface()` (rwnx_main.c) walks the vif list looking
for *another* data interface but tests `vif` — the interface being changed — instead of
`vif_el`. The driver's own P2P device entry is always in that list, so the loop refuses
every time. The fix tests `vif_el`, and skips vifs that are monitor, the P2P device
(no data path) or not up. With it:

```sh
nmcli dev set wlan0 managed no          # NM re-manages a NEW netdev after a driver reload: do this after the modules are in
ip link set wlan0 down && iw dev wlan0 set type monitor && ip link set wlan0 up
iw dev wlan0 set freq 5240              # radiotap frames on 2.4 and 5 GHz, channels 1–165 (a7s-3, 2026-09-08)
```

`ip link set wlan0 down && iw dev wlan0 set type managed && ip link set wlan0 up &&
nmcli dev set wlan0 managed yes` hands the radio back. Still true: monitor and data
cannot coexist (the firmware has no MON_DATA), so a board in monitor mode has no Wi-Fi
uplink — use the backbone.

Build note: `PWD ?= $(shell pwd)` in the fdrv Makefile inherits an exported `PWD`, so
run `make` from inside `driver/usb/aic8800_fdrv` (or pass `PWD=$(pwd)`), and remember
`sudo` resets `HOME`.
