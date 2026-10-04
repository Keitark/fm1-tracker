"""Fail-closed binding to the manually reviewed experimental machine code.

This is not a silicon test, general disassembler, or upload authorization.
When the compiler or sources change, review the new instructions before
changing these pins. No device access is implemented here.
"""
import hashlib

EXTENSION_SHA = 'a680e2c412a7d9e4346c45e5e76821d4cca380e56a73bd60a3cc930c32bccee1'
HOOK = bytes.fromhex('c2 ea 72 de')
DISPLACED = bytes.fromhex('31 e1 7b 0f')


def verify(extension, hook, original, unresolved=''):
    if unresolved.strip():
        raise ValueError('Unresolved symbols in extension')
    if len(original) != 24064 or hashlib.sha256(original).hexdigest() != (
            'db8676b9cca4e2c0dfee062715cdb41528a2a8ccc827520ba8c194e4a2e0356e'):
        raise ValueError('Unreviewed decoded original loader')
    if original[0x2318:0x231c] != DISPLACED:
        raise ValueError('Displaced instruction changed')
    if hook != HOOK:
        raise ValueError('Unreviewed hook instruction')
    if len(extension) != 212 or hashlib.sha256(extension).hexdigest() != EXTENSION_SHA:
        raise ValueError('Unreviewed extension machine code')
    if extension[14:18] != DISPLACED:
        raise ValueError('Passthrough does not reproduce displaced instruction')
    # Targets are from manual review of this exact binary. Hashes bind that
    # review; this deliberately does not claim a general instruction decoder.
    return {
        'exact_reviewed_machine_code': True,
        'hook': {'from': '0x01c04318', 'to': '0x01c60000', 'bytes': 4},
        'status_call': {'from': '0x01c60006', 'to': '0x01c6003a'},
        'response_return': {'from': '0x01c6000a', 'to': '0x01c04b32'},
        'passthrough_return': {'from': '0x01c60012', 'to': '0x01c0431c'},
        'hardware_qualified': False,
    }
