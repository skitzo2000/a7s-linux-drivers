# U-Boot for the Cubie A7S

Mainline U-Boot for the **Radxa Cubie A7S**, built on Yixun Lan's A733 support and its
Cubie A7A board file, with the adjustments the A7S needs. Two patches.

With these, U-Boot enumerates the board's USB-A socket and boots an OS straight off a USB
stick:

```
Bus usb@4200000: 3 USB Device(s) found
       scanning usb for storage devices... 1 Storage Device(s) found
Device 0: Vendor: SanDisk'  Prod: Cruzer Fit  Capacity: 57.7 GB
Scanning usb 0:1... Found U-Boot script /boot.scr
Starting kernel ...
```

## The changes

**Board file — `sun60i-a733-radxa-a7s.dts` + `radxa-cubie-a7s_defconfig`.** The A7S wires
its USB host VBUS to **PM5** (`USB_HOST_EN`), the enable of the SGM2576 load switch that
supplies `VCC5V0_USB20`. That rail powers the USB-A socket, which sits behind the onboard
CH334F hub on `usb1` (`ehci1`/`ohci1`), alongside the AIC8800 WiFi/BT module. So
`usb1_vbus-supply` is a fixed regulator on PM5. `usb0` stays on the always-on supply — that
port is powered by the cable plugged into it.

**Pinctrl — bank stride from the controller description.** On the A733 the main pin
controller spaces its banks `0x80` apart and the R controller uses `0x30`. The stride is
now taken from the controller's description at bind time, so `PM*` pins address correctly.
This one is generic sunxi code: any A733 board driving a PM pin wants it.

| Patch | |
|---|---|
| [`0001-sunxi-pinctrl-take-the-bank-stride-from-the-controll.patch`](patches/0001-sunxi-pinctrl-take-the-bank-stride-from-the-controll.patch) | R-controller bank stride |
| [`0002-sunxi-a733-add-support-for-the-Radxa-Cubie-A7S.patch`](patches/0002-sunxi-a733-add-support-for-the-Radxa-Cubie-A7S.patch) | A7S board file and defconfig |

## Building

Base tree: [`dlan17/u-boot`](https://github.com/dlan17/u-boot) branch `allwinner/A733/next`,
plus a BL31 from [`dlan17/trusted-firmware-a`](https://github.com/dlan17/trusted-firmware-a)
branch `A733` (`PLAT=sun60i_a733`).

```sh
git clone -b allwinner/A733/next https://github.com/dlan17/u-boot
cd u-boot
git am /path/to/u-boot/patches/000*.patch

export CROSS_COMPILE=aarch64-linux-gnu-
export BL31=/path/to/trusted-firmware-a/build/sun60i_a733/debug/bl31.bin
make radxa-cubie-a7s_defconfig
make -j"$(nproc)"
```

Gives `u-boot-sunxi-with-spl.bin` (SPL) and `u-boot-sunxi-with-spl.fit.fit` (BL31 +
U-Boot proper).

Build notes: keep one Python across the whole build, and delete
`scripts/dtc/pylibfdt/_libfdt.so` if you switch versions. If your host lacks libcrypto or
gnutls headers, `./scripts/config --disable TOOLS_LIBCRYPTO --disable TOOLS_FIT_SIGNATURE
--disable TOOLS_MKEFICAPSULE` (a fresh `make <board>_defconfig` re-enables them).

## Running it over FEL

The A733 BROM loads its first stage from SD or eMMC, and offers USB FEL on the Type-C port.
FEL lets a host supply the whole chain into RAM, which is how you boot a unit whose OS
lives on a USB stick. It runs from RAM, so repeat it at each power-on.

You need `sunxi-fel` from [`dlan17/sunxi-tools`](https://github.com/dlan17/sunxi-tools)
branch `A733`, with its A733 `soc_info` sized for a 240 KB first stage: `sram_size` 264 KB,
`scratch_addr = 0x85400`, `thunk_addr = 0x85000`, swap `buf2 = 0x83400`. You also need
vendor `boot0` for the LPDDR5 init — it carries the `eGON.BT0` magic at offset 4 and lives
in the first 16 MiB of a Radxa BSP image. Serial is 115200 8N1.

With the stick in the USB-A socket:

```sh
sunxi-fel ver                                                    # board is in FEL
sunxi-fel spl boot0.bin                                          # DRAM init, returns to FEL
sunxi-fel -p write 0x4D000000 u-boot-sunxi-with-spl.fit.fit      # BL31 + U-Boot
sunxi-fel -v spl u-boot-sunxi-with-spl.bin                       # hand off
```

Each `spl` step ends with a `usb_bulk_send()` message as the board takes over the port and
re-enumerates — that is the handoff, and it comes back with a new devnum, so re-read
`bus:devnum` between steps or pin by sysfs port path.

U-Boot's distro boot then finds `/boot.scr` on the stick and runs it. Any image whose boot
script is written in `${devtype} ${devnum}` terms works — a plain `dd` of an Armbian-style
A7S image is enough. Have `usb-storage.ko` and `uas.ko` in the initramfs so the kernel can
mount root from the stick.

## Scope

Boot media is the USB-A socket, on `ehci1`. The Type-C port's DWC3/xHCI controller is not
built in: bringing it up needs more than its clocks, reset and power domain, and it is not
needed for boot. Notes and register readings from that experiment are on the `xhci` branch.

Verified 2026-08-19 on hardware: FEL → boot0 → BL31 → U-Boot → USB stick → Linux 6.18.19.
