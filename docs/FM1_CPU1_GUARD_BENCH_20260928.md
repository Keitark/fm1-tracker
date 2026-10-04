# CPU1 SYS_DIV guard experiment, 2026-09-28

**Flashed and twice readback-verified.** All 19 sectors verified; both complete
1,048,576-byte final files independently hash to the candidate SHA-256 below.
Receipt status is `written_and_twice_readback_verified`. No reset was sent.
**Display acceptance failed:** the user subsequently reported no screen for
this version. Gameplay is not established. The successful write/readback is
still valid, but the divider guard was not sufficient to fix startup/display.
See `FM1_NO_SCREEN_GUARD_FOLLOWUP_20260928.md` for the offline follow-up.

## Intent

User requested writing the modified version after the stock-first startup
comparison. This experiment implements only the missing stock divider guard
around CPU1 release/ready wait. It retains the SDK SMP scheduler, current
stock-derived power parameters, screen-first display, SMB1 and integrity checks.
This is a hypothesis test, not a proved root-cause fix or hardware-qualified NES.

## Exact modification

`firmware/nes/cpu1_guard.py` extracts `port.c.o` from the pinned SDK system.a,
adds volatile save/OR-8/restore operations inside `EnableOtherCpu`, and produces
a separate archive in the build directory. The original SDK is unchanged.
Archive member names/order and every member except `port.c.o` are checked for
byte equality. Removing the two guard insertions from the edited LLVM IR must
reproduce the entire original IR. No SDK authentication/integrity logic changes.

The actual linked CPU0 path is reviewed and checked at `02027000..0202703E`:

- CPU1 bypasses the guard and proceeds to task restore as before.
- CPU0 writes the existing CPU1 vector at `01C7FFF8`.
- At `02027010..1C`, load/save SYS_DIV `00010008`, OR with 8, write it.
- Clear the existing ready flag `01C1AA2D`; release CPU1 using existing writes.
- At `02027036..38`, wait for the existing CPU1 ready flag.
- At `0202703A`, restore the original SYS_DIV, before `lockset`/task restore.

Like stock, the ready wait remains unbounded. The divider is restored on the
successful ready path; if CPU1 never responds, the firmware cannot reach that
restore. No claim is made that this experiment establishes whether CPU1 starts.
If blank again, do not repeatedly flash the same image as a new diagnosis.

## Artifacts and checks

Build: `firmware/nes/build/boot-cpu1-guard-smb1/`.
Application: 181,136 bytes, SHA-256
`63476ce1f89cd6b83fe4a915396a07668bdfca49696d3917e91aff523508a6ea`.
Full image: `private-backups/nes-candidates/smb1/screen-first/51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1/fm1-nes-stock-layout.UNQUALIFIED.bin`.
Full image SHA-256:
`51dc35fdf0bcbe4e105c4f84e5c4ee52e827f4a88169c81b65541488d5998aa1`.

- 15 host CTest groups, 26 binary audit tests and 3 stock power tests pass.
- 3 guard tests pass normally and with `-O`: exact IR insertion scope, archive
  member scope and rejection of mutations at every byte of the linked guard.
- 5 fake-device deployment tests pass normally and with `-O`: forward write,
  V14 restore, fail-stop behavior, exact scope and existing writer selection.
- SDK and peak-nes dependency checkouts remain clean.
- Build log `tmp/cpu1-guard-build.log`; package log `tmp/cpu1-guard-package.log`.

The image preserves boot/configuration/reserved bytes. Relative to the failed
screen-first image, 19 sectors change (10,775 bytes differ, including link
relocations); the application directory sector `4000` is written last.
No whole-chip erase, guessed pins, new rail setting, core-count change, key
query or automatic reset is introduced.

## Device operations and acceptance

Cold UBOOT full read through existing `jluboottool.py`:
`private-backups/pre-cpu1-guard-20260928-021425/`.
It uploaded the original WL82 helper to RAM (not KALLSUP), did not program/erase
flash, and returned exactly the expected previous screen-first SHA-256
`198140cbf6cb4d052ecc9fbb610ad0f9619acadb65888adee1e172827fc87e95`.

Deployment directory: `private-backups/cpu1-guard-write-20260928-021650/`.
The selector `deploy_cpu1_guard_via_cli.py` delegates device operations to the
existing `deploy_v14_smb1_via_cli.py` / `jluboottool.py` flow. It requires two
fresh full source matches, each sector preimage, sector erase/program/readback,
and two final full matches. The same established application bounds apply.
`deployment.json` is authoritative for write completion; this note does not
substitute for a receipt. The subsequent visible-output bench test failed;
execution reaching the display routine and gameplay remain unverified.

Expected visible sequence after a successful normal boot: RGB bands/yellow
strip, then later blue checkpoint, then SMB1. The early picture still follows
OS startup; this experiment changes CPU1 timing, not that placement.

Rollback selector supports `--restore-v14` from the exact complete modified
image to the verified stock V14 backup. Do not use it against a partial write
or another image; preflight must match. Restoration is not automatic.
