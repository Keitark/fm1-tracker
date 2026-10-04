# SMB1 NES bench write — 2026-09-27

Historical write status: **written and twice readback verified; subsequent
normal boot produced a blank screen**. The user then requested V14 restoration,
which completed with two exact full readbacks. Current flash is the V14 backup,
not NES; see `FM1_V14_RESTORE_20260927.md`. No NES boot success is claimed.

The user authorized proceeding to NES after the official V14 update. This
bench attempt used the existing `jluboottool.py` CLI's `write` and `read`
commands, not KALLSUP firmware, ISD, or the abandoned custom protocol writer.
The original community RAM helper was reused, not uploaded again or patched.

## Exact inputs and scope

- Baseline: the two identical post-official-V14 1 MiB reads under
  `private-backups/post-official-update-20260927-230806/`.
- Baseline SHA-256:
  `bcf61795e2ddb201f2f72180ec8f3dca830c84a7c64d23d08475412dbcfe2f9c`.
- Application: peripheral NES with the privately supplied SMB1 ROM as default,
  167,072 bytes; SHA-256
  `a3e4f81914cceab7971d955832fa7b2821f3597a9dc27ac81a1cc2196e23216a`.
- Complete private candidate and both final reads have SHA-256
  `9873314f769c8b0670c6b03dab4ec2d84b29b473dd5ba16d23fd68586a8c59b8`.
- 41 changed 4 KiB sectors: `0x5000..0x2c000`, then `0x4000` last.
- Bootloader, non-application directory entries, unused application tail,
  settings/user-state and reserved identity/configuration bytes were retained
  from V14 exactly. No whole-chip erase, chip-key operation, reset, or automatic
  retry was performed. The original V14 backup is retained untouched.

## Method and evidence

`deploy_v14_smb1_via_cli.py` orchestrates CLI subprocesses only. It pins both
images, rechecks the current source/build manifest and executable audit, and
regenerates the candidate before any device command. The CLI's optional
`--fm1-bench` mode enforces one-shot device identity, exact NOR identity,
256-byte I/O and application-only aligned 4 KiB sector writes. It rejects
an online-device error instead of proceeding past it.

The live run completed two fresh full baseline reads, each sector preimage
check, each erase readback, each program readback, an additional CLI read of
each sector, then two full 1 MiB comparisons with the candidate. All passed.

Evidence: `private-backups/nes-smb1-v14-write-20260927-231607/`, especially
`deployment.json`, `console.log`, per-sector logs and `final-1.bin`/`final-2.bin`.
Raw data is private. No binary data or chip-key values belong in shared docs.

Before the attempt, twelve packaging tests, four orchestration/shell tests
(also under Python `-O`), four protocol guard tests, and ten reader tests passed.
An offline shell test caught an argument-shadowing error; it was fixed before
any live write.

## Remaining limits

The previous no-writer plan remains historical, not a claim that its proposed
independent NOR status polling and recovery qualification have been achieved.
This was an explicitly authorized experimental bench write using verified
readback. The original loader's internal completion limitations and status-
register initialization side effects still apply. No independent SR1 polling
or physical power-interruption recovery test was added or claimed.

The RAM helper was left running. A normal power cycle without the downloader
boot-entry sequence is needed to test NES startup, display, controls and audio.
Successful storage verification does not prove those behaviors. Keep external
audio disconnected for the first boot. Restoration has not been exercised.
