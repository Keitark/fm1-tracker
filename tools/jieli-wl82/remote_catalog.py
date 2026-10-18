"""Private, immutable firmware packages for the remote app/ROM picker."""
import base64
import hashlib
import json
from pathlib import Path
import re
import os
import shutil
import stat
from datetime import datetime, timezone
import uuid

MAX_BUNDLE_BYTES = 1_500_000
MAX_SET_BYTES = 12 * 1024 * 1024
MAX_SET_MEMBERS = 8


def _plain_path(path):
    """Catalog storage never follows a link or Windows reparse point."""
    path = Path(path).absolute()
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Catalog storage must not contain links or reparse points')
    return path


def _encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True,
                      allow_nan=False).encode('utf-8')


def _metadata(bundle):
    return {key: bundle[key] for key in ('id', 'profile', 'title', 'description', 'variant')} | {
        'sha256': bundle['request']['sha256']}

SIZE = 0x100000
APP_START = 0x4120
APP_END = APP_START + 584956
PROFILES = ('nes', 'doom', 'mdx', 'buddha', 'protracker','diagnostics')
TITLES = dict(nes='NES', doom='Doom', mdx='MDX Karaoke', buddha='Buddha Machine', protracker='ProTracker',diagnostics='FM1 FW Factory Diagnostics')
DESCRIPTIONS = dict(nes='Play a packaged NES ROM.', doom='Explore a packaged Doom build.',
                    mdx='Play a packaged MDX music bank.', buddha='Loop and play a packaged chant.',
                    protracker='Edit and play four-channel MOD songs.',diagnostics='Asset-free LCD, buttons, encoders, volume and key-tone diagnostics with CDC/optional UAC1 recovery.')


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
        self.folder = _plain_path(folder).resolve()
        self.folder.mkdir(parents=True, exist_ok=True)

    def get(self, ident):
        if not isinstance(ident, str) or not re.fullmatch('[a-z0-9][a-z0-9_-]{0,63}', ident):
            raise ValueError('Invalid catalog id')
        path = _plain_path(self.folder / (ident + '.json'))
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
        target = _plain_path(self.folder / (bundle['id'] + '.json'))
        if target.exists():
            if self.get(bundle['id']) != bundle:
                raise ValueError('Catalog ids are immutable; give the new variant a new id')
        else:
            with target.open('x', encoding='utf-8') as stream:
                json.dump(bundle, stream)
        return {key: bundle[key] for key in ('id', 'profile', 'title', 'description', 'variant')} | {
            'sha256': bundle['request']['sha256'], 'device_io': False}

    def install_set(self, name, bundles):
        """Validate the complete set before publishing any immutable package."""
        if not isinstance(name, str) or not 1 <= len(name.strip()) <= 80 or name != name.strip():
            raise ValueError('Local set name must contain 1 to 80 characters')
        if any(ord(char) < 32 or ord(char) == 127 for char in name):
            raise ValueError('Local set name contains control characters')
        if type(bundles) is not list or not 1 <= len(bundles) <= MAX_SET_MEMBERS:
            raise ValueError('Local set must contain 1 to 8 catalog bundles')
        if len(_encoded({'name': name, 'bundles': bundles})) > MAX_SET_BYTES:
            raise ValueError('Local set exceeds size limit')
        ids, missing, members = set(), [], []
        for bundle in bundles:
            validate_bundle(bundle)
            encoded = _encoded(bundle)
            if len(encoded) > MAX_BUNDLE_BYTES:
                raise ValueError('Catalog package exceeds size limit')
            ident = bundle['id']
            if ident in ids:
                raise ValueError('Local set contains duplicate catalog ids')
            ids.add(ident)
            for key in ('title', 'description', 'variant'):
                if any(ord(char) < 32 or ord(char) == 127 for char in bundle[key]):
                    raise ValueError('Catalog display metadata contains control characters')
            target = _plain_path(self.folder / (ident + '.json'))
            if target.exists():
                if self.get(ident) != bundle:
                    raise ValueError('Catalog ids are immutable; give the new variant a new id')
            else:
                missing.append((target, encoded))
            members.append(_metadata(bundle))
        members.sort(key=lambda item: item['id'])
        ident = sha(_encoded({'name': name, 'members': members}))[:32]
        sets_dir = _plain_path(self.folder / 'sets')
        if sets_dir.exists() and not sets_dir.is_dir():
            raise ValueError('Local set receipt storage is unavailable')
        receipt_path = _plain_path(sets_dir / (ident + '.json'))
        if receipt_path.exists():
            receipt = self._read_set(receipt_path)
            if receipt['name'] != name or receipt['members'] != members:
                raise ValueError('Local set receipt collision')
            return receipt
        receipt = {'id': ident, 'name': name, 'members': members,
                   'imported': datetime.now(timezone.utc).isoformat(timespec='milliseconds'),
                   'device_io': False}
        # Staged files are fsynced before publication. Exclusive hard-link
        # publication never overwrites an existing immutable catalog id.
        stage = _plain_path(self.folder / ('.import-' + uuid.uuid4().hex))
        published = []
        stage.mkdir(mode=0o700)
        try:
            for target, encoded in missing:
                staged = stage / target.name
                with staged.open('xb') as stream:
                    stream.write(encoded)
                    stream.flush()
                    os.fsync(stream.fileno())
            with (stage / 'receipt.json').open('xb') as stream:
                stream.write(_encoded(receipt))
                stream.flush()
                os.fsync(stream.fileno())
            sets_dir.mkdir(mode=0o700, exist_ok=True)
            for target, _ in missing:
                _plain_path(target)
                os.link(stage / target.name, target)
                published.append(target)
            _plain_path(receipt_path)
            os.link(stage / 'receipt.json', receipt_path)
            return receipt
        except BaseException:
            for target in reversed(published):
                target.unlink()
            raise
        finally:
            shutil.rmtree(stage)

    def _read_set(self, path):
        path = _plain_path(path)
        if not path.is_file() or path.stat().st_size > 100000:
            raise ValueError('Invalid local set receipt')
        receipt = json.loads(path.read_text(encoding='utf-8'))
        if type(receipt) is not dict or set(receipt) != {'id', 'name', 'members', 'imported', 'device_io'}:
            raise ValueError('Invalid local set receipt')
        members = receipt['members']
        if (receipt['id'] != path.stem or not re.fullmatch('[0-9a-f]{32}', receipt['id'])
                or type(members) is not list or not 1 <= len(members) <= MAX_SET_MEMBERS
                or receipt['device_io'] is not False
                or not isinstance(receipt['name'], str) or not 1 <= len(receipt['name']) <= 80
                or not isinstance(receipt['imported'], str)):
            raise ValueError('Invalid local set receipt')
        for member in members:
            if type(member) is not dict or set(member) != {'id', 'profile', 'title', 'description', 'variant', 'sha256'}:
                raise ValueError('Invalid local set member')
            if member != _metadata(self.get(member['id'])):
                raise ValueError('Local set member no longer matches catalog')
        expected = sha(_encoded({'name': receipt['name'], 'members': members}))[:32]
        if expected != receipt['id']:
            raise ValueError('Local set receipt digest mismatch')
        return receipt

    def sets(self):
        receipts = []
        folder = _plain_path(self.folder / 'sets')
        if folder.exists():
            for path in folder.glob('*.json'):
                try:
                    receipts.append(self._read_set(path))
                except (ValueError, OSError, KeyError, TypeError):
                    continue
        return sorted(receipts, key=lambda item: (item['imported'], item['id']), reverse=True)

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
        return {'apps': entries, 'sets': self.sets()}
