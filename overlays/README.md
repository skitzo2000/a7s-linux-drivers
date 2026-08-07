# overlays — device tree for the cyberdeck shield

Two overlays. One brings up the deck's screen, touch, and radio sockets. The other shuts up a log
flood that was drowning everything else.

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

## Build and install

```sh
dtc -@ -I dts -O dtb -o sun60i-a733-cyberdeck-shield.dtbo sun60i-a733-cyberdeck-shield.dts
dtc    -I dts -O dtb -o sun60i-a733-cyberdeck-noedp.dtbo  sun60i-a733-cyberdeck-noedp.dts
sudo cp *.dtbo /boot/dtb/allwinner/overlay/
```

Then in `/boot/armbianEnv.txt`:

```
overlays=cyberdeck-shield cyberdeck-noedp
```

Reboot. The shield overlay wants `-@` for the symbol table; the noedp one targets by node path and
doesn't.
