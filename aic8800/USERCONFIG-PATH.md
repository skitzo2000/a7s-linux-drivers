# AIC8800D80 WiFi: driver looks for its config files in the wrong directory

**Status:** FIXED AND VERIFIED on-board 2026-07-26 by symlink. **The newest vendor drop has the
same bug.** The *mechanism* by which the subdir append is skipped is confirmed by runtime
evidence but its origin in the build is NOT yet pinned down — see "Unresolved" below.

Verification after reboot:

```
rwnx_load_firmware :firmware path = /lib/firmware/aic_userconfig_8800d80.txt
### Load file done: aic_userconfig_8800d80.txt, size=2724
userconfig download complete
rwnx_send_txpwr_lvl_v3_req:lvl_11b_11ag_1m_2g4:18     (was 20)
rwnx_send_txpwr_lvl_v3_req:lvl_11n_11ac_mcs9_2g4:14   (was 16)
rwnx_send_txpwr_lvl_v3_req:lvl_11ax_mcs11_2g4:13      (was 15)
rwnx_send_txpwr_lvl_v3_req:lvl_11ax_mcs11_5g:12       (was 14)
```

The calibrated table is now actually pushed to firmware via `rwnx_send_txpwr_lvl_v3_req`, not
just parsed.

## Symptom

Every boot, `aic8800_fdrv` logs:

```
AICWFDBG(LOGERROR)  rwnx_load_firmware: aic_userconfig_8800d80.txt file failed to open
AICWFDBG(LOGERROR)  wrong size of firmware file
```

WiFi then comes up on the driver's **built-in default** txpower/calibration table instead of
the board's on-disk calibration.

## Root cause (confirmed empirically, not inferred)

`rwnx_load_firmware()` logs the path it builds at `LOGINFO`, *before* `filp_open()`. The stock
log level is `LOGERROR` (1), which hides it. Setting `aicwf_dbg_level=3` revealed:

```
rwnx_load_firmware :firmware path = /lib/firmware/aic_userconfig_8800d80.txt
```

The `aic8800D80/` subdirectory is missing. That subdir is appended by
`aicwf_compat_8800d80.c`:

```c
int rwnx_plat_userconfig_load_8800d80(struct rwnx_hw *rwnx_hw){
    ...
#ifndef ANDROID_PLATFORM
            sprintf(aic_fw_path, "%s/%s", aic_fw_path, "aic8800D80");
#endif
```

