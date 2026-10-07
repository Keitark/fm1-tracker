"""Export one private, offline-validated firmware catalog bundle. No device I/O.

The normal audited producer prepares the image against an idle verified
session baseline. Bundles contain private device/configuration bytes and must
stay in an ignored local directory, such as tmp/remote-catalog.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import flash_session_worker as worker
import prepare_flash_request
import remote_catalog

PROFILES = ('nes', 'doom', 'mdx', 'buddha', 'protracker')


def require(test, message):
    if not test:
        raise ValueError(message)


def session_baseline(session):
    session = Path(session).resolve()
    state = json.loads((session / 'state.json').read_text(encoding='utf-8-sig'))
    require(state.get('blocked') is False and not state.get('reset_pending') and
            not state.get('needs_observation') and state.get('operation', 'idle') == 'idle',
            'Catalog export requires an idle verified session baseline')
    filename = state.get('baseline_file')
    require(type(filename) is str and filename, 'Session baseline file missing')
    path = (session / filename).resolve()
    require(path.is_relative_to(session), 'Baseline outside session')
    baseline = path.read_bytes()
    digest = state.get('baseline_sha256')
    require(len(baseline) == worker.SIZE and worker.sha(baseline) == digest, 'Session baseline damaged')
    receipt_name = 'baseline-deployment.json' if path.name == 'baseline.bin' else 'deployment.json'
    receipt = json.loads((path.parent / receipt_name).read_text(encoding='utf-8-sig'))
    require(receipt.get('status') in ('written_and_readback_verified', 'written_and_twice_readback_verified')
            and receipt.get('full_readback_sha256') == digest,
            'Session baseline requires a matching full-readback receipt')
    if receipt['status'] == 'written_and_readback_verified':
        require(receipt.get('full_readback_count') == 1, 'Invalid single-readback baseline receipt')
    return baseline, digest


def check_freshness(manifest, doom=False):
    fields = ('source_sha256',) if doom else ('source_sha256', 'archive_sha256', 'vendor_overlay_sources')
    for field in fields:
        hashes = manifest.get(field)
        require(type(hashes) is dict and hashes, 'Missing audited build hashes: ' + field)
        for filename, digest in hashes.items():
            require(type(filename) is str and type(digest) is str and
                    re.fullmatch(r'[0-9a-f]{64}', digest) is not None, 'Invalid build hash record')
            require(worker.sha(Path(filename).read_bytes()) == digest,
                    'Stale source/archive/overlay: ' + filename)


def check_profile(profile, manifest):
    if profile == 'doom':
        require(manifest.get('status') == 'uboot_app_input_ready_unflashed' and
                manifest.get('uboot_app_input_ready') is True, 'Build is not an audited Doom candidate')
    elif profile in ('protracker', 'buddha'):
        require(manifest.get('mod') is True and not manifest.get('mdx') and not manifest.get('nes_smb1'),
                'Selected build is not the standalone MOD editor profile')
    elif profile == 'mdx':
        require((manifest.get('mdx') is True or manifest.get('status') == 'LINKED_MDX_KARAOKE_UNTESTED')
                and not manifest.get('mod') and not manifest.get('nes_smb1'),
                'Selected build is not the standalone MDX profile')
    else:
        require(manifest.get('nes_smb1') is True and not manifest.get('mdx') and not manifest.get('mod'),
                'Selected build is not the audited NES profile')


def verify_buddha_boot(manifest):
    """Bind the label to the exact generated NAMO.MOD embedded boot array."""
    folder = ROOT / 'firmware/protracker/build/buddha'
    song = (folder / 'NAMO.MOD').read_bytes()
    demo = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    digest = worker.sha(song)
    require(demo.get('sha256') == digest and demo.get('mod_bytes') == len(song),
            'Buddha NAMO.MOD prototype hash/size mismatch')
    require(manifest.get('mod_default_song_sha256') == digest and
            manifest.get('mod_default_song_bytes') == len(song),
            'Buddha requires an audited firmware build embedding NAMO.MOD as its boot song; current tracker build is not Buddha firmware')
    sources = manifest.get('source_sha256', {})
    embedded = [(Path(name), expected) for name, expected in sources.items()
                if Path(name).name == 'embedded_default_mod.c']
    require(len(embedded) == 1, 'Buddha boot song source is absent from audited build closure')
    source, expected = embedded[0]
    raw = source.read_bytes()
    require(worker.sha(raw) == expected, 'Stale Buddha embedded boot source')
    text = raw.decode('ascii')
    match = re.search(r'\bfm1_mod_default_song\[\].*?=\s*\{(.*?)\}\s*;', text, re.DOTALL)
    require(match is not None, 'Buddha embedded boot array missing')
    encoded = match.group(1)
    require(re.sub(r'0x[0-9a-fA-F]{2}|[,\s]', '', encoded) == '', 'Unrecognized Buddha embedded array')
    decoded = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', encoded))
    require(decoded == song, 'Buddha embedded boot array differs from exact NAMO.MOD')


def prepare_doom(build, session):
    # The Doom repository exposes only a CLI main(), not a prepare() function.
    # Discover its owning checkout from the explicitly selected build path.
    repositories = [parent for parent in (build, *build.parents)
                    if (parent / 'tools/prepare_flash_request.py').is_file() and
                    (parent / 'TARGET_CANDIDATE.md').is_file()]
    require(len(repositories) == 1, 'Cannot identify the audited Doom producer checkout from --build')
    repository = repositories[0]
    producer = repository / 'tools/prepare_flash_request.py'
    result = subprocess.run([sys.executable, str(producer), '--fm1-root', str(ROOT),
                             '--session', str(session), '--build', str(build)],
                            cwd=repository, capture_output=True, text=True, timeout=120)
    require(result.returncode == 0, 'Audited Doom request producer refused: ' +
            (result.stderr or result.stdout)[-4000:])
    report = json.loads(result.stdout)
    require(report.get('status') == 'offline_plan_ready' and
            report.get('device_operations_performed') is False, 'Doom producer did not confirm an offline plan')
    path = Path(report['request']).resolve()
    require(path.is_relative_to((repository / 'build/flash-request').resolve()),
            'Doom producer request outside private output directory')
    return json.loads(path.read_text(encoding='utf-8'))


def prepare_local_mdx(build, digest, manifest):
    """Package the current local builder's explicit standalone MDX schema.

    The older shared request helper recognizes the external MDX status string.
    Current local --mdx output uses USB diagnostic filenames/status instead;
    dispatch that declared profile through the unchanged full stock audit.
    """
    require(manifest.get('status') == 'LINKED_USB_DIAGNOSTIC_UNTESTED' and
            manifest.get('profile') == 'MDX_KARAOKE_CANDIDATE' and manifest.get('mdx') is True and
            manifest.get('peripheral_tests') is True and not manifest.get('mod') and
            not manifest.get('nes_smb1') and not manifest.get('usb_audio'),
            'Local MDX package requires the exact audited standalone profile')
    app = (build / 'fm1-usb-diag.app.bin').read_bytes()
    elf = (build / 'fm1-usb-diag.elf').read_bytes()
    require(worker.sha(app) == manifest.get('application_sha256') and
            len(app) == manifest.get('application_bytes'), 'Local MDX app hash/length mismatch')
    sys.path.insert(0, str(ROOT / 'firmware/nes'))
    import build_stock_image as pack
    image, report = pack.build_candidate(pack.V14_BACKUP.read_bytes(), elf, app,
        require_peripherals=False, usb_only=True, usb_peripheral_tests=True,
        usb_nes=False, usb_mdx=True, usb_audio=False, usb_mod=False, rom='rom50')
    return {'op': 'plan', 'baseline_sha256': digest, 'sha256': report['candidate_sha256'],
            'image': base64.b64encode(image).decode('ascii')}


def external_mdx_repository(manifest):
    producers = [Path(name).resolve() for name in manifest.get('source_sha256', {})
                 if Path(name).name == 'build_target.py' and Path(name).parent.name == 'scripts']
    require(len(producers) <= 1, 'Ambiguous external MDX producer')
    return producers[0].parents[1] if producers else None


def audit_external_mdx(repository, build, manifest):
    """Run the exact producer's complete audit with its declared MDX flags."""
    board = repository / 'firmware/nes'
    hashes = manifest['source_sha256']
    for name in ('audit_boot.py', 'audit_power.py', 'audit_pre_os.py', 'audit_usb_packet.py'):
        path = board / name
        require(hashes.get(str(path)) == worker.sha(path.read_bytes()),
                'External MDX audit is not in the fresh producer closure: ' + name)
    # A separate isolated interpreter keeps external audit modules separate
    # from the current FM1 stock-container tool's modules and requirements.
    script = ('import json,sys; from pathlib import Path; '
              'sys.path.insert(0,sys.argv[1]); from audit_boot import audit; '
              'result=audit(Path(sys.argv[2]).read_bytes(),Path(sys.argv[3]).read_bytes(),'
              'usb_only=True,usb_peripheral_tests=True,usb_mdx=True,usb_audio=sys.argv[4]=="1",'
              'require_boot_trace=True,require_board_power=True); print(json.dumps(result))')
    result = subprocess.run([sys.executable, '-I', '-S', '-c', script, str(board),
        str(build / 'fm1-mdx.elf'), str(build / 'fm1-mdx.app.bin'),
        '1' if manifest.get('usb_audio') else '0'], capture_output=True, text=True, timeout=60)
    require(result.returncode == 0, 'Exact external MDX static audit refused: ' +
            (result.stderr or result.stdout)[-4000:])
    report = json.loads(result.stdout)
    require(report.get('static_audit') == 'passed' and report.get('hardware_boot_verified') is False,
            'External MDX audit did not return an unflashed static pass')
    return report


