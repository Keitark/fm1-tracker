"""Client transport tests; no Tailscale peers or physical devices."""
import hashlib
import io
import json
import unittest
from unittest.mock import Mock
from urllib.error import URLError

from remote_client import Client, NoRedirect

TOKEN = 'test-only-token-that-is-at-least-32-characters'


class Response(io.BytesIO):
    def __init__(self, data, headers=None):
        super().__init__(data)
        self.headers = headers or {}


class ClientTests(unittest.TestCase):
    def test_public_credentials_or_query_origin_is_refused(self):
        for url in ('http://example.com', 'http://192.168.1.2', 'http://user:pass@localhost',
                    'http://localhost?token=secret', 'http://localhost/api', 'file:///tmp/token'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                Client(url, TOKEN)

    def test_accepts_loopback_and_tailscale_origins(self):
        for url in ('http://127.0.0.1:9770', 'https://laptop.tail1234.ts.net',
                    'http://100.77.33.76:9771', 'http://[fd7a:115c:a1e0::1]:9771'):
            Client(url, TOKEN)

    def test_redirect_never_reuses_token(self):
        with self.assertRaisesRegex(ValueError, 'Redirect refused'):
            NoRedirect().redirect_request(None, None, 302, '', {}, 'https://example.com')

    def test_job_timeout_never_resubmits(self):
        client = Client('http://127.0.0.1:9770', TOKEN)
        client.opener = Mock()
        client.opener.open.side_effect = URLError('network disconnected')
        with self.assertRaises(URLError):
            client.call('/v1/jobs', {'id': 'a'*32, 'operation': 'reset'})
        self.assertEqual(client.opener.open.call_count, 1)
        request = client.opener.open.call_args.args[0]
        self.assertEqual(request.get_header('Authorization'), 'Bearer '+TOKEN)
        self.assertNotIn(TOKEN.encode(), request.data)
        self.assertNotIn(TOKEN, request.full_url)

    def test_verified_binary_download_and_bad_digest(self):
        data = bytes(1048576)
        client = Client('http://127.0.0.1:9770', TOKEN)
        client.opener = Mock()
        client.opener.open.return_value = Response(data, {'X-FM1-SHA256': hashlib.sha256(data).hexdigest()})
        self.assertEqual(client.call('/v1/baseline', binary=True), data)
        for payload, digest in ((data, '0'*64), (data[:-1], hashlib.sha256(data[:-1]).hexdigest())):
            client.opener.open.return_value = Response(payload, {'X-FM1-SHA256': digest})
            with self.assertRaisesRegex(ValueError, 'size/hash mismatch'):
                client.call('/v1/baseline', binary=True)

    def test_known_failed_job_is_returned_without_retry(self):
        job = {'id': 'a'*32, 'status': 'failed'}
        client = Client('http://127.0.0.1:9770', TOKEN)
        client.opener = Mock()
        client.opener.open.return_value = Response(json.dumps({'ok': True, 'data': job}).encode())
        self.assertEqual(client.wait('a'*32, 1), job)
        self.assertEqual(client.opener.open.call_count, 1)


if __name__ == '__main__':
    unittest.main()
