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
    factory = manifest.get('factory_diag') is True
    usb_audio = manifest.get('usb_audio', False)
    usb_video = manifest.get('usb_video', False)
    if type(usb_audio) is not bool or (usb_audio and not (mdx or mod or factory)):
        raise ValueError('Composite audio requires an explicit MDX, MOD or factory build profile')
    if mod and (type(manifest.get('mod_usb_audio',False)) is not bool or
                type(manifest.get('mod_uvc',False)) is not bool or
                usb_audio!=manifest.get('mod_usb_audio',False) or
                usb_video!=manifest.get('mod_uvc',False)):
        raise ValueError('Tracker USB classes must match the explicit current MOD profile flags')
    if type(usb_video) is not bool or (usb_video and not
            (mod or (factory and manifest.get('forge_project_schema')==3))):
        raise ValueError('USB video requires an explicit MOD profile or Forge project schema3')
    sys.path.insert(0, str(ROOT / 'firmware/nes'))
    import build_stock_image as pack
    if factory:
        if type(manifest.get('factory_user_module',False)) is not bool:
            raise ValueError('Invalid factory module profile flag')
        if manifest.get('status')!='LINKED_USB_DIAGNOSTIC_UNTESTED' or manifest.get('profile')!='FM1_FORGE_FACTORY_DIAG_V1' or type(manifest.get('factory_schema')) is not int or manifest.get('factory_schema')!=1 or manifest.get('peripheral_tests') is not True or manifest.get('asset_free') is not True or manifest.get('external_media_required') is not False or any(manifest.get(n) for n in ('nes_smb1','mod','mdx')):
            raise ValueError('Only the explicit asset-free factory diagnostic schema is accepted')
        if 'forge_hal_access' in manifest and manifest['forge_hal_access'] is None:
            raise ValueError('Invalid factory HAL access selections')
        pack.validate_factory_project(manifest.get('forge_project_schema',1),manifest.get('forge_hal_access'))
        if type(baseline) is not bytes or len(baseline)!=0x100000 or hashlib.sha256(baseline).hexdigest()!=baseline_sha256:
            raise ValueError('Factory diagnostic requires the exact verified current baseline')
    elif mod:
        if not manifest.get('peripheral_tests') or manifest.get('nes_smb1') or manifest.get('mdx'):
            raise ValueError('Only the audited standalone MOD editor build is accepted')
        if type(baseline) is not bytes or len(baseline) != 0x100000 or \
                hashlib.sha256(baseline).hexdigest() != baseline_sha256:
            raise ValueError('MOD editor requires the exact verified current baseline')
    elif not mdx and (manifest.get('status') != 'LINKED_USB_DIAGNOSTIC_UNTESTED' or
            not manifest.get('nes_smb1') or not manifest.get('peripheral_tests')):
        raise ValueError('Only audited NES+CDC, standalone MDX, or standalone MOD builds are accepted')
    for field in ('source_sha256', 'archive_sha256', 'vendor_overlay_sources'):
        for name, digest in manifest[field].items():
            if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest:
                raise ValueError('Stale source/archive/overlay: ' + name)
    prefix = 'fm1-mdx' if mdx else 'fm1-usb-diag'
    app = (folder / (prefix + '.app.bin')).read_bytes()
    elf = (folder / (prefix + '.elf')).read_bytes()
    if hashlib.sha256(app).hexdigest() != manifest['application_sha256']:
        raise ValueError('Stale app')
    image, report = pack.build_candidate(pack.V14_BACKUP.read_bytes(), elf, app,
        require_peripherals=False, usb_only=True, usb_peripheral_tests=True,
        usb_nes=not (mdx or mod or factory), usb_mdx=mdx, usb_mod=mod,factory_diag=factory,factory_user_module=manifest.get('factory_user_module',False),
        usb_audio=usb_audio,usb_video=usb_video, factory_clock=manifest.get('forge_clock','sdk-default'),factory_hal_access=manifest.get('forge_hal_access') if factory else None,factory_project_schema=manifest.get('forge_project_schema',1) if factory else 1, rom='rom50' if mod or factory else 'smb1')
    if mod or factory:
        for start,end in ((0,0x4000),(0x4040,0x4120),(0x4120+584956,0x100000)):
            if image[start:end] != baseline[start:end]:
                raise ValueError('Candidate differs from current baseline outside application scope')
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
