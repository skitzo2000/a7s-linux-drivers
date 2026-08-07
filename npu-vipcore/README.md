# vipcore — the A733 NPU on 6.18

The A7S has a VIP9000 NPU, roughly 3 TOPS, and it's the reason to own this board. On the vendor
5.15/6.6 BSP it works. On 6.18 the driver doesn't build and the DT node gets claimed by the wrong
driver. This is the port that fixes both.

Source is the vendor BSP's `aw_nna_vip/vip2` (VIPLite 2.0.3), from
`radxa/allwinner-bsp`. My changes are small and listed below — this is a port, not a rewrite.

Loads clean on 6.18.19-edge-sun60iw2:

```
VIPLite driver version 2.0.3.0-AW
OPP table 492 / 852 / 1008 MHz, devfreq up, thermal cooling device registered
reserved 4MB video heap @ 0x45800000, IRQ 484
/dev/vipcore
```

## Build

On the board (gcc 14.2 is what's there and it's fine):

```sh
make -C /lib/modules/$(uname -r)/build M=$PWD/vipcore modules
sudo install -m 0644 vipcore/vipcore.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
```

Autoload it with `/etc/modules-load.d/vipcore.conf` containing `vipcore`.

One gotcha that cost me an hour: `scp`ing this tree once landed a source file as all NUL bytes.
It compiled to an empty `.o` and modpost complained about a missing `MODULE_LICENSE`, which sends
you looking in entirely the wrong place. If the build says something that strange, check the
files came across whole.

## You have to block etnaviv first

The edge DT calls the NPU node `compatible = "vivante,gc"`, and etnaviv grabs it before vipcore
gets a look. No `/dev/vipcore`, no obvious error.

A plain `blacklist etnaviv` is **not** enough — it still loads at boot, probably pulled in through
the initramfs, and takes the node. What works, in `/etc/modprobe.d/blacklist-etnaviv.conf`:

```
install etnaviv /bin/true
blacklist etnaviv
```

If it sneaks in anyway, you can take the node back by hand:

```sh
rmmod vipcore
echo 3600000.npu > /sys/bus/platform/drivers/etnaviv/unbind
rmmod etnaviv
modprobe vipcore
```

## What I changed

Six files. Everything else is stock vendor code.

**`compat618.h`** (new) — force-included into every translation unit via `-include`. Right now
it's just `nth_page()`, which 6.18 removed. Pages inside one allocation are contiguous in the
memmap, so pointer arithmetic is the replacement.

**`Kbuild`** — out-of-tree build. `$(srctree)/$(src)` becomes `$(M)`, `obj-$(CONFIG_AW_NNA_VIP)`
becomes `obj-m` (there's no vendor Kconfig symbol out here), and `-objs` becomes `-y`. 6.18 kbuild
dropped `EXTRA_CFLAGS` entirely, so the accumulated flags get handed to `ccflags-y` at the end.
It also fakes `CONFIG_ARCH_SUN60IW2` and `CONFIG_AW_PM_DOMAINS`, which the code keys its sun60iw2
paths off and which don't exist on an edge kernel.

**`os/linux/vip_drv_device_driver.c`** — `platform_driver.remove` returns void since 6.11, and
one more `nth_page()`.

**`os/linux/allocator/vip_drv_mem_allocator_common.c`** — `MODULE_IMPORT_NS` takes a string
literal since 6.13. `nth_page()` again. And `vipdrv_import_pfn_map` now returns
`VIP_ERROR_NOT_SUPPORTED` on 6.18, because `__pte_offset_map_lock` isn't exported to modules
anymore. That path only serves wrapping foreign `VM_PFNMAP` mappings, which model execution never
does — failing cleanly beats hand-walking page tables to keep a dead path alive.

**`os/linux/platform/allwinner/vip_drv_device_platform.c`** — adds `"vivante,gc"` to the match
table, plus fallbacks for every clock and reset name so it binds the edge DT as well as the
vendor one (`core`/`bus`/`mbus`/`reg`, `axi`/`ahb`). The mainline node has no PLL parent phandle,
so the parent clock is optional now and rate propagation through the CCF covers it.

**`os/linux/platform/allwinner/sunxi-sid.h`, `sunxi-smc.h`** (new) — stubs for two vendor headers
that don't exist outside the BSP tree. They're for VF binning; the sun60iw2 path never calls
them.

## What's still missing

**Userspace.** The kernel side is done and the device node is live, but you can't run a model
yet. The A733 directory in Allwinner's model zoo ships `libVIPhal.so` and `libNBGlinker.so` and
**not** `libVIPlite.so`, which is the one that matters. That plus `vpm_run` out of the
viplite-tina SDK is what's between here and inference.

Compiled NBG models are ready and waiting on it — yolov8n quantized int16 and pcq, both built for
the A733 (chip PID `0x1000003B`) and verified by their `VPMN` magic.

**Devfreq tops out at 852 MHz.** The 1008 MHz OPP wants 960 mV, regulator control fails, and the
rail stays at 800 mV. Not chased down yet.
