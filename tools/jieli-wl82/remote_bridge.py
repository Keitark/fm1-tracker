"""Authenticated, loopback-only FM-1 bridge with durable asynchronous jobs.

Use Tailscale Serve HTTPS for the browser store, or an SSH tunnel over Tailscale
for command-line access.  This process never accepts a device path, shell
command, or arbitrary filesystem path over HTTP.
The Windows USB implementation is imported only by ``main`` so this module can
be tested without loading hardware support.
"""

from __future__ import annotations

import argparse
import base64
import binascii
from collections import Counter
from datetime import datetime, timezone
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import socket
import threading
from typing import Any
from urllib.parse import urlsplit
import uuid
import secrets
import time
import webbrowser

from remote_catalog import _plain_path, MAX_SET_BYTES


FIRMWARE_SIZE = 0x100000
MAX_BODY = 1_500_000
IMPORT_TTL = 300
DEFAULT_PORT = 9770
READ_TIMEOUT = 10
ID_PATTERN = re.compile(r"[0-9a-f]{32}\Z")
SHA_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
CATALOG_PATTERN = re.compile(r"[a-z0-9][a-z0-9_-]{0,63}\Z")
IMAGE_OPERATIONS = frozenset({"plan", "flash", "retry_flash", "recover_flash"})
NO_ARGUMENT_OPERATIONS = frozenset(
    {"serial_status", "observe", "enter_uboot", "reset", "environment", "official_updater"}
)
OPERATIONS = IMAGE_OPERATIONS | NO_ARGUMENT_OPERATIONS | {"read_firmware", "switch_app", "plan_app"}
OFFLINE_OPERATIONS = frozenset({"plan", "environment", "plan_app"})
TERMINAL_STATUSES = frozenset({"succeeded", "failed", "unknown"})


class BridgeError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _json_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
        allow_nan=False,
    ).encode("utf-8")


def _atomic_json(path: Path, value: Any) -> None:
    encoded = _json_bytes(value)
    temp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        descriptor = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
    finally:
        try:
            temp.unlink()
        except FileNotFoundError:
            pass


def _without_images(value: Any) -> Any:
    """Never put the submitted firmware payload in public job metadata."""
    if isinstance(value, dict):
        return {
            key: _without_images(item)
            for key, item in value.items() if key != "image"
        }
    if isinstance(value, list):
        return [_without_images(item) for item in value]
    return value


def _copy_json(value: Any) -> Any:
    return json.loads(_json_bytes(value))


def _valid_id(value: Any) -> bool:
    return isinstance(value, str) and ID_PATTERN.fullmatch(value) is not None


