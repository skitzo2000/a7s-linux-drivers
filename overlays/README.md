# overlays — device tree for the cyberdeck shield

Three overlays. One brings up the deck's screen, touch, and radio sockets. One shuts up a log
flood that was drowning everything else. One fixes the USB-C DisplayPort alt-mode SVID.

Both are BSP-dialect DT — `spi_sunxi_ng`, sun60iw2 pinctrl — built against 6.18.19-edge-sun60iw2.

## sun60i-a733-cyberdeck-shield.dts

SPI1 with an ILI9341 TFT, an XPT2046 touch controller, and two radio sockets.

| What | Pins |
|---|---|
| SPI1 | CLK PD11, MOSI PD12, MISO PD13 (mux 6) |
| TFT | CS PD10, DC PD14, RST PB2, BL PB6 |
| Touch | CS PM3, IRQ PM4 (PM bank is on `r_pio`) |
| Radio 1 (J5) | CS PJ24, flex pins PB3 and PJ25 |
| Radio 2 (J6) | CS PB7, flex pins PB8 and PG0 |

Every chip select is a plain GPIO.

The radio sockets are wired in nRF24 pin order, and the two flex pins are whatever that module
calls its interrupt and data lines — GDO0 and GDO2 on a CC1101. They're left as bare GPIOs on
purpose. spidev carries the register traffic and userspace reads the flex pins through gpiod, so
swapping a CC1101 for an nRF24 or a CC2500 is a userspace change, not a DT change.

J6's three pins were confirmed on the running board by forcing each pin's internal pull both ways.
With a module seated, J6.3 and J6.8 stop following the bias while every other spare pin still
does.

The GPIO cells are chip-relative banks: `&pio` B=1, D=3, J=9; `&r_pio` L=0, M=1. PJ=9 isn't in the
docs — it comes from the running kernel's own gpiochip0 numbering (gpio-310 is PIN_5 is PJ22, and
310 = 9×32 + 22).

The sockets sit on +3V3 straight off the A7S header through polyfuse F2. **F2 is the only link to
that rail**, and a screen-first build is allowed to leave it unpopulated — in which case a radio
reads exactly like a dead module, 0x00 or 0xFF on MISO. Check the fuse before you debug the radio.

## sun60i-a733-cyberdeck-noedp.dts

Disables `edp0@5720000`.

The built-in sunxi DRM binds it and runs an HPD poll thread that logs at KERN_INFO about twenty
times a second — 27,000+ lines a boot. journald catches all of it off `/dev/kmsg`. It burns CPU
and IO, and worse, it buries real traces. It's what hid the AIC8800 radio-hang signature from me
for longer than I'd like to admit.

The deck's display is the SPI panel on `fb0`, not `card0`, so nothing needs edp0.

**Superseded.** `patches/0102` drops those two prints to `EDP_DRV_DBG`, which kills the flood
without disabling the node — and disabling `edp0` also disables DisplayPort, so if you are running
that patch you want this overlay *out*. It is kept for anyone on a stock kernel.

## sun60i-a733-cubie-a7s-dp.dts

Everything DisplayPort-over-USB-C needs from the device tree, in one overlay. **Load this for DP;
it is the piece without which nothing else in the DP path matters.**

### DP alt-mode SVID width

The BSP declares the connector's alt mode with a 32-bit cell, `svid = <0xff01>`. On 6.18 the
Type-C class reads that property as a *16-bit* value:

```c
u16 svid;
ret = fwnode_property_read_u16(child, "svid", &svid);
```

`of_property_read_u16()` on a 4-byte big-endian property returns the first two bytes — `0x0000`.
(`vdo` uses the u32 accessor, which is why only `svid` is affected.) So the port advertised
DisplayPort under SVID 0, never matched the sink's `0xff01`, and DP alt mode was never entered:
the mux sat in `STATE_USB` with no DP lanes, no HPD, and no AUX ever attempted.

The fix is one line — declare `svid` at the width the kernel reads:

```dts
svid = /bits/ 16 <0xff01>;
```

### AUX DC bias

DP AUX over USB-C is AC-coupled and needs a DC bias applied at the source for the sink to
recognise a valid AUX partner. Which line is pulled up and which down depends on cable
orientation, since AUX+/AUX− map to SBU1/SBU2 differently when the plug is flipped. On the A7S
(schematic v1.10) that bias is two SoC GPIOs driving the DC side of the coupling caps:

```
AUXP ── C105 100nF ── SBU1          PL10 ── SBU1-DC
AUXN ── C107 100nF ── SBU2          PL11 ── SBU2-DC
```

`sunxi-phy-switcher.c` already implements the orientation swap; the vendor DTS just leaves the
two GPIOs commented out, so the code was unreachable. This overlay wires them up.

Note the vendor's `hotplug` GPIO stays commented out deliberately: PH4 is `GMAC1_TXD1` on this
board, and claiming it breaks ethernet. HPD arrives over CC as a VDM and reaches DRM through
extcon, so no GPIO is involved.

### Verifying

```sh
cat /sys/class/typec/port0/port0.0/svid                        # ff01, was 0000
cat /sys/class/typec/port0-partner/port0-partner.0/active      # yes, was no
cat /sys/class/drm/card0-DP-1/status                           # connected
```

With this overlay and the `0100`–`0110` kernel patches, the board negotiates DP alt mode, trains
the link and scans out. See [`patches/README.md`](../patches/README.md).

## Build and install

```sh
dtc -@ -I dts -O dtb -o sun60i-a733-cyberdeck-shield.dtbo    sun60i-a733-cyberdeck-shield.dts
dtc    -I dts -O dtb -o sun60i-a733-cyberdeck-noedp.dtbo     sun60i-a733-cyberdeck-noedp.dts
dtc -@ -I dts -O dtb -o sun60i-a733-cubie-a7s-dp.dtbo         sun60i-a733-cubie-a7s-dp.dts
sudo cp *.dtbo /boot/dtb/allwinner/overlay/
```

Then in `/boot/armbianEnv.txt` — on a stock kernel:

```
overlays=cyberdeck-shield cyberdeck-noedp
```

or, if you are running the `patches/` kernel and want DisplayPort:

```
overlays=cyberdeck-shield cubie-a7s-dp
```

Reboot. The shield overlay wants `-@` for the symbol table; the noedp one targets by node path and
doesn't.

All three compile clean on dtc 1.7.2. Worth reading back the DP one, since the whole point of it is
a property width:

```sh
dtc -I dtb -O dts sun60i-a733-cubie-a7s-dp.dtbo | grep svid    # svid = [ff 01];
```

Two bytes, not four — `[ff 01]` is what `fwnode_property_read_u16()` needs to see. If that comes
back as `<0x0000ff01>` the `/bits/ 16` got lost and DP alt mode will never be entered.
