# patches — the DVFS reset fix

One patch, against the Allwinner BSP overlay tree. It's the fix for the single worst bug I hit on
this board.

## The A733 resets under load when cpufreq changes frequency

Not at a particular frequency. Not at a particular voltage. Pin the clock anywhere you like —
1508 MHz, 2 GHz, doesn't matter — and the board runs a full 8-core load for half an hour without
blinking. Let `ondemand` change frequency under that same load and it hard-resets in minutes.

That took a while to see, because every symptom pointed somewhere else. The ethernet "hang" was
this. The under-load reboots were this. The thermal throttle taking the box down was this — a
throttle *is* a frequency change.

**How it was pinned down:** pinned the big cluster at 2 GHz and bumped dcdc3 from 1050 to 1120 mV
by hand (`i2cset -f -y 0 0x36 0x14 0xbe`). The board survived load at both voltages, so it isn't
static undervolt. Then the fixed-frequency soak survived where `ondemand` died. It's the
transition.

**The code bug:** the AXP8191's CPU rails (dcdc2/3/4) go through `AXP_DESC_RANGES_VOL_DELAY`,
which writes the new selector in one shot. On this board that's up to a 250 mV jump — 800 to
1050 mV on the A76 rail — in a single write, and the rail glitches getting there. The driver
already has a stepped path for exactly this. It's gated behind
`CONFIG_AW_AXP1530_WORKAROUND_DVM`, which is off, and the AXP8191 descriptors never used it
anyway.

**The fix:** ramp up-transitions ~50 mV at a time with a 250 µs settle between steps. Down-
transitions can't undervolt, so those still go direct.

**Verified:** 10-minute soak of `ondemand` + full 8-core load + 2 GHz + crossing the 60 °C passive
trip, so governor transitions and thermal transitions firing continuously. Zero resets, zero Oops.
That's the exact condition that took the stock kernel down in under two minutes.

## Applying it

The regulator is `CONFIG_AW_REGULATOR_AXP2101=y` — built in, not a module. This needs a full Image
rebuild.

From the root of `radxa/allwinner-bsp` (branch `linux-6.18.x`), which mounts at `bsp/` in the
kernel tree:

```sh
patch -p1 < 0001-axp8191-step-cpu-rail-voltage-transitions.patch
```

Back up `/boot/Image` before you swap it. If the new one doesn't boot you have no console to tell
you why, and getting the card back out is the only way home.

## Worth knowing before you build on this

The BSP's "AXP8191" is the **AXP318W** — 9 DCDC and 28 LDO match exactly, and mainline U-Boot and
a mainline regulator driver both landed under that name.

**DCDC2 and DCDC3 have hardware DVM.** The rail walks to target on its own at one step per
15.625 µs or 31.25 µs, which is 640 or 320 mV/ms — both faster than this patch's 200 mV/ms. If you
can find the enable bit, hardware DVM is strictly better than a software loop for those two rails,
and DCDC4 is the only one that would still need the loop.

I haven't found that bit. No AXP8191 DVM register exists in any vendor header, and
`DCDC_MODE_CTL1–4` (0x1B–0x1E) read zero and are PWM/PFM only. The candidate is bit 7 on
0x13–0x1A, which boot0 sets and the live board matches — but **don't probe it empirically**, those
are live CPU rails. The mainline `regulator: axp20x` AXP318W series on lore.kernel.org should have
the real answer.

If it turns out hardware DVM covers dcdc2/3, this patch shrinks to dcdc4 only. If it doesn't, the
loop should at least get cheaper: `usleep_range` instead of `udelay`, and drop `.set_voltage_time`
so the regulator core stops adding a second full ramp delay on top of ours.

## Related

`gmac/README.md` covers the other half of the instability — the cpuidle race that this patch
doesn't touch. You want both: this patch for the transitions, deep-idle disabled for the race, and
a fan so the thermal trip stops firing in the first place.