def prepare_external_mdx(build, digest, manifest, repository):
    require(manifest.get('status') == 'LINKED_MDX_KARAOKE_UNTESTED' and
            type(manifest.get('usb_audio', False)) is bool, 'Unrecognized external MDX manifest')
    app = (build / 'fm1-mdx.app.bin').read_bytes()
    require(worker.sha(app) == manifest.get('application_sha256') and
            len(app) == manifest.get('application_bytes'), 'External MDX app hash/length mismatch')
    audit_external_mdx(repository, build, manifest)
    sys.path.insert(0, str(ROOT / 'firmware/nes'))
    import build_stock_image as pack
    stock = pack.V14_BACKUP.read_bytes()
    original = pack.BACKUP.read_bytes()
    require(stock[:pack.AREA] == original[:pack.AREA], 'Stock MDX boot/header/config identity changed')
    pack.decode_boot(original)
    # These are the unchanged container operations inside build_candidate.
    # Static auditing above uses the exact separately reviewed MDX producer;
    # stock_layout/verify_candidate still check every layout, CRC, round trip,
    # boot/config/reserved span and unused application byte.
    image = pack._replace_application(stock, app)
    report = pack.verify_candidate(stock, image, app)
    return {'op': 'plan', 'baseline_sha256': digest, 'sha256': report['candidate_sha256'],
            'image': base64.b64encode(image).decode('ascii')}


