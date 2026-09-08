# a7s-linux-drivers

Out-of-tree kernel drivers, bootloader patches, and device-tree overlays for the **Radxa Cubie A7S** —
Allwinner **A733**, die `sun60iw2`, on **Linux 6.18**.

Mainline doesn't know this SoC yet. The 6.18 edge images boot, but a few things the board
actually has are missing on the software side: the ethernet MAC has a DT node and no driver, the
NPU node gets grabbed by the wrong driver, cpufreq transitions take the box down under load, and
DisplayPort — the board's only video output — never trains a link. This repo is what I wrote and
ported to close those gaps while building a cyberdeck around the board.

Everything here has been on real hardware. Where something works but isn't finished, the README
for that piece says so.

## What's in here

| Directory | What it is | State |
|---|---|---|
| [`gmac/`](gmac/) | `dwmac-sun60iw2` — stmmac glue for the GMAC-210, ported to 6.18 | eth0 at 1 Gbps, DHCP, clean counters |
| [`npu-vipcore/`](npu-vipcore/) | VIP9000 NPU driver ported to 6.18 | `/dev/vipcore` live; userspace runtime still missing |
| [`aic8800/`](aic8800/) | AIC8800D80 WiFi/BT tree + two fixes | both fixes verified on-board |
| [`rtl-sdr/`](rtl-sdr/) | DVB-USB v2 + RTL28xxU from v6.18.19, the two modules the kernel config leaves out — an RTL-SDR becomes `/dev/swradio0` | verified on-board: IQ at 100 / 434 / 915 MHz |
| [`dp/`](dp/) | DisplayPort-over-USB-C — replacement sources for the three BSP drivers on the DP path | 2560×1440, automatic on boot, survives reboot |
| [`patches/`](patches/) | AXP8191 CPU-rail fix, plus the DP series `0100`–`0110` | both verified on-board; need a full Image rebuild |
| [`overlays/`](overlays/) | Device-tree overlays for the cyberdeck shield | in daily use |
| [`u-boot/`](u-boot/) | Mainline U-Boot for the A7S — board file plus a sunxi pinctrl addition | boots an OS off a USB stick over FEL |

## The kernel

Everything was built and run against **6.18.19-edge-sun60iw2** — the Armbian unofficial edge
build for the Cubie A7S (`Radxa-mainline-WIP-a7s`). The two modules build on the board itself;
install `linux-headers-$(uname -r)` and `build-essential` and you have what you need.

Other 6.18.x kernels should be fine. Anything older will fight you — a good chunk of the porting
work here is 6.18 API churn (`nth_page()` gone, `.remove` returning void, `EXTRA_CFLAGS` retired),
and none of it is version-gated backward.

Everything under `patches/` is one drop-in set: all twelve apply at `-p1` from the kernel source
root and build. The baseline they were checked against, and how to tell a rebase went wrong, are in
[`patches/README.md`](patches/README.md).

## Building

`build-modules.sh` builds and installs both modules, runs depmod, and tells you what landed.
Run it as root, on the board:

```sh
sudo ./build-modules.sh
```

It builds out of a temp copy so the repo stays clean, and it skips anything already installed in
`/lib/modules/$(uname -r)/extra/`. Delete the `.ko` there to force a rebuild.

Each directory also has a plain `make` path if you'd rather do one at a time. See its README.

### The whole kernel

To get all of this *into* the kernel packages instead of loading it beside them — the patch
set, the aic8800 monitor-mode fix, the RTL-SDR config — stage the repo into an Armbian build
checkout (NickAlilovic/build, branch `Radxa-mainline-WIP-a7s`) and build:

```sh
./kernel/stage-userpatches.sh ~/path/to/armbian-build
cd ~/path/to/armbian-build
DOCKER_EXTRA_ARGS="--network=host" ./compile.sh kernel BOARD=radxa-cubie-a7s BRANCH=edge KERNEL_CONFIGURE=no PREFER_DOCKER=yes
```

