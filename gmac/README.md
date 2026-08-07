# dwmac-sun60iw2 — ethernet for the A733 on 6.18

The 6.18 edge image ships the DT node for the A7S gigabit MAC and nothing that binds to it. So
there's no eth0, and no error explaining why. This module is the missing half.

It's a port of the vendor BSP glue (`allwinner-bsp/drivers/stmmac/dwmac-sunxi.c`) to 6.18 APIs,
with the 6.18 idioms taken from in-tree `dwmac-sun55i.c`. Result on the board: **eth0 at 1
Gbps/Full, DHCP, zero-loss flood ping, clean counters.**

The PHY is a YT8531, but it reports id `0x7b744412` — which the in-tree Motorcomm driver doesn't
match. Generic PHY drives it fine, so don't go hunting for a PHY driver. There isn't one, and you
don't need one.

## Build

On the board:

```sh
make
sudo install -m 0644 dwmac-sun60iw2.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
sudo modprobe dwmac-sun60iw2
```

Or point it at a different kernel:

```sh
make KDIR=/usr/src/linux-headers-6.18.19-edge-sun60iw2
```

`stmmac-hdrs/` holds the stmmac private headers copied verbatim from the matching kernel source.
Out-of-tree modules can't reach `drivers/net/ethernet/stmicro/stmmac/*.h`, and the glue needs
them. If you build against a different 6.18.x, re-copy that tree's headers over this directory —
they're kernel-internal and they move.

## Two things to know before you load it

**Don't turn MULTI_MSI back on.** The vendor variant enables per-queue tx0/rx0 interrupts. On
6.18 those hard-panic the stmmac core at link-up, and then it re-panics every boot for as long as
the cable is plugged in — recovery is unplug, power-cycle. Single `macirq`, the way sun55i does
it, is stable. The flag is dropped in `dwmac210_variant` with a comment on it. Leave it dropped.

**LPI gets stripped in probe, on purpose.** With EEE/Low-Power-Idle live, an idle link puts the
wrapper's TX clock gating into a state it doesn't come back from, and the SoC hard-locks a few
minutes after link-up. Probe clears `STMMAC_FLAG_EN_TX_LPI_CLOCKGATING`,
`STMMAC_FLAG_EN_TX_LPI_CLK_PHY_CAP`, and `axi_lpi_en` no matter what the DT asks for.

## About the idle hang

If you go digging through the history on this driver, you'll find a long hunt for a hard lock
that showed up 11–14 minutes after ethernet load. Worth knowing how it ended, because it wasn't
the ethernet driver.

Serial console plus a board-local sysrq loop caught it: kworkers piling up in D-state on
`Workqueue: events handle_update`, which is cpufreq policy update work. Ethernet load heats the
SoC past the 60 °C passive trip, the thermal governor forces a cpufreq transition, and that
transition races deep cpuidle entry (PSCI `CPU_SUSPEND`, handled by SCP firmware). The kernel
then took an `Oops - Undefined instruction` on the *idle task* and froze. Ethernet was just the
thing generating enough heat to trigger it.

Two fixes, both applied here, and they stack:

1. **Active cooling.** Keep it under 60 °C and the trip never fires.
2. **Disable the deep idle states** — `echo 1 > /sys/devices/system/cpu/cpuN/cpuidle/state{1,2}/disable`
   on all eight cores, leaving only WFI. WFI wakes in about a microsecond, so this costs nothing
   you'll feel, and it removes the idle side of the race.

The `patches/` directory one level up has the third piece: the AXP8191 voltage-stepping fix for
the transitions themselves.

## Quirks

The MAC address isn't stable across cold boots — the vendor's SID helper is stubbed out in this
port. If that bothers you, pin it with NetworkManager's `cloned-mac-address`.