The append did not happen, so **`ANDROID_PLATFORM` is defined in the built module** — that is
the only branch that skips it, and `aic_fw_path` was exactly `/lib/firmware` (so
`rwnx_init_aic()`'s `get_fw_path()` had already run and set the default correctly).

### RESOLVED: where the define comes from

`CONFIG_PLATFORM_ALLWINNER` is **also a kernel config symbol** (the BSP's top-level `Kconfig`
declares `config PLATFORM_ALLWINNER`). The deck's kernel has it enabled:

```
/proc/config.gz                                   -> CONFIG_PLATFORM_ALLWINNER=y
linux-headers-6.18.19-edge-sun60iw2/include/config/auto.conf -> CONFIG_PLATFORM_ALLWINNER=y
```

kbuild includes `auto.conf`, so the variable is **already set** before the driver Makefile's
`CONFIG_PLATFORM_ALLWINNER ?= n` runs — making the `?=` a no-op. The Allwinner branch fires and
adds `-DANDROID_PLATFORM`. This is why a plain reading of the Makefile is misleading, and it
affects **out-of-tree builds too**, not just the in-tree BSP build.

Note it is not selected via the SDIO choice (`CONFIG_AIC8800_SDIO is not set`,
`CONFIG_AIC8800_USB=y`) — the defconfig sets it directly.

**The trap is the vendor's naming:** `PLATFORM_ALLWINNER` means *"Allwinner **Android** BSP"*,
not "Allwinner SoC". The A7S is Allwinner hardware running Debian, so the flag is simply wrong
for us.

### Source-level fix (built and verified, NOT yet installed)

Override it on the make command line — command-line assignments outrank the `auto.conf` value:

```sh
make -C usb/aic_load_fw    KDIR=/lib/modules/$(uname -r)/build \
     CONFIG_PLATFORM_ALLWINNER=n CONFIG_PLATFORM_UBUNTU=y
make -C usb/aic8800_fdrv   KDIR=/lib/modules/$(uname -r)/build \
     KBUILD_EXTRA_SYMBOLS=$PWD/usb/aic_load_fw/Module.symvers \
     CONFIG_PLATFORM_ALLWINNER=n CONFIG_PLATFORM_UBUNTU=y
```

Verify with `grep -c DANDROID_PLATFORM usb/aic8800_fdrv/.aicwf_compat_8800d80.o.cmd` → must be 0.

**Both modules must be replaced as a pair**: the newer `aic8800_fdrv` needs
`get_flash_bin_size` / `get_flash_bin_crc`, which only the newer `aic_load_fw` exports (our
running one exports neither — 0 hits in `/proc/kallsyms`). Building `fdrv` alone fails at MODPOST.

Side effects of dropping the define, each checked:
- the 4 chip-specific firmware-path appends (D80, D80X2, DC, D80N, DLN) start working — the fix
- 2 `CONFIG_WOWLAN`-gated sites — inert, `CONFIG_WOWLAN` is not in the build flags
- `usb_driver.supports_autosuspend` 1 → 0 — a real change, and likely *better* for an always-on
  deck whose USB is shared with Bluetooth
- `HIGH_KERNEL_VERSION*` thresholds shift (5.15.41/5.15.41/5.15.104/6.1.0 → 6.0.0/6.1.0/6.3.0/
  6.3.0) — no effect, 6.18.19 exceeds all of them in both branches

Meanwhile `aic_load_fw` (the BT/firmware loader) builds its paths differently — explicit
per-chip subdir handling in `aicbluetooth.c` — which is why *its* loads succeed from
`/lib/firmware/aic8800D80/` and only the WiFi driver's config load fails.

Two other notes on that same line: the `sprintf` has overlapping source and destination
(undefined behaviour, benign here only by luck), and it appends unconditionally, so it would
double the subdir if ever called twice.

## What we were actually losing

The on-disk table is **lower** than the driver's fallback — the radio was transmitting roughly
2 dB **above** the vendor's calibrated ceiling:

| Key | On-disk (correct) | Fallback (was in use) |
|---|---|---|
| `lvl_11b_11ag_1m_2g4` | 18 | 20 |
| `lvl_11n_11ac_mcs9_2g4` | 14 | 16 |
| `lvl_11ax_mcs11_2g4` | 13 | 15 |
| `lvl_11ax_mcs11_5g` | (from file) | 14 |

Over-driving the PA degrades EVM at high MCS — retries, reduced throughput, flaky links — and
costs battery on a deck. Crystal settings are unaffected: the file's `xtal_enable=0`,
`xtal_cap=24`, `xtal_cap_fine=31` are identical to the fallback.

## Fix applied

```sh
ln -sf aic8800D80/aic_userconfig_8800d80.txt /lib/firmware/aic_userconfig_8800d80.txt
ln -sf aic8800D80/aic_powerlimit_8800d80.txt /lib/firmware/aic_powerlimit_8800d80.txt
```

Chosen over a rebuild because it is zero-risk, needs no module swap (wlan0 is the board's only
link), and survives reboots. **Re-apply after any reimage.**

## Proper fix (not yet done)

Either drop `-DANDROID_PLATFORM` from the WiFi module's build flags, or make the path logic
unconditional. Wants a rebuild of `aic8800_fdrv` and a way to recover the board if the new
module fails to probe — see `kernel/aic8800-618/` for the staged newer driver.

## Diagnostic left in place

`/etc/modprobe.d/aic8800-diag.conf` sets `aicwf_dbg_level=3` so the path stays visible.
Remove it once this is settled — it is verbose (dumps the whole txpower table each boot).
