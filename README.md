# a7s-linux-drivers

Out-of-tree kernel drivers, patches, and device-tree overlays for the **Radxa Cubie A7S** —
Allwinner **A733**, die `sun60iw2`, on **Linux 6.18**.

Mainline doesn't know this SoC yet. The 6.18 edge images boot, but a few things the board
actually has are missing on the software side: the ethernet MAC has a DT node and no driver, the
NPU node gets grabbed by the wrong driver, and cpufreq transitions take the box down under load.
This repo is what I wrote and ported to close those gaps while building a cyberdeck around the
board.

Everything here has been on real hardware. Where something works but isn't finished, the README
for that piece says so.

## What's in here

| Directory | What it is | State |
|---|---|---|
| [`gmac/`](gmac/) | `dwmac-sun60iw2` — stmmac glue for the GMAC-210, ported to 6.18 | eth0 at 1 Gbps, DHCP, clean counters |
| [`npu-vipcore/`](npu-vipcore/) | VIP9000 NPU driver ported to 6.18 | `/dev/vipcore` live; userspace runtime still missing |
| [`aic8800/`](aic8800/) | AIC8800D80 WiFi/BT tree + two fixes | both fixes verified on-board |
| [`patches/`](patches/) | AXP8191 stepped CPU-rail voltage — the DVFS reset fix | verified, needs a full Image rebuild |
| [`overlays/`](overlays/) | Device-tree overlays for the cyberdeck shield | in daily use |

## The kernel

Everything was built and run against **6.18.19-edge-sun60iw2** — the Armbian unofficial edge
build for the Cubie A7S (`Radxa-mainline-WIP-a7s`). The two modules build on the board itself;
install `linux-headers-$(uname -r)` and `build-essential` and you have what you need.

Other 6.18.x kernels should be fine. Anything older will fight you — a good chunk of the porting
work here is 6.18 API churn (`nth_page()` gone, `.remove` returning void, `EXTRA_CFLAGS` retired),
and none of it is version-gated backward.

## Building

`build-modules.sh` builds and installs both modules, runs depmod, and tells you what landed.
Run it as root, on the board:

```sh
sudo ./build-modules.sh
```

It builds out of a temp copy so the repo stays clean, and it skips anything already installed in
`/lib/modules/$(uname -r)/extra/`. Delete the `.ko` there to force a rebuild.

Each directory also has a plain `make` path if you'd rather do one at a time. See its README.

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
