# dp — DisplayPort over USB-C

The five BSP sources on the A7S DisplayPort path, with the fixes integrated in place. This is the
same change as [`patches/0100`–`0110`](../patches/), in the other form: whole files instead of a
series. Pick one route, not both.

All five files are byte-identical to what you get by applying `0100`–`0110` to a stock BSP tree, so
the two routes cannot drift apart silently — a `diff` catches it.

**Status: working.** 2560×1440 on a cold boot with the cable already in, no debug knobs, no manual
`echo detect`, survives a reboot. The findings behind each fix are written up in
[`patches/README.md`](../patches/README.md); every change is also commented in place here, with the
register and the manual section it came from.

## These are not modules

`CONFIG_AW_CADENCE_COMBOPHY=y` and `CONFIG_AW_DRM_EDP=y`, so the in-tree drivers already claim these
compatibles at boot. A module declaring the same compatible would never bind. Running this code
means rebuilding the kernel Image either way.

Because of that, `patches/` is the easier route on Armbian — drop the series into
`userpatches/kernel/archive/sun60iw2-edge/` and the build applies it for you. Use `dp/` when you are
building the BSP tree directly, or when you want to read the finished file rather than a diff.

## Installing

The layout mirrors the BSP exactly: `dp/<path>` goes to `bsp/drivers/<path>` in the kernel source.

| This file | Destination |
|---|---|
| `drm/sunxi_drm_edp.c` | `bsp/drivers/drm/sunxi_drm_edp.c` |
| `drm/sunxi_device/sunxi_edp.c` | `bsp/drivers/drm/sunxi_device/sunxi_edp.c` |
| `drm/sunxi_device/hardware/lowlevel_edp/trilinear_dp14/trilinear_dp14.c` | same, under `bsp/drivers/drm/` |
| `phy/sunxi-cadence-combophy.c` | `bsp/drivers/phy/sunxi-cadence-combophy.c` |
| `usb/typec/mux/sunxi-phy-switcher.c` | `bsp/drivers/usb/typec/mux/sunxi-phy-switcher.c` |

So, from the repo root against a kernel tree at `$K`:

```sh
rsync -a dp/ "$K/bsp/drivers/"
```

These are the 6.18.19-edge-sun60iw2 versions of the vendor files. Copying them over a different BSP
drop silently discards whatever else changed in it — if you are not on that kernel, apply
`patches/` instead and let the hunks fail loudly.

## What a copy does *not* give you

The series also touches the device tree, and two of those bits do not live in any source file:

- **`aux_p` / `aux_n`** — the PL10/PL11 AUX DC-bias GPIOs (`0103`). Carried by the
  [`sun60i-a733-cubie-a7s-dp`](../overlays/) overlay, so the overlay covers this one.
- **`allwinner,typec-dp-no-hpd`** — the opt-in no-physical-HPD lifecycle (`0104`). **No overlay
  carries this.** Add it by hand to two nodes in `sun60i-a733-cubie-a7s.dts`: `phy_switcher@10`
  (alongside `aux_p`/`aux_n`) and the combophy node (alongside `typec_remap`). Both
  `sunxi-phy-switcher.c` and `sunxi-cadence-combophy.c` read it, and without it neither takes the
  no-HPD path.

You want the DP overlay regardless — it also fixes the alt-mode SVID width, without which DP alt
mode is never entered at all.

## Provenance

Allwinner BSP sources, Copyright Allwinner Technology, with my fixes integrated. GPL-2.0 as marked
in each file's SPDX line (`sunxi-phy-switcher.c` is GPL-2.0+, the DRM files GPL-2.0-or-later).