def normalize_request(body: Any) -> dict[str, Any]:
    if not isinstance(body, dict):
        raise BridgeError(400, "request must be a JSON object")
    if not _valid_id(body.get("id")):
        raise BridgeError(400, "id must be 32 lowercase hexadecimal characters")
    operation = body.get("operation")
    if not isinstance(operation, str) or operation not in OPERATIONS:
        raise BridgeError(400, "unsupported operation")
    required = {"id", "operation"}
    if operation in IMAGE_OPERATIONS:
        required.add("request")
    elif operation == "read_firmware":
        required.add("loader_state")
    elif operation == "switch_app":
        required.update({"catalog_id", "entry_method"})
    elif operation == "plan_app":
        required.add("catalog_id")
    if set(body) != required:
        raise BridgeError(400, "unexpected or missing request fields")
    if operation in IMAGE_OPERATIONS:
        request = body["request"]
        if not isinstance(request, dict) or set(request) != {
            "op", "image", "sha256", "baseline_sha256"
        }:
            raise BridgeError(400, "image request has unexpected or missing fields")
        if request["op"] != "plan":
            raise BridgeError(400, "embedded request op must be plan")
        for key in ("sha256", "baseline_sha256"):
            value = request[key]
            if not isinstance(value, str) or not SHA_PATTERN.fullmatch(value):
                raise BridgeError(400, key + " must be 64 lowercase hexadecimal characters")
        payload = request["image"]
        if not isinstance(payload, str):
            raise BridgeError(400, "image must be base64 text")
        try:
            decoded = base64.b64decode(payload, validate=True)
        except (ValueError, binascii.Error):
            raise BridgeError(400, "image is not valid base64") from None
        if len(decoded) != FIRMWARE_SIZE:
            raise BridgeError(400, "image must decode to exactly 1 MiB")
        if base64.b64encode(decoded).decode("ascii") != payload:
            raise BridgeError(400, "image must use canonical base64 encoding")
        if hashlib.sha256(decoded).hexdigest() != request["sha256"]:
            raise BridgeError(400, "image SHA-256 does not match")
    elif operation == "read_firmware":
        if body["loader_state"] not in ("cold", "reuse"):
            raise BridgeError(400, "loader_state must be cold or reuse")
    elif operation in {"switch_app", "plan_app"}:
        catalog_id = body["catalog_id"]
        if not isinstance(catalog_id, str) or not CATALOG_PATTERN.fullmatch(catalog_id):
            raise BridgeError(400, "catalog_id must be a lowercase catalog slug of at most 64 characters")
        if operation == "switch_app" and body["entry_method"] not in ("serial", "already_uboot"):
            raise BridgeError(400, "entry_method must be serial or already_uboot")
    return _copy_json(body)


class _StateLock:
    """One OS-held lock per state directory, including across processes."""

    def __init__(self, path: Path):
        self.stream = path.open("a+b")
        self.locked = False
        try:
            self.stream.seek(0, os.SEEK_END)
            if self.stream.tell() == 0:
                self.stream.write(b"\0")
                self.stream.flush()
            self.stream.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.locked = True
            self.stream.seek(1)
            self.stream.truncate()
            self.stream.write(("\n" + str(os.getpid()) + "\n").encode("ascii"))
            self.stream.flush()
        except OSError:
            self.close()
            raise BridgeError(409, "another bridge process owns this state directory") from None

    def close(self) -> None:
        if self.stream.closed:
            return
        try:
            if self.locked:
                self.stream.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(self.stream.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.stream.fileno(), fcntl.LOCK_UN)
        finally:
            self.stream.close()