`output/debs/linux-{image,headers,dtb}-edge-sun60iw2_*.deb` come out; the `P<hash>` field in
their names hashes the patch set, so a staging change shows up in the file name. Install on a
board with `apt install ./linux-image-…deb ./linux-dtb-…deb ./linux-headers-…deb` and reboot.
The out-of-tree modules in `extra/` (vipcore, GMAC glue) still need `build-modules.sh`; the
DVB-USB pair is then in-tree and its copy in `extra/` can go. `kernel/install-on-board.sh`
does the install (a reinstall from apt's view — same release string) and that cleanup.

Built packages are published as releases here: `kernel-6.18.19-Pacdb` (2026-09-08) is what
the cluster's three boards run — 16 minutes in the Armbian container on a 16-core host, all
27 patches applied clean.

## DisplayPort

The A7S has **no HDMI**. Every pixel leaves this board over USB-C DisplayPort alt mode, so this is
the only video path there is.

It works: 2560×1440 on a cold boot with the cable already in, no debug knobs, no manual
`echo detect`, and it survives a reboot. Confirmed from the controller registers, not just the
connector state — `LANE_COUNT_SET = 0x2`, `TRAINING_PATTERN_SET = 0x0` (trained),
`LINK_BW_SET = 0x14` (5.4 Gbps), `MAIN_HRES/VRES = 2560×1440`, with DPCD `0x101 = 0x82` agreeing
from the sink side.

Five things this turned up, any of which will bite anyone else driving this IP:

- **The DP link policy sat behind a `dpcd_parsed` guard.** When the plug-time DPCD read didn't
  land, the policy never ran and the devicetree default `edp_lane_cnt = <4>` went straight to
  link training — four lanes into a Type-C pin-D link that has two wired. The two lanes that go
  nowhere never reach clock recovery, so the training loop walks the drive swing to its ceiling.
  It also explains why the `src_max_lane_debug` sysfs knob looked inert: it's read inside the
  branch being skipped.
- **`TR_INTERRUPT_CAUSE` latches and is read-to-clear.** A stale `REPLY_TIMEOUT` fails the *next*
  AUX request before it is even sent. Reading `TR_INTERRUPT_STATE` does not clear it.
- **The sink extcon notifier only delivers edges.** Type-C finishes negotiating DP alt mode at
  about t=5.03s while the DRM driver binds at t=5.47s, so a cable already plugged at power-on is
  announced to nobody. Reading the level at bind is what removes the manual `echo detect`.
- **`DP_STATUS`'s HPD bit is the real connection authority**, not "are we in a DP alt mode
  state". The mux points at DP a couple of hundred milliseconds before the sink is ready to
  answer AUX.
- **The combo PHY already knew the pin assignment.** `sunxi_cadence_combo0_dp_phy_validate()`
  correctly rejects more than two lanes on pin D/F — nothing ever called it. The lane budget came
  free once it was asked.

Worth recording alongside those: the A733's DP controller is standard **Synopsys DesignWare
DPTx** (`CORE_ID` reads `0x000a0509`, and the register map is textbook DWC DP), and
`AUX_CLOCK_DIVIDER` is the APB clock in MHz — `clk_bus_edp` is 26. Full breakdown in
[`patches/README.md`](patches/README.md).

Unlike `gmac/` and `npu-vipcore/`, **these are not modules.** `CONFIG_AW_CADENCE_COMBOPHY=y` and
`CONFIG_AW_DRM_EDP=y` mean the in-tree drivers already claim these compatibles at boot, so a
module declaring the same compatible would never bind. Running this code means rebuilding the
kernel Image. `dp/` mirrors the BSP layout exactly, so installing is a straight copy — plus one
device-tree property no overlay carries, noted in [`dp/README.md`](dp/README.md). The same changes
are in `patches/` as Armbian userpatches, which is the easier route and needs no hand-editing.

## Licensing and provenance

Mixed, and it matters, so here's the honest breakdown:

- **`gmac/dwmac-sun60iw2.c`** — GPL-2.0-or-later. My port of the vendor BSP's `dwmac-sunxi.c`
  (Copyright Allwinner Technology). 6.18 idioms modeled on in-tree `dwmac-sun55i.c`.
- **`gmac/stmmac-hdrs/`** — verbatim private headers from the Linux stmmac driver, GPL-2.0.
  Unmodified. They're here because an out-of-tree module can't reach them.
- **`npu-vipcore/vipcore/`** — VeriSilicon's driver, dual MIT/GPL, Copyright Vivante Corporation.
  My changes are four files plus a compat header, all listed in that README.
- **`aic8800/driver/`** — AICSemi's driver, as shipped in the Allwinner BSP. My patch on top is
  in `aic8800/patches/`.
- **`dp/`** — GPL-2.0. Allwinner BSP sources (Copyright Allwinner Technology) with my fixes
  integrated; every change is commented in place with the register and the manual section.
- **`patches/`, `overlays/`** — mine, GPL-2.0.

Nothing here has a redistribution problem. Everything vendor-derived is GPL or MIT and is
credited where it came from.

## Related

This came out of [`a7s-cyberdeck`](https://github.com/skitzo2000/a7s-cyberdeck), where the rest
of the deck lives — backplane, enclosure, build guide, and the open DRAM init work.

---

If you're bringing up an A733 and something here doesn't work for you, I'd like to hear about it.
Any questions let me know, and don't be afraid to poke me.

Sincerely,
Paul
