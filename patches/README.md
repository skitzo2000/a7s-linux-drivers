# patches

Kernel patches against the Allwinner BSP tree (`NickAlilovic/allwinner-bsp`, branch
`linux-6.18.z`, which mounts at `bsp/` inside the kernel source).

`0001` is a power fix and stands alone. `0100`–`0110` are the DisplayPort-over-USB-C series and
apply in order. All of them are `-p1` from the kernel source root, so the whole directory drops in
as one set.

## 0001 — AXP8191 stepped CPU-rail voltage

| Patch | What |
|---|---|
| `0001-axp8191-step-cpu-rail-voltage-transitions.patch` | Step the CPU rail in bounded increments instead of one jump |

The A733 hard-resets under load when cpufreq changes frequency — not at any particular frequency
or voltage. Pin the clock anywhere and the board runs a full 8-core load indefinitely; let
`ondemand` move it under that same load and it resets in minutes. The ethernet "hangs", the
under-load reboots and the thermal-throttle crashes were all this one bug (a throttle *is* a
frequency change). Verified on-board.

## 0100–0110 — DisplayPort over USB-C

**Status: working.** Cold boot with the cable already in, no debug knobs, no manual
`echo detect`, survives reboot. Verified 2026-08-19 on a Pluggable UD-ULTC4K dock's native
DP-alt output driving a 2560×1440 monitor.

| Patch | What |
|---|---|
| `0100-dp-combophy-dp-only-bringup-orientation-aux-pad.patch` | Bring the combo PHY up for the 4-lane DP-only pin C/E layout, latch orientation before the code that reads it, drive the AUX pad |
| `0101-dp-trilinear-aux-clkdiv-and-pad-direction.patch` | `AUX_CLOCK_DIVIDER` is the APB clock in MHz (`clk_bus_edp` = 26), and AUX is half-duplex so the pad direction must be driven per transaction |
| `0102-dp-edp-reinit-phy-before-aux-retry-and-quiet-detect.patch` | Recover the PHY and controller before an AUX retry; stop logging connector detect at info level (~27k lines/boot) |
| `0103-dp-dts-a7s-enable-aux-dc-bias-gpios.patch` | Wire up the PL10/PL11 AUX DC-bias GPIOs the vendor DTS leaves commented out |
| `0104-dp-a7s-typec-altmode-no-hpd-lifecycle.patch` | Opt-in lifecycle for boards with no usable physical HPD pin |
| `0105-dp-force-phy-cycle-before-typec-aux-retry.patch` | The generic PHY core refcounts `init`/`power_on`, so `0102`'s recovery was a no-op — drop the bind reference first |
| `0106-dp-trilinear-clear-stale-aux-interrupt-cause.patch` | `TR_INTERRUPT_CAUSE` latches and is read-to-clear; a stale `REPLY_TIMEOUT` failed the *next* request before it was sent. Also fixes a one-byte reply-FIFO overrun (`i <= len`) |
| `0107-dp-edp-pin-assignment-aware-link-policy.patch` | Rewrite `edp_update_capacity()` as a real DP link policy maker: pin-assignment lane budget and bandwidth-aware rate, applied whether or not DPCD was read |
| `0108-dp-edp-link-training-fallback.patch` | Bounded lane/rate step-down when training fails, instead of training once and giving up |
| `0109-dp-combophy-gate-software-hpd-on-dp-status.patch` | Require the real `DP_STATUS` HPD bit, not just "are we in a DP alt mode state" — it was firing ~230 ms early |
| `0110-dp-edp-adopt-already-connected-sink-at-bind.patch` | The sink extcon notifier is edge-only; read the level at bind so a cable already plugged at power-on is seen |

### What this turned up

The DP **link policy** was skipped whenever the plug-time DPCD read failed:

```c
if (drm_edp->dpcd_parsed && !edp_debug->lane_debug_en) {
        lane_para->lane_cnt = min(src_cap->max_lane, sink_cap->max_lane);
        lane_para->bit_rate = min(src_cap->max_rate, sink_cap->max_rate);
}
```

With no DPCD, `lane_para` kept the devicetree defaults — `edp_lane_cnt = <4>` — and the driver
trained four lanes into a Type-C pin-D link that only has two wired. The two lanes that go
nowhere never reach clock recovery, so the training loop walks the drive swing to its ceiling and
dies with `swing voltage reach max level, training1(clock recovery training) fail`. The same
guard is why the `src_max_lane_debug` sysfs attribute appeared to do nothing — it is read inside
the branch being skipped.

Several separate bugs conspired to make that DPCD read fail: the software HPD fired before the
sink was ready (`0109`), a latched interrupt cause failed the request before it was sent
(`0106`), and a cable present at power-on was never announced at all (`0110`).

Three more things worth knowing if you go digging in this IP:

- **AUX works.** Every receive-side counter reading zero points at the latched interrupt cause
  above, not at the analog path.
- **Sinks report their own lane count.** This dock advertises two lanes in `DPCD 0x002 = 0xc2`,
  so `min(src, sink)` already lands on 2 — the pin-assignment budget in `0107` is what covers the
  sinks that *don't*, such as a passive USB-C→DP adapter advertising four lanes on a pin-D link.
- **`TR_PHY_STATUS == 0` proves nothing.** The A733 manual declares that register
  implementation-defined and the vendor driver never uses it for control.

### Verifying

```sh
cat /sys/class/drm/card0-DP-1/status                        # connected
cat /sys/devices/virtual/edp/edp/attr/lane_config_now       # lane_para_use: user, lane_cnt: 2
```

Training success is silent, so trust the registers (`/dev/mem`, `CONFIG_STRICT_DEVMEM` is off):

| Register | Expected |
|---|---|
| `LANE_COUNT_SET` `0x5740004` | `0x2` — negotiated lane count |
| `TRAINING_PATTERN_SET` `0x574000c` | `0x0` — training complete |
| `LINK_BW_SET` `0x5740000` | `0x14` — 5.4 Gbps |
| `MAIN_HRES` / `MAIN_VRES` `0x5740834` / `0x5740838` | the active mode |

`tools/reg.py` reads these without hand-rolling `mmap`.

### Applying

These are delivered as Armbian userpatches. Drop them in
`userpatches/kernel/archive/sun60iw2-edge/` and build:

```sh
./compile.sh kernel BOARD=radxa-cubie-a7s BRANCH=edge KERNEL_CONFIGURE=no PREFER_DOCKER=yes
```

`PREFER_DOCKER=yes` runs the build in the Armbian container and needs no host sudo. Note that
`docker` and `kernel` are both framework commands, so `./compile.sh docker kernel …` fails. The
`P<hash>` field in the output `.deb` names hashes the patch set — it is the quickest check that
new patches were actually picked up.

The DP path also needs the device-tree overlays in [`overlays/`](../overlays/); without the SVID
one, DP alt mode is never entered at all.

### Sink coverage

Proven on Type-C **pin D** (2 lanes) via a dock's native DP-alt output. `0107` computes the lane
budget from the negotiated pin assignment, so **pin C/E** (4 lanes) is handled by the same code
path; more sinks are being brought through next.