class JobManager:
    """Serializes operations and records uncertainty before allowing more I/O."""

    def __init__(self, backend: Any, state_dir: Path):
        self.backend = backend
        self.state_dir = Path(state_dir).resolve()
        self.state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        self._state_lock = _StateLock(self.state_dir / "bridge.lock")
        self._mutex = threading.RLock()
        self._jobs: dict[str, dict[str, Any]] = {}
        self._active: str | None = None
        self._worker: threading.Thread | None = None
        self._closed = False
        self._storage_fault = False
        self._import_tokens = {}
        try:
            self.jobs_dir = self.state_dir / "jobs"
            self.jobs_dir.mkdir(mode=0o700, exist_ok=True)
            self._load_jobs()
            self.library_launches_dir = _plain_path(self.state_dir / 'library-launches')
            self.library_launches_dir.mkdir(mode=0o700, exist_ok=True)
        except BaseException:
            self._state_lock.close()
            raise

    def _load_jobs(self) -> None:
        for path in sorted(self.jobs_dir.iterdir()):
            if not path.is_dir():
                continue
            if not _valid_id(path.name):
                raise BridgeError(500, "invalid job directory in bridge state")
            metadata_path = path / "job.json"
            if not metadata_path.exists():
                # A crash before the queued metadata was committed cannot have
                # started device I/O. Keep the private incomplete request.
                continue
            try:
                job = json.loads(metadata_path.read_text(encoding="utf-8"))
                if (not isinstance(job, dict) or job.get("id") != path.name
                        or job.get("operation") not in OPERATIONS
                        or job.get("status") not in TERMINAL_STATUSES | {"queued", "running"}
                        or not SHA_PATTERN.fullmatch(job.get("request_digest", ""))):
                    raise ValueError("invalid metadata")
                _json_bytes(job)
            except (OSError, ValueError, TypeError):
                raise BridgeError(500, "invalid persisted job metadata") from None
            if job["status"] in {"queued", "running"}:
                job["status"] = (
                    "failed" if job["operation"] in OFFLINE_OPERATIONS else "unknown"
                )
                job["updated"] = _utc_now()
                job["error"] = "bridge restarted before this job completed; no automatic retry"
                job["result"] = None
                _atomic_json(metadata_path, job)
            self._jobs[path.name] = job

    def _blocked_jobs(self) -> list[str]:
        return [
            key for key, job in self._jobs.items()
            if job["status"] == "unknown" or (
                job["operation"] == "official_updater" and job["status"] == "succeeded"
            )
        ]

    def _public(self, job: dict[str, Any]) -> dict[str, Any]:
        return _copy_json(_without_images(job))

    def _save(self, job: dict[str, Any]) -> None:
        _atomic_json(self.jobs_dir / job["id"] / "job.json", _without_images(job))

    def submit(self, body: Any) -> dict[str, Any]:
        request = normalize_request(body)
        job_id = request["id"]
        operation = request["operation"]
        digest = hashlib.sha256(_json_bytes(request)).hexdigest()
        with self._mutex:
            if self._closed:
                raise BridgeError(503, "bridge is shutting down")
            existing = self._jobs.get(job_id)
            if existing is not None:
                if not hmac.compare_digest(existing["request_digest"], digest):
                    raise BridgeError(409, "job id already belongs to a different request")
                return self._public(existing)
            if self._storage_fault:
                raise BridgeError(503, "bridge state storage failed; service restart is required")
            if self._active is not None:
                raise BridgeError(409, "another operation is active")
            if self._blocked_jobs() and operation not in OFFLINE_OPERATIONS:
                raise BridgeError(409, "an unknown job outcome blocks device operations")
            job_dir = self.jobs_dir / job_id
            job_dir.mkdir(mode=0o700, exist_ok=True)
            job = {
                "id": job_id,
                "operation": operation,
                "status": "queued",
                "request_digest": digest,
                "created": _utc_now(),
                "updated": _utc_now(),
                "result": None,
                "error": None,
            }
            _atomic_json(job_dir / "request.json", request)
            self._save(job)
            self._jobs[job_id] = job
            self._active = job_id
            self._worker = threading.Thread(
                target=self._execute, args=(job_id, request),
                name="fm1-job-" + job_id, daemon=False,
            )
            try:
                self._worker.start()
            except BaseException:
                job["status"] = "failed"
                job["error"] = "operation worker could not start"
                job["updated"] = _utc_now()
                self._save(job)
                self._active = None
                raise
            # Return a committed queued snapshot; the worker cannot take our
            # mutex until this snapshot has been copied.
            return self._public(job)

    def _execute(self, job_id: str, request: dict[str, Any]) -> None:
        with self._mutex:
            job = self._jobs[job_id]
            job["status"] = "running"
            job["updated"] = _utc_now()
            try:
                self._save(job)
            except Exception:
                job["status"] = "failed"
                job["error"] = "could not persist running state; device operation did not start"
                self._storage_fault = True
                self._active = None
                return
        result: Any = None
        error: str | None = None
        try:
            operation = request["operation"]
            if operation in IMAGE_OPERATIONS:
                result = self.backend.execute(operation, request=request["request"])
            elif operation == "read_firmware":
                result = self.backend.execute(operation, loader_state=request["loader_state"])
            elif operation == "switch_app":
                result = self.backend.execute(
                    operation, catalog_id=request["catalog_id"],
                    entry_method=request["entry_method"],
                )
            elif operation == "plan_app":
                result = self.backend.execute(operation, catalog_id=request["catalog_id"])
            else:
                result = self.backend.execute(operation)
            if not isinstance(result, dict) or type(result.get("ok")) is not bool:
                raise ValueError("backend did not return a definite operation outcome")
            result = _without_images(result)
            _json_bytes(result)
            status = "succeeded" if result["ok"] else "failed"
        except BaseException as exc:
            status = "unknown"
            # Never include exception text: backend failures may contain private
            # request data or paths. The local backend owns detailed diagnostics.
            error = "backend raised " + type(exc).__name__ + "; no automatic retry"
            result = None
        with self._mutex:
            job["status"] = status
            job["updated"] = _utc_now()
            job["result"] = result
            job["error"] = error
            try:
                self._save(job)
            except Exception:
                # The persisted running record becomes unknown after restart.
                self._storage_fault = True
                if request["operation"] not in OFFLINE_OPERATIONS:
                    job["status"] = "unknown"
                job["error"] = "could not persist operation outcome; no automatic retry"
            finally:
                self._active = None

    def get_job(self, job_id: str) -> dict[str, Any]:
        if not _valid_id(job_id):
            raise BridgeError(404, "job not found")
        with self._mutex:
            job = self._jobs.get(job_id)
            if job is None:
                raise BridgeError(404, "job not found")
            return self._public(job)

    def status(self) -> dict[str, Any]:
        with self._mutex:
            unknown = self._blocked_jobs()
            health = {
                "active": self._active,
                "blocked_unknown": bool(unknown),
                "unknown_jobs": unknown,
                "updater_handoff": any(
                    job["operation"] == "official_updater" and job["status"] == "succeeded"
                    for job in self._jobs.values()
                ),
                "jobs": dict(Counter(job["status"] for job in self._jobs.values())),
                "storage_fault": self._storage_fault,
            }
        try:
            device = self.backend.status()
            if not isinstance(device, dict):
                raise TypeError("invalid status")
            return {"engine": health, "device": _copy_json(_without_images(device))}
        except Exception:
            raise BridgeError(503, "local status snapshot is unavailable") from None

    def catalog(self) -> dict[str, Any]:
        with self._mutex:
            try:
                catalog = self.backend.catalog()
                if not isinstance(catalog, dict):
                    raise TypeError("invalid catalog")
                return _copy_json(_without_images(catalog))
            except Exception:
                raise BridgeError(503, "local application catalog is unavailable") from None

    def install_catalog(self, bundle: Any) -> dict[str, Any]:
        if not isinstance(bundle, dict) or set(bundle) != {
            "id", "profile", "title", "description", "variant", "request"
        }:
            raise BridgeError(400, "catalog bundle has unexpected or missing fields")
        if not isinstance(bundle["id"], str) or not CATALOG_PATTERN.fullmatch(bundle["id"]):
            raise BridgeError(400, "catalog id must be a lowercase slug of at most 64 characters")
        for key in ("profile", "title", "description", "variant"):
            if not isinstance(bundle[key], str):
                raise BridgeError(400, "catalog " + key + " must be text")
        normalize_request({"id": "0" * 32, "operation": "plan", "request": bundle["request"]})
        with self._mutex:
            if self._closed:
                raise BridgeError(503, "bridge is shutting down")
            if self._active is not None:
                raise BridgeError(409, "another operation is active")
            try:
                result = self.backend.install_catalog(_copy_json(bundle))
                if not isinstance(result, dict):
                    raise TypeError("invalid catalog installation result")
                return _copy_json(_without_images(result))
            except ValueError:
                raise BridgeError(400, "catalog bundle was rejected by local validation") from None
            except Exception:
                raise BridgeError(503, "local application catalog installation failed") from None

    def _launch_record(self, ident):
        path = _plain_path(self.library_launches_dir / (ident + '.json'))
        if not path.exists():
            raise BridgeError(404, 'local import launch receipt not found')
        try:
            if not path.is_file() or path.stat().st_size > 2048:
                raise ValueError('invalid receipt')
            record = json.loads(path.read_text(encoding='utf-8'))
            if (type(record) is not dict or set(record) != {'id', 'status', 'created', 'expires'}
                    or record['id'] != ident or record['status'] not in {'launching', 'opened', 'failed', 'unknown'}
                    or type(record['created']) not in (int, float)
                    or type(record['expires']) not in (int, float)
                    or record['expires'] != record['created'] + IMPORT_TTL):
                raise ValueError('invalid receipt')
            return record
        except (OSError, ValueError, TypeError):
            raise BridgeError(503, 'local import launch receipt is unavailable') from None

    @staticmethod
    def _public_launch(record):
        remaining = max(0, min(IMPORT_TTL, int(record['expires'] - time.time())))
        result = {'id': record['id'], 'opened': record['status'] == 'opened',
                  'device_io': False, 'expires_in': remaining}
        if record['status'] in {'launching', 'unknown'}:
            result['error'] = 'local_import_launch_unknown'
        elif record['status'] == 'failed':
            result['error'] = 'local_import_launch_failed'
        elif not remaining:
            result['error'] = 'local_import_session_expired'
        return result

    def library_launch(self, ident):
        if not _valid_id(ident):
            raise BridgeError(404, 'local import launch receipt not found')
        with self._mutex:
            return self._public_launch(self._launch_record(ident))

    def open_library(self, body, port):
        if type(body) is not dict or set(body) != {'id'} or not _valid_id(body['id']):
            raise BridgeError(400, 'local import launch requires one 32-character id')
        with self._mutex:
            try:
                return self._public_launch(self._launch_record(body['id']))
            except BridgeError as error:
                if error.status != 404:
                    raise
            if self._closed:
                raise BridgeError(503, 'bridge is shutting down')
            if self._active is not None:
                raise BridgeError(409, 'another operation is active')
            if self._storage_fault:
                raise BridgeError(503, 'bridge state storage failed; service restart is required')
            now = time.time()
            self._import_tokens = {key: value for key, value in self._import_tokens.items()
                                   if value['expires'] > now and not value['used']}
            if len(self._import_tokens) >= 32:
                raise BridgeError(429, 'too many pending local import sessions')
            record = {'id': body['id'], 'status': 'launching', 'created': now, 'expires': now + IMPORT_TTL}
            path = _plain_path(self.library_launches_dir / (body['id'] + '.json'))
            # Persist intent before invoking the browser. A process crash or an
            # ambiguous launcher exception can never cause an automatic reopen.
            _atomic_json(path, record)
            scoped = secrets.token_urlsafe(32)
            self._import_tokens[scoped] = {'expires': record['expires'], 'used': False}
            try:
                opened = webbrowser.open('http://127.0.0.1:' + str(port) + '/library-import#' + scoped,
                                         new=2, autoraise=True)
                record['status'] = 'opened' if opened is True else 'failed'
                if opened is not True:
                    self._import_tokens.pop(scoped, None)
            except Exception:
                record['status'] = 'unknown'
            try:
                _atomic_json(path, record)
            except Exception:
                # The durable launching record remains an uncertain receipt.
                raise BridgeError(503, 'local import launch outcome could not be saved; do not retry with a new id') from None
            return self._public_launch(record)

    def check_import_token(self, token):
        with self._mutex:
            grant = self._import_tokens.get(token)
            if grant is None or grant['used'] or grant['expires'] <= time.time():
                raise BridgeError(401, 'local import session expired or already used')

    def install_library_set(self, body, import_token=None):
        if type(body) is not dict or set(body) != {'name', 'bundles'}:
            raise BridgeError(400, 'local set requires only name and bundles')
        with self._mutex:
            if import_token is not None:
                self.check_import_token(import_token)
            if self._closed:
                raise BridgeError(503, 'bridge is shutting down')
            if self._active is not None:
                raise BridgeError(409, 'another operation is active')
            if self._storage_fault:
                raise BridgeError(503, 'bridge state storage failed; service restart is required')
            try:
                result = self.backend.install_library_set(body['name'], body['bundles'])
                if type(result) is not dict or result.get('device_io') is not False:
                    raise TypeError('invalid local set installation result')
                if import_token is not None:
                    self._import_tokens[import_token]['used'] = True
                return _copy_json(_without_images(result))
            except (ValueError, TypeError, KeyError):
                raise BridgeError(400, 'local set was rejected by validation; no device I/O') from None
            except Exception:
                raise BridgeError(503, 'local set installation failed; inspect the library before retrying') from None

    @staticmethod
    def _artifact(value: Any) -> bytes:
        if not isinstance(value, bytes) or len(value) != FIRMWARE_SIZE:
            raise BridgeError(502, "backend artifact is not exactly 1 MiB")
        return value

    def baseline(self) -> bytes:
        # Hold the submission mutex until the static verified artifact is read.
        with self._mutex:
            if self._active is not None:
                raise BridgeError(409, "an operation is active")
            try:
                return self._artifact(self.backend.baseline())
            except BridgeError:
                raise
            except Exception:
                raise BridgeError(503, "verified baseline is unavailable") from None

    def firmware(self, job_id: str) -> bytes:
        with self._mutex:
            job = self.get_job(job_id)
            if job["operation"] != "read_firmware" or job["status"] != "succeeded":
                raise BridgeError(409, "firmware is available only for a successful read_firmware job")
            result = job["result"]
        try:
            return self._artifact(self.backend.firmware(result))
        except BridgeError:
            raise
        except Exception:
            raise BridgeError(503, "verified firmware artifact is unavailable") from None

    def close(self, wait: bool = True) -> None:
        with self._mutex:
            if self._closed:
                return
            if not wait and self._active is not None:
                raise BridgeError(409, "cannot release service lock while an operation is active")
            self._closed = True
            worker = self._worker
        if worker is not None and worker is not threading.current_thread():
            worker.join()
        self._state_lock.close()


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ValueError("nonfinite JSON number")


