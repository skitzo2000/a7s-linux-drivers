# RTL-SDR — the in-kernel SDR driver the A7S kernel leaves out

The vendor kernel (6.18.19-edge-sun60iw2) ships almost the whole RTL2832U stack:
`dvb-core`, `rc-core` and `i2c-mux` built in; `rtl2832`, `rtl2832_sdr`, `r820t` and the
other demods and tuners as modules. What it does **not** build is the USB glue that binds
the dongle and creates those devices — `CONFIG_DVB_USB_V2` and `CONFIG_DVB_USB_RTL28XXU`
are "not set". Plug an RTL-SDR in and nothing happens: no `/dev/swradio0`, no `/dev/dvb`.

`src/` is those two drivers, **verbatim from v6.18.19** (`drivers/media/usb/dvb-usb-v2/`),
built out of tree against the headers package — the same shape as `gmac/`:

| module | what |
|---|---|
| `dvb_usb_v2.ko` | the DVB-USB v2 framework (`dvb_usb_core.c`, `dvb_usb_urb.c`, `usb_urb.c`) |
| `dvb-usb-rtl28xxu.ko` | Realtek RTL28xxU: probes the dongle, attaches `rtl2832` + the tuner, registers the SDR |

The demod/tuner attach headers (`rtl2832_sdr.h`, `r820t.h`, …) normally come from
`drivers/media/{dvb-frontends,tuners}`, which the headers package does not ship; copies
sit next to the sources. No source changes.

## Build and install (on the board)

```sh
cd rtl-sdr/src
make                 # against /lib/modules/$(uname -r)/build
sudo make install    # → /lib/modules/$(uname -r)/extra + depmod
sudo modprobe dvb-usb-rtl28xxu
```

`build-modules.sh` at the repo root does the same as part of its run. Loads at boot once
installed (udev matches the USB id).

## Verified (a7s-3, 2026-09-08)

RTL-SDR with an R828D tuner (RTL-SDR Blog V4-class), 0bda:2838 on a USB hub:

```
r820t 4-003a: Rafael Micro r820t successfully identified, chip type: R828D
rtl2832_sdr rtl2832_sdr.3.auto: Registered as swradio0
rtl2832_sdr rtl2832_sdr.3.auto: Realtek RTL2832 SDR attached
usb 4-1.2.2: dvb_usb_v2: 'Realtek RTL2832U reference design' successfully initialized and connected
```

`/dev/swradio0` (root:video 0660), `/dev/dvb/adapter0/{frontend0,demux0,dvr0}` and an IR
receiver `rc0` appear. A plain `read()` on `swradio0` after `VIDIOC_S_FREQUENCY` (tuner 0 =
ADC rate, tuner 1 = RF) returns CU8 IQ centred on 127 with live variance at 915, 433.92 and
100.1 MHz. A slow reader gets `video buffer is full, N packets dropped` — that is the
reader, not the driver.

## Two APIs, one dongle

- **V4L2 SDR** (`/dev/swradio0`) — what this driver gives you: GNU Radio / gr-osmosdr,
  SoapySDR's V4L2 module, or a raw reader. Group `video`.
- **librtlsdr** (`rtl_433`, `rtl_power`, `rtl_fm`, SDR++) talks to the dongle over libusb
  and needs the kernel driver *off* the interface: builds with `DETACH_KERNEL_DRIVER`
  detach it themselves; otherwise `sudo rmmod dvb_usb_rtl28xxu` (or a modprobe blacklist)
  before use. Both can live on one board — not at the same time on one dongle.
