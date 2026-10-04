"""Read FM-1 flash through the WL82 UBOOT loader.

This adapts kagaimiq/jl-uboot-tool's WL82 configuration and protocol classes.
It uploads the helper loader to volatile RAM, or reuses an already running
loader, then reads flash. It never sends the chip-key query or invokes flash
erase/write commands. Raw backups include device configuration; keep private.
The helper's online-device command CAN write NOR protection/status registers;
this is not a side-effect-free device inspection. See WL82_LOADER_AUDIT.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path


TOOL_ROOT = Path(__file__).resolve().parents[1] / "jl-uboot-tool"
sys.path.insert(0, str(TOOL_ROOT))

import yaml

from jltech.crc import jl_crc16
from jltech.uboot import JL_LoaderV2, JL_UBOOT
from scsiio import SCSIDev


FLASH_READ_CHUNK = 256
FM1_FLASH_SIZE = 0x100000
FM1_FLASH_ID = 0x856014
BOOT_APP_PREFIX_LENGTH = 0x54000
V14_MARKER_OFFSET = 0x53978
V15_MARKER_OFFSET = 0x52A64
V14_BOOT_APP_LENGTH = 0x98000
V15_BOOT_APP_LENGTH = 0x97000
SPI_NOR_DEVICE_TYPE = 0x03
WL82_LOADER_ADDRESS = 0x01c02000
WL82_LOADER_SIZE = 24064
WL82_LOADER_SHA256 = 'd41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
LOADER_SIDE_EFFECT_WARNING = (
    'WARNING: WL82 loader initialization/online-device can change NOR status '
    'and protection bits, even for readback. No flash-array program/erase '
    'command is requested. See tools/jieli-wl82/WL82_LOADER_AUDIT.md.'
)


def load_wl82_profile() -> tuple[dict, dict, Path]:
    data_dir = TOOL_ROOT / "data"
    chips = yaml.safe_load((data_dir / "chips.yaml").read_text(encoding="utf-8"))
    loaders = yaml.safe_load((data_dir / "usb-loaders.yaml").read_text(encoding="utf-8"))
    chip = chips["wl82"]
    loader = loaders["wl82"]
    loader_path = data_dir / loader["file"]
    if not loader_path.is_file():
        raise FileNotFoundError(f"WL82 RAM-loader blob is missing: {loader_path}")
    return chip, loader, loader_path


def read_inquiry(dev: SCSIDev) -> tuple[str, str, str]:
    response = bytearray(36)
    residue = dev.execute(b"\x12\x00\x00\x00\x24\x00", None, response)
    if residue != 0 or len(response) != 36:
        raise IOError('Incomplete SCSI inquiry; target identity not established')
    return (
        bytes(response[8:16]).decode("ascii").strip(),
        bytes(response[16:32]).decode("ascii").strip(),
        bytes(response[32:36]).decode("ascii").strip(),
    )


def upload_wl82_ram_loader(dev: SCSIDev, chip: dict, loader_spec: dict, loader_path: Path) -> None:
    """Upload only the reviewed original helper; stop on the first failure.

    No retry, reset, key access or automatic RAM readback. A transport-complete
    write is not a verified RAM readback or evidence of cache coherence.
    UBOOT inquiry text alone does not establish a freshly reset ROM session.
    """
    address = int(loader_spec["address"])
    block_size = int(loader_spec.get("blocksize", loader_spec.get("block-size", 512)))
    if (address != WL82_LOADER_ADDRESS or block_size != 512 or
            any(int(loader_spec[key]) != 512 for key in ('blocksize','block-size') if key in loader_spec) or
            loader_spec.get('encryption') != 'MengLi' or
            loader_spec.get('info',{}).get('protocol-version') != 'v2' or
            chip.get('uboot1.00',{}).get('protocol-version') != 'v2' or
            chip.get('uboot1.00',{}).get('quirks',{}).get('memory-rw-mengli-crypt') is not True):
        raise ValueError('Unreviewed WL82 upload profile; no commands sent')
    # Read and validate the whole source before the first device operation.
    # The reviewed blob is already encoded separately for each 512-byte write.
    raw = loader_path.read_bytes()
    if len(raw) != WL82_LOADER_SIZE or hashlib.sha256(raw).hexdigest() != WL82_LOADER_SHA256:
        raise ValueError('Unreviewed WL82 loader image; no commands sent')

    uboot = JL_UBOOT(dev)
    for offset in range(0,len(raw),block_size):
        payload = raw[offset:offset+block_size]
        args = ((address+offset).to_bytes(4,'big') + len(payload).to_bytes(2,'big') +
                b'\0' + jl_crc16(payload).to_bytes(2,'little'))
        residue = dev.execute(uboot.cmd_prepare_cdb(JL_UBOOT.Cmd.WRITE_MEMORY,args),payload,None)
        if residue != 0:
            raise IOError(f'Incomplete RAM write at 0x{address+offset:x}; '
                          'loader jump not sent, device RAM state uncertain')

    # Argument 1 selects SPI NOR, matching the FM-1 WL82 profile.
    args = address.to_bytes(4,'big') + b'\0\1'
    response = bytearray(16)
    residue = dev.execute(uboot.cmd_prepare_cdb(JL_UBOOT.Cmd.JUMP_TO_MEMORY,args),None,response)
    if residue != 0 or len(response) != 16 or response[:2] != b'\xfb\x08':
        raise IOError('Invalid or incomplete RAM-loader jump response; execution state uncertain; '
                      'do not automatically retry or reset')


def read_flash_exact(dev: SCSIDev, protocol: JL_LoaderV2, address: int, length: int) -> bytes:
    if length <= 0 or length > FLASH_READ_CHUNK:
        raise ValueError(f"Invalid WL82 read chunk length: {length}")
    cdb_args = address.to_bytes(4, "big") + length.to_bytes(2, "big")
    cdb = protocol.cmd_prepare_cdb(JL_LoaderV2.Cmd.READ_FLASH, cdb_args)
    buffer = bytearray(length)
    residue = dev.execute(cdb, None, buffer)
    if residue != 0:
        raise IOError(f"Short flash read at 0x{address:x}: {length - residue}/{length} bytes")
    return bytes(buffer)


def read_flash_range(dev: SCSIDev, protocol: JL_LoaderV2, address: int, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        size = min(FLASH_READ_CHUNK, length - len(result))
        result.extend(read_flash_exact(dev, protocol, address + len(result), size))
    return bytes(result)


def detect_stock_version(prefix: bytes) -> tuple[str, int]:
    matches: list[tuple[str, int]] = []
    if prefix[V14_MARKER_OFFSET : V14_MARKER_OFFSET + 8] == b"FM-1_014":
        matches.append(("V14", V14_BOOT_APP_LENGTH))
    if prefix[V15_MARKER_OFFSET : V15_MARKER_OFFSET + 8] == b"FM-1_015":
        matches.append(("V15", V15_BOOT_APP_LENGTH))
    if len(matches) != 1:
        raise RuntimeError(
            "Could not uniquely identify the stock FM-1 version in its known app marker locations; "
            "stopping without reading beyond the 0x54000-byte identification prefix."
        )
    return matches[0]


def write_new_file(path: Path, data: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(data)
    return hashlib.sha256(data).hexdigest()


def read_full_boot_app(dev: SCSIDev, protocol: JL_LoaderV2, output_path: Path) -> None:
    partial_path = output_path.with_suffix(output_path.suffix + ".partial")
    if output_path.exists() or partial_path.exists():
        raise FileExistsError(f"Refusing to overwrite an existing output or partial file: {output_path}")

    prefix = read_flash_range(dev, protocol, 0, BOOT_APP_PREFIX_LENGTH)
    version, total_length = detect_stock_version(prefix)
    print(f"Detected stock FM-1 {version}; reading bootloader + app allocation ({total_length:#x} bytes).")
    print("VM/user-state, BTIF/USR data, and the reserved identity/config sector are excluded.")

    digest = hashlib.sha256()
    try:
        with partial_path.open("xb") as output:
            address = 0
            while address < total_length:
                size = min(FLASH_READ_CHUNK, total_length - address)
                data = read_flash_exact(dev, protocol, address, size)
                output.write(data)
                digest.update(data)
                address += size
    except Exception:
        print(f"Partial local output retained for diagnosis: {partial_path}", file=sys.stderr)
        raise

    actual_length = partial_path.stat().st_size
    if actual_length != total_length:
        raise IOError(f"Read length mismatch: expected {total_length:#x}, got {actual_length:#x}")

    partial_path.rename(output_path)
    print(f"Saved {actual_length} bytes to {output_path}")
    print(f"SHA-256: {digest.hexdigest()}")


def read_raw_backup(dev: SCSIDev, protocol: JL_LoaderV2, output_path: Path) -> None:
    """Preserve raw flash privately; confirm every byte with a second read."""
    partial = output_path.with_suffix(output_path.suffix + ".partial")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    with partial.open("xb") as stream:
        for address in range(0, FM1_FLASH_SIZE, FLASH_READ_CHUNK):
            data = read_flash_exact(dev, protocol, address, FLASH_READ_CHUNK)
            stream.write(data)
            digest.update(data)
            if (address + FLASH_READ_CHUNK) % 0x10000 == 0:
                print(f"Read {address + FLASH_READ_CHUNK}/{FM1_FLASH_SIZE} bytes", flush=True)
        stream.flush()
        os.fsync(stream.fileno())

    verify_digest = hashlib.sha256()
    with partial.open("rb") as stream:
        for address in range(0, FM1_FLASH_SIZE, FLASH_READ_CHUNK):
            actual = read_flash_exact(dev, protocol, address, FLASH_READ_CHUNK)
            expected = stream.read(FLASH_READ_CHUNK)
            if actual != expected:
                # Preserve the differing response privately without another USB
                # operation. Never print configuration bytes or decoded keys.
                mismatch = output_path.with_suffix('.mismatch.bin')
                write_new_file(mismatch, actual)
                with output_path.with_suffix('.mismatch.json').open('x', encoding='utf-8') as detail:
                    json.dump({'chunk_address': hex(address), 'chunk_bytes': len(actual),
                               'changed_offsets': [hex(address+i) for i, (a,b) in
                                                   enumerate(zip(expected,actual)) if a != b],
                               'second_pass_chunk': str(mismatch),
                               'cause': 'not established'}, detail, indent=2)
                raise IOError(f"Second read differs at flash address 0x{address:x}; partial and mismatch retained")
            verify_digest.update(actual)
            if (address + FLASH_READ_CHUNK) % 0x10000 == 0:
                print(f"Verified {address + FLASH_READ_CHUNK}/{FM1_FLASH_SIZE} bytes", flush=True)
    if digest.digest() != verify_digest.digest() or partial.stat().st_size != FM1_FLASH_SIZE:
        raise IOError("Backup verification failed; partial retained")
    partial.rename(output_path)
    manifest = {
        "kind": "private raw FM-1 flash backup",
        "bytes": FM1_FLASH_SIZE,
        "sha256": digest.hexdigest(),
        "verification": "second complete read matched every byte",
        "flash_id": "0x856014",
        "includes_device_configuration": True,
        "chip_key_command_queried": False,
        "flash_program_or_erase_commands_sent": False,
        "restore_tested": False,
    }
    with output_path.with_suffix(".json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
    print(f"Verified raw backup saved: {output_path}")
    print(f"SHA-256: {digest.hexdigest()}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="FM-1/WL82 flash readback adapter (loader status-register side effects)")
    parser.add_argument("--device", help=r"Windows SCSI disk path, e.g. \\.\PHYSICALDRIVE3")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--probe-output", type=Path, help="Read only the first 256 flash bytes to a new file")
    mode.add_argument("--full-output", type=Path, help="Read the versioned bootloader + app allocation")
    mode.add_argument("--raw-output", type=Path, help="Private 1 MiB raw backup, verified with a second read")
    parser.add_argument("--reuse-loader", action="store_true", help="Use the already running RAM loader; do not upload it again")
    parser.add_argument("--reference-probe", type=Path, help="Require the first 256 bytes to match a previous successful probe")
    parser.add_argument("--plan", action="store_true", help="Print the FM-1/WL82 read plan without opening USB")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.plan:
        print("Chip: WL82 / AC791N (FM-1 AC7911B8)")
        print("RAM loader: local jl-uboot-tool WL82 blob at 0x1c02000; 24,064 bytes")
        print("Cold upload: exact original blob/profile only; 47 complete RAM writes required before jump")
        print("UBOOT1.00 text does not distinguish cold ROM from an already running helper")
        print("Probe: READ_FLASH 0x00000000..0x000000ff")
        print("Full mode: identify V14/V15, then read only bootloader + app allocation")
        print("Raw mode: private 1 MiB backup with two complete matching reads")
        print("Reuse mode: existing RAM loader, with no upload or jump")
        print("Not queried: chip key. Not called: flash write, erase, or reset commands.")
        print(LOADER_SIDE_EFFECT_WARNING)
        return 0

    if not args.device:
        raise SystemExit("--device is required unless --plan is used")
    if not args.probe_output and not args.full_output and not args.raw_output:
        raise SystemExit("Choose --probe-output, --full-output, or --raw-output")

    selected_output = args.probe_output or args.full_output or args.raw_output
    assert selected_output is not None
    if selected_output.exists():
        raise SystemExit(f"Refusing to overwrite existing output: {selected_output}")
    if args.full_output or args.raw_output:
        partial = selected_output.with_suffix(selected_output.suffix + ".partial")
        if partial.exists():
            raise SystemExit(f"Refusing to overwrite existing partial output: {partial}")
    if args.raw_output and selected_output.with_suffix(".json").exists():
        raise SystemExit("Refusing to overwrite existing backup manifest")
    if args.raw_output and not args.reference_probe:
        raise SystemExit("Raw backup requires --reference-probe from the successful device probe")
    reference = args.reference_probe.read_bytes() if args.reference_probe else None
    if reference is not None and len(reference) != 256:
        raise SystemExit("Reference probe must be exactly 256 bytes")

    chip, loader_spec, loader_path = load_wl82_profile()
    print(LOADER_SIDE_EFFECT_WARNING, flush=True)
    dev = None
    try:
        # Raw Windows disk access needs elevation. Fail immediately rather than
        # silently retrying forever as the upstream device picker does.
        dev = SCSIDev(args.device)
        vendor, product, revision = read_inquiry(dev)
        if vendor.lower() != "wl82" or product != "UBOOT1.00":
            raise RuntimeError(
                f"Unexpected target {vendor!r}/{product!r}/{revision!r}; expected WL82/UBOOT1.00."
            )
        print(f"Target confirmed: {vendor} {product} {revision}")

        if args.reuse_loader:
            print("Reusing the running RAM loader; no loader upload or jump will be sent.", flush=True)
        else:
            upload_wl82_ram_loader(dev, chip, loader_spec, loader_path)
        protocol = JL_LoaderV2(dev)
        online = protocol.online_device()
        if online["type"] != SPI_NOR_DEVICE_TYPE:
            raise RuntimeError(
                f"WL82 loader reports device type 0x{online['type']:02x}, not SPI NOR; stopping."
            )
        print(f"SPI NOR online (ID 0x{online['id']:06x}); chip key was not queried.")
        if args.raw_output and online["id"] != FM1_FLASH_ID:
            raise RuntimeError("Flash ID changed from the successful probe; stopping")
        if reference is not None:
            if read_flash_range(dev, protocol, 0, 256) != reference:
                raise RuntimeError("Current flash header differs from the saved probe; stopping")
            print("Current flash header matches the saved probe.", flush=True)

        if args.raw_output:
            read_raw_backup(dev, protocol, args.raw_output)
        elif args.probe_output:
            probe = read_flash_range(dev, protocol, 0, 256)
            digest = write_new_file(args.probe_output, probe)
            print(f"Read-only probe saved: {args.probe_output} (256 bytes)")
            print(f"SHA-256: {digest}")
        else:
            assert args.full_output is not None
            read_full_boot_app(dev, protocol, args.full_output)
        return 0
    finally:
        if dev is not None and getattr(dev, "is_open", False):
            dev.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        raise SystemExit(f"Read stopped: {type(error).__name__}: {error}") from error