def validate_token(token: str) -> str:
    if (not isinstance(token, str) or len(token) < 32
            or any(ord(character) < 33 or ord(character) > 126 for character in token)):
        raise BridgeError(400, "token must contain at least 32 printable ASCII characters without spaces")
    return token


def make_server(manager: JobManager, token: str, port: int = DEFAULT_PORT) -> ThreadingHTTPServer:
    """Create an authenticated server; its bind address is deliberately fixed."""
    token = validate_token(token)
    if type(port) is not int or not 0 <= port <= 65535:
        raise BridgeError(400, "port must be between 0 and 65535")

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "FM1Bridge/1"
        sys_version = ""

        def setup(self) -> None:
            super().setup()
            self.connection.settimeout(READ_TIMEOUT)

        def log_message(self, format: str, *args: Any) -> None:
            # BaseHTTPRequestHandler would include attacker-controlled URLs.
            pass

        def _send_json(self, status: int, value: Any) -> None:
            payload = _json_bytes(value)
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.end_headers()
            self.close_connection = True
            try:
                self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def _send_artifact(self, payload: bytes) -> None:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("X-FM1-SHA256", hashlib.sha256(payload).hexdigest())
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            self.wfile.write(payload)

        def _send_store(self, importer=False) -> None:
            try:
                payload = Path(__file__).with_name("remote_library.html" if importer else "remote_store.html").read_bytes()
            except OSError:
                raise BridgeError(404, "store interface is unavailable") from None
            if importer:
                script = re.search(br'<script>(.*?)</script>', payload, re.S)
                if script is None:
                    raise BridgeError(503, 'local import interface is unavailable')
                script_hash = base64.b64encode(hashlib.sha256(script.group(1)).digest()).decode('ascii')
                policy = ("default-src 'none'; script-src 'sha256-" + script_hash + "'; "
                          "connect-src 'self'; style-src 'unsafe-inline'; frame-ancestors 'none'; "
                          "base-uri 'none'; form-action 'none'; object-src 'none'")
            else:
                policy = ("default-src 'self'; script-src 'self' 'unsafe-inline'; "
                          "style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; "
                          "base-uri 'none'; form-action 'self'")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            if importer:
                self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
            self.send_header('Content-Security-Policy', policy)
            self.end_headers()
            self.close_connection = True
            self.wfile.write(payload)

        def _authenticate(self, import_allowed=False):
            headers = self.headers.get_all("Authorization") or []
            expected = "Bearer " + token
            if len(headers) != 1 or not headers[0].isascii():
                raise BridgeError(401, "authentication required")
            if hmac.compare_digest(headers[0], expected):
                return None
            if import_allowed and headers[0].startswith('Bearer '):
                scoped = headers[0][7:]
                manager.check_import_token(scoped)
                return scoped
            raise BridgeError(401, "authentication required")

        def _same_origin(self):
            origins = self.headers.get_all('Origin') or []
            if origins and origins != ['http://127.0.0.1:' + str(self.server.server_port)]:
                raise BridgeError(403, 'local import requires the loopback origin')
            if self.headers.get('Sec-Fetch-Site') in {'cross-site', 'same-site'}:
                raise BridgeError(403, 'local import requires the loopback origin')

        def _path(self) -> str:
            parsed = urlsplit(self.path)
            if parsed.query or parsed.fragment or parsed.scheme or parsed.netloc:
                raise BridgeError(400, "query strings and absolute URLs are not accepted")
            return parsed.path

        def _dispatch(self, method: str) -> None:
            try:
                if method == "GET" and self.path in {"/", "/library-import"}:
                    if (self.headers.get("Transfer-Encoding") is not None
                            or self.headers.get_all("Content-Length") not in (None, ["0"])):
                        raise BridgeError(400, "GET requests must not contain a body")
                    self._send_store(importer=self.path == '/library-import')
                    return
                path = self._path()
                is_import = method == 'POST' and path == '/v1/library/sets'
                if path in {'/v1/library/sets', '/v1/library/open'}:
                    self._same_origin()
                import_token = self._authenticate(import_allowed=is_import)
                if method == "GET":
                    if self.headers.get("Transfer-Encoding") is not None:
                        raise BridgeError(400, "Transfer-Encoding is not supported")
                    lengths = self.headers.get_all("Content-Length") or []
                    if lengths and (len(lengths) != 1 or lengths[0] != "0"):
                        raise BridgeError(400, "GET requests must not contain a body")
                    if path == "/v1/status":
                        data = manager.status()
                    elif path == "/v1/catalog":
                        data = manager.catalog()
                    elif path == "/v1/baseline":
                        self._send_artifact(manager.baseline())
                        return
                    elif re.fullmatch(r'/v1/library/open/[0-9a-f]{32}', path):
                        data = manager.library_launch(path.rsplit('/', 1)[1])
                    else:
                        match = re.fullmatch(r"/v1/jobs/([0-9a-f]{32})(/firmware)?", path)
                        if match is None:
                            raise BridgeError(404, "endpoint not found")
                        if match.group(2):
                            self._send_artifact(manager.firmware(match.group(1)))
                            return
                        data = manager.get_job(match.group(1))
                    self._send_json(200, {"ok": True, "data": data})
                    return
                if method != "POST":
                    raise BridgeError(405, "method not supported")
                if path not in {"/v1/jobs", "/v1/catalog", '/v1/library/open', '/v1/library/sets'}:
                    raise BridgeError(404, "endpoint not found")
                if self.headers.get("Transfer-Encoding") is not None:
                    raise BridgeError(400, "Transfer-Encoding is not supported")
                lengths = self.headers.get_all("Content-Length") or []
                if len(lengths) != 1 or re.fullmatch(r"[0-9]+", lengths[0]) is None:
                    raise BridgeError(411, "a single valid Content-Length is required")
                canonical_length = lengths[0].lstrip("0") or "0"
                body_limit = MAX_SET_BYTES if path == '/v1/library/sets' else MAX_BODY
                if len(canonical_length) > len(str(body_limit)):
                    raise BridgeError(413, "request body is too large")
                length = int(canonical_length)
                if length > body_limit:
                    raise BridgeError(413, "request body is too large")
                if length == 0:
                    raise BridgeError(400, "JSON request body is required")
                content_types = self.headers.get_all("Content-Type") or []
                if content_types and (
                    len(content_types) != 1 or
                    content_types[0].split(";", 1)[0].strip().lower() != "application/json"
                ):
                    raise BridgeError(415, "Content-Type must be application/json")
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise BridgeError(400, "incomplete request body")
                try:
                    body = json.loads(
                        raw.decode("utf-8"), object_pairs_hook=_unique_object,
                        parse_constant=_reject_constant,
                    )
                except (UnicodeError, ValueError, RecursionError):
                    raise BridgeError(400, "invalid JSON body") from None
                if path == "/v1/jobs":
                    self._send_json(202, {"ok": True, "data": manager.submit(body)})
                elif path == '/v1/library/open':
                    self._send_json(200, {'ok': True, 'data': manager.open_library(body, self.server.server_port)})
                elif path == '/v1/library/sets':
                    self._send_json(201, {'ok': True, 'data': manager.install_library_set(body, import_token)})
                else:
                    self._send_json(201, {"ok": True, "data": manager.install_catalog(body)})
            except BridgeError as exc:
                self._send_json(exc.status, {"ok": False, "error": exc.message})
            except (socket.timeout, TimeoutError):
                self._send_json(408, {"ok": False, "error": "request timed out"})
            except (BrokenPipeError, ConnectionResetError):
                # A client disconnect does not cancel the committed worker.
                return
            except Exception:
                self._send_json(500, {"ok": False, "error": "bridge request failed"})

        def do_GET(self) -> None:
            self._dispatch("GET")

        def do_POST(self) -> None:
            self._dispatch("POST")

        def do_PUT(self) -> None:
            self._dispatch("PUT")

        def do_DELETE(self) -> None:
            self._dispatch("DELETE")

        def do_OPTIONS(self) -> None:
            self._dispatch("OPTIONS")

        def do_HEAD(self) -> None:
            self._dispatch("HEAD")

        def handle_expect_100(self) -> bool:
            # Authenticate before accepting a potentially large body. Framing
            # validation happens in the normal dispatch without sending 100.
            try:
                self._authenticate()
            except BridgeError as exc:
                self._send_json(exc.status, {"ok": False, "error": exc.message})
                return False
            self._send_json(417, {"ok": False, "error": "Expect is not supported"})
            return False

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--session", type=Path)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--token-file", type=Path, required=True)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--official-updater", type=Path)
    parser.add_argument("--official-updater-sha256")
    args = parser.parse_args(argv)
    manager = None
    server = None
    try:
        token = validate_token(args.token_file.read_text(encoding="utf-8").strip())
        from remote_backend import WindowsBackend
        backend = WindowsBackend(
            repo=args.repo, session=args.session, catalog_dir=args.state_dir / "catalog",
            official_updater=args.official_updater,
            official_updater_sha256=args.official_updater_sha256,
        )
        manager = JobManager(backend, args.state_dir)
        server = make_server(manager, token, args.port)
        print("FM-1 bridge listening on 127.0.0.1:" + str(server.server_port), flush=True)
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        print("Stopping bridge; waiting for any active operation to finish.", flush=True)
    except (BridgeError, OSError, ValueError) as exc:
        # Tokens and backend exception details are intentionally absent.
        message = exc.message if isinstance(exc, BridgeError) else type(exc).__name__
        print("FM-1 bridge could not start: " + message, flush=True)
        return 1
    finally:
        if server is not None:
            server.server_close()
        if manager is not None:
            manager.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
