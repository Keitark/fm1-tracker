"""Private, immutable firmware packages for the remote app/ROM picker."""
import base64
import hashlib
import json
from pathlib import Path
import re

SIZE = 0x100000
APP_START = 0x4120
APP_END = APP_START + 584956
PROFILES = ('nes', 'doom', 'mdx', 'buddha', 'protracker')
TITLES = dict(nes='NES', doom='Doom', mdx='MDX Karaoke', buddha='Buddha Machine', protracker='ProTracker')
DESCRIPTIONS = dict(nes='Play a packaged NES ROM.', doom='Explore a packaged Doom build.',
                    mdx='Play a packaged MDX music bank.', buddha='Loop and play a packaged chant.',
                    protracker='Edit and play four-channel MOD songs.')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate_bundle(bundle):
    if type(bundle) is not dict or set(bundle) != {'id', 'profile', 'title', 'description', 'variant', 'request'}:
        raise ValueError('Unexpected catalog fields')
    if not isinstance(bundle['id'], str) or not re.fullmatch('[a-z0-9][a-z0-9_-]{0,63}', bundle['id']):
        raise ValueError('Invalid catalog id')
    if bundle['profile'] not in PROFILES:
        raise ValueError('Unsupported app profile')
    for key, limit in (('title', 80), ('description', 500), ('variant', 120)):
        if not isinstance(bundle[key], str) or not 1 <= len(bundle[key]) <= limit:
            raise ValueError('Invalid catalog ' + key)
    req = bundle['request']
    if type(req) is not dict or set(req) != {'op', 'image', 'sha256', 'baseline_sha256'} or req['op'] != 'plan':
        raise ValueError('Catalog requires an offline validated plan request')
    if not isinstance(req['image'], str) or len(req['image']) != 1398104:
        raise ValueError('Catalog image must be a packaged 1 MiB firmware')
    data = base64.b64decode(req['image'], validate=True)
    if len(data) != SIZE or sha(data) != req['sha256']:
        raise ValueError('Catalog image size/hash mismatch')
    if not re.fullmatch('[0-9a-f]{64}', req['baseline_sha256'] or ''):
        raise ValueError('Invalid catalog baseline SHA256')
    return data


def rebase(bundle, baseline):
    """Change only the baseline hash; never graft a package onto another device."""
    image = validate_bundle(bundle)
    if len(baseline) != SIZE:
        raise ValueError('Verified baseline is not 1 MiB')
    for start, end in ((0, 0x4000), (0x4040, APP_START), (APP_END, SIZE)):
        if image[start:end] != baseline[start:end]:
            raise ValueError('Package boot/config/reserved bytes differ from this device')
    return dict(bundle['request'], baseline_sha256=sha(baseline))


class Catalog:
    def __init__(self, folder):
        self.folder = Path(folder).resolve()
        self.folder.mkdir(parents=True, exist_ok=True)

    def get(self, ident):
        if not isinstance(ident, str) or not re.fullmatch('[a-z0-9][a-z0-9_-]{0,63}', ident):
            raise ValueError('Invalid catalog id')
        path = self.folder / (ident + '.json')
        if not path.resolve().is_relative_to(self.folder) or not path.is_file():
            raise ValueError('Catalog package unavailable')
        if path.stat().st_size > 1500000:
            raise ValueError('Catalog package exceeds size limit')
        bundle = json.loads(path.read_text(encoding='utf-8-sig'))
        validate_bundle(bundle)
        if bundle['id'] != ident:
            raise ValueError('Catalog package id mismatch')
        return bundle

    def install(self, bundle):
        validate_bundle(bundle)
        target = self.folder / (bundle['id'] + '.json')
        if target.exists():
            if self.get(bundle['id']) != bundle:
                raise ValueError('Catalog ids are immutable; give the new variant a new id')
        else:
            with target.open('x', encoding='utf-8') as stream:
                json.dump(bundle, stream)
        return {key: bundle[key] for key in ('id', 'profile', 'title', 'description', 'variant')} | {
            'sha256': bundle['request']['sha256'], 'device_io': False}

    def listing(self, baseline=None):
        entries = []
        for profile in PROFILES:
            variants = []
            for path in sorted(self.folder.glob('*.json')):
                try:
                    bundle = self.get(path.stem)
                    if bundle['profile'] != profile:
                        continue
                    ready, reason = False, 'Start a protected laptop session to write'
                    if baseline is not None:
                        try:
                            rebase(bundle, baseline)
                            ready, reason = True, 'Package matches this device'
                        except ValueError as error:
                            reason = str(error)
                    variants.append({key: bundle[key] for key in ('id', 'title', 'description', 'variant')} | {
                        'sha256': bundle['request']['sha256'], 'ready': ready, 'reason': reason})
                except (ValueError, OSError, KeyError, TypeError):
                    # Invalid packages never become writeable cards.
                    continue
            entries.append(dict(profile=profile, title=TITLES[profile], description=DESCRIPTIONS[profile],
                                variants=variants, available=bool(variants)))
        return {'apps': entries}