def export(profile, build, session, out, identifier, title, description=''):
    require(profile in PROFILES, 'Unknown catalog profile')
    require(type(identifier) is str and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', identifier)
            and len(identifier) <= 64, 'Catalog id must be a lowercase slug up to 64 characters')
    require(type(title) is str and 0 < len(title.strip()) <= 80, 'Catalog title required, up to 80 characters')
    require(type(description) is str, 'Catalog description must be text')
    description = description.strip() or remote_catalog.DESCRIPTIONS[profile]
    require(1 <= len(description) <= 500, 'Catalog description required, up to 500 characters')
    build, session, out = Path(build).resolve(), Path(session).resolve(), Path(out).resolve()
    require(not out.exists(), 'Output exists; catalog bundles are never overwritten')
    # Generated bundles include private raw image bytes. Keep them in a known
    # ignored local tree instead of accidentally adding them to a public repo.
    allowed = (ROOT / 'tmp').resolve()
    require(out.is_relative_to(allowed), 'Private catalog output must stay under this checkout tmp directory')
    baseline, digest = session_baseline(session)
    manifest = json.loads((build / 'build-manifest.json').read_text(encoding='utf-8-sig'))
    check_profile(profile, manifest)
    check_freshness(manifest, doom=profile == 'doom')
    if profile == 'buddha':
        verify_buddha_boot(manifest)
    if profile == 'doom':
        request = prepare_doom(build, session)
    elif profile == 'mdx' and manifest.get('status') == 'LINKED_USB_DIAGNOSTIC_UNTESTED':
        request = prepare_local_mdx(build, digest, manifest)
    elif profile == 'mdx' and external_mdx_repository(manifest) is not None:
        request = prepare_external_mdx(build, digest, manifest, external_mdx_repository(manifest))
    else:
        request = prepare_flash_request.prepare(build, digest, baseline)
    require(request.get('op') == 'plan', 'Audited producer did not return an offline plan request')
    worker.candidate(request, baseline)  # Recheck full image identity/protected scope.
    bundle = {'id': identifier, 'profile': profile, 'title': title.strip(), 'description': description,
              'variant': build.name, 'request': request}
    remote_catalog.validate_bundle(bundle)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open('x', encoding='utf-8') as stream:
        json.dump(bundle, stream)
    return {'file': str(out), 'id': identifier, 'profile': profile, 'variant': build.name,
            'sha256': request['sha256'], 'baseline_sha256': digest, 'device_io': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, choices=PROFILES)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--session', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--id', required=True, dest='identifier')
    parser.add_argument('--title', required=True)
    parser.add_argument('--description', default='')
    args = parser.parse_args()
    print(json.dumps(export(**vars(args))))


if __name__ == '__main__':
    main()
