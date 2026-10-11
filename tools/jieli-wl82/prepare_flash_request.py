"""Unprivileged build/package validation for the flash session. No device I/O."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def prepare(folder, baseline_sha256, baseline=None):
    folder = folder.resolve()
    manifest = json.loads((folder / 'build-manifest.json').read_text())
    mdx = manifest.get('status') == 'LINKED_MDX_KARAOKE_UNTESTED'
    mod = manifest.get('status') == 'LINKED_USB_DIAGNOSTIC_UNTESTED' and manifest.get('mod') is True
    usb_audio = manifest.get('usb_audio', False)
    hs_probe = manifest.get('usb_hs_phy_probe', False)
    uvc_screen = manifest.get('usb_uvc_screen', False)
    if type(uvc_screen) is not bool or (uvc_screen and (not mod or hs_probe or
            not manifest.get('lcd_async') or manifest.get('usb_controller_candidate') != 0)):
        raise ValueError('Screen UVC requires the explicit USB0 asynchronous MOD profile')
    if type(hs_probe) is not bool:
        raise ValueError('USB1 PHY probe profile must be explicit boolean')
    if type(usb_audio) is not bool or (usb_audio and not mdx):
        raise ValueError('Composite audio requires an explicit MDX build profile')
    if mod or hs_probe:
        if hs_probe and (mod or mdx or manifest.get('status') != 'LINKED_USB_DIAGNOSTIC_UNTESTED' or
                manifest.get('peripheral_tests') or manifest.get('nes_smb1') or manifest.get('mdx') or usb_audio or
                manifest.get('usb_controller_candidate') != 0 or manifest.get('probe_controller_candidate') != 1):
            raise ValueError('USB1 PHY probe requires its audited USB0-only diagnostic')
        if mod and (not manifest.get('peripheral_tests') or manifest.get('nes_smb1') or manifest.get('mdx')):
            raise ValueError('Only the audited standalone MOD editor build is accepted')
        if type(baseline) is not bytes or len(baseline) != 0x100000 or \
                hashlib.sha256(baseline).hexdigest() != baseline_sha256:
            raise ValueError('MOD/probe requires the exact verified current baseline')
    elif not mdx and (manifest.get('status') != 'LINKED_USB_DIAGNOSTIC_UNTESTED' or
            not manifest.get('nes_smb1') or not manifest.get('peripheral_tests')):
        raise ValueError('Only audited NES+CDC, standalone MDX/MOD, or bounded PHY probe builds are accepted')
    for field in ('source_sha256', 'archive_sha256', 'vendor_overlay_sources'):
        for name, digest in manifest[field].items():
            if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest:
                raise ValueError('Stale source/archive/overlay: ' + name)
    prefix = 'fm1-mdx' if mdx else 'fm1-usb-diag'
    app = (folder / (prefix + '.app.bin')).read_bytes()
    elf = (folder / (prefix + '.elf')).read_bytes()
    if hashlib.sha256(app).hexdigest() != manifest['application_sha256']:
        raise ValueError('Stale app')
    sys.path.insert(0, str(ROOT / 'firmware/nes'))
    import build_stock_image as pack
    image, report = pack.build_candidate(pack.V14_BACKUP.read_bytes(), elf, app,
        require_peripherals=False, usb_only=True, usb_peripheral_tests=not hs_probe,
        usb_nes=not (mdx or mod or hs_probe), usb_mdx=mdx, usb_mod=mod,
        usb_audio=usb_audio, usb_hs_phy_probe=hs_probe, usb_uvc_screen=uvc_screen,
        rom='rom50' if (mod or hs_probe) else 'smb1')
    if mod or hs_probe:
        for start,end in ((0,0x4000),(0x4040,0x4120),(0x4120+584956,0x100000)):
            if image[start:end] != baseline[start:end]:
                raise ValueError('MOD/probe candidate differs from current baseline outside application scope')
    return {'op': 'plan', 'baseline_sha256': baseline_sha256,
            'sha256': report['candidate_sha256'], 'image': base64.b64encode(image).decode('ascii')}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    state = json.loads((args.session / 'state.json').read_text(encoding='utf-8-sig'))
    base_path = (args.session / state['baseline_file']).resolve()
    if not base_path.is_relative_to(args.session.resolve()):
        raise ValueError('Baseline outside protected session')
    baseline = base_path.read_bytes()
    request = prepare(args.build, state['baseline_sha256'], baseline)
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(request, stream)
    print(json.dumps({'request': str(args.out.resolve()), 'sha256': request['sha256'], 'device_io': False}))
