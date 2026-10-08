"""PC-side client for the FM-1 laptop bridge. Never automatically resubmits jobs."""
import argparse
import hashlib
import ipaddress
import json
import re
from pathlib import Path
import sys
import time
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import build_opener, ProxyHandler, HTTPRedirectHandler, Request
import uuid


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('Redirect refused; bearer credentials stay at the configured bridge')


class Client:
    def __init__(self, url, token):
        parsed = urlsplit(url)
        if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ('', '/'):
            raise ValueError('Use a bridge origin, e.g. http://laptop.tailnet.ts.net:9771')
        host = parsed.hostname
        try:
            address = ipaddress.ip_address(host)
            valid = address.is_loopback or address in ipaddress.ip_network('100.64.0.0/10') or address in ipaddress.ip_network('fd7a:115c:a1e0::/48')
        except ValueError:
            valid = host == 'localhost' or host.endswith('.ts.net')
        if not valid:
            raise ValueError('Use a loopback or Tailscale address')
        if len(token) < 32 or not token.isascii() or any(ord(ch) < 33 or ord(ch) > 126 for ch in token):
            raise ValueError('Token must be at least 32 printable ASCII characters')
        self.url, self.token = url.rstrip('/'), token
        # Never send the device token through a configured corporate/public proxy.
        self.opener = build_opener(ProxyHandler({}), NoRedirect())

    def call(self, path, body=None, binary=False):
        data = json.dumps(body).encode('utf-8') if body is not None else None
        req = Request(self.url + path, data=data, headers={'Authorization': 'Bearer ' + self.token,
                      'Content-Type': 'application/json'}, method='POST' if data is not None else 'GET')
        try:
            with self.opener.open(req, timeout=25) as response:
                raw = response.read(1500001)
                if binary:
                    expected = response.headers.get('X-FM1-SHA256')
                    if len(raw) != 0x100000 or hashlib.sha256(raw).hexdigest() != expected:
                        raise ValueError('Firmware download size/hash mismatch')
                    return raw
                result = json.loads(raw)
        except HTTPError as error:
            raw = error.read(8192)
            try:
                message = json.loads(raw).get('error', 'Bridge rejected request')
            except ValueError:
                message = 'Bridge rejected request'
            raise ValueError(f'HTTP {error.code}: {message}') from None
        if result.get('ok') is not True:
            raise ValueError(result.get('error', 'Bridge rejected request'))
        return result['data']

    def wait(self, ident, seconds):
        deadline = time.monotonic() + seconds
        while True:
            job = self.call('/v1/jobs/' + ident)
            if job['status'] in ('succeeded', 'failed', 'unknown'):
                return job
            if time.monotonic() >= deadline:
                raise TimeoutError('Job still running; poll its saved id. Do not resubmit a flash.')
            time.sleep(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--url', required=True)
    ap.add_argument('--token-file', type=Path, required=True)
    sub = ap.add_subparsers(dest='command', required=True)
    sub.add_parser('status')
    sub.add_parser('catalog')
    upload = sub.add_parser('upload'); upload.add_argument('bundle', type=Path)
    job = sub.add_parser('job')
    job.add_argument('operation', choices=('plan', 'flash', 'retry_flash', 'recover_flash', 'reset', 'observe',
                                         'enter_uboot', 'read_firmware', 'serial_status', 'environment',
                                         'plan_app', 'switch_app', 'official_updater'))
    job.add_argument('--request-file', type=Path)
    job.add_argument('--loader-state', choices=('cold', 'reuse'))
    job.add_argument('--catalog-id')
    job.add_argument('--entry-method', choices=('serial', 'already_uboot'))
    job.add_argument('--id', default=None)
    job.add_argument('--wait', action='store_true')
    job.add_argument('--seconds', type=int, default=1300)
    get = sub.add_parser('get'); get.add_argument('id')
    wait = sub.add_parser('wait'); wait.add_argument('id'); wait.add_argument('--seconds', type=int, default=1300)
    download = sub.add_parser('download'); download.add_argument('--id'); download.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if getattr(args, 'id', None) is not None and not re.fullmatch('[0-9a-f]{32}', args.id):
        ap.error('Use the exact 32-character lowercase saved job id')
    if hasattr(args, 'seconds') and not 1 <= args.seconds <= 3600:
        ap.error('Wait duration must be 1 to 3600 seconds')
    client = Client(args.url, args.token_file.read_text(encoding='utf-8-sig').strip())
    if args.command in ('status', 'catalog'):
        result = client.call('/v1/' + args.command)
    elif args.command == 'upload':
        if args.bundle.stat().st_size > 1500000:
            ap.error('Bundle exceeds bridge size limit')
        result = client.call('/v1/catalog', json.loads(args.bundle.read_text(encoding='utf-8-sig')))
    elif args.command == 'job':
        ident = args.id or uuid.uuid4().hex
        body = {'id': ident, 'operation': args.operation}
        for name in ('loader_state', 'catalog_id', 'entry_method'):
            value = getattr(args, name)
            if value is not None:
                body[name] = value
        if args.request_file:
            body['request'] = json.loads(args.request_file.read_text(encoding='utf-8-sig'))
        print('Job id: ' + ident + ' (save this if the connection drops)', file=sys.stderr, flush=True)
        result = client.call('/v1/jobs', body)
        if args.wait:
            result = client.wait(ident, args.seconds)
    elif args.command in ('get', 'wait'):
        result = client.wait(args.id, args.seconds) if args.command == 'wait' else client.call('/v1/jobs/' + args.id)
    else:
        if args.out.exists():
            ap.error('Refusing to overwrite an existing private firmware backup')
        path = '/v1/jobs/' + args.id + '/firmware' if args.id else '/v1/baseline'
        data = client.call(path, binary=True)
        with args.out.open('xb') as stream:
            stream.write(data)
        result = {'file': str(args.out.resolve()), 'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
    print(json.dumps(result, indent=2))
    if isinstance(result, dict) and result.get('status') in ('failed', 'unknown'):
        return 1
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
