# FM1 Forge local app sets — 2026-10-12

The Forge library accepts named sets of one to eight exported catalog bundle
JSON files. The native panel opens a local companion with file/folder selection,
reviews the package metadata, and registers the set on the laptop. Firmware
bytes and local file paths never enter the hosted relay. Set membership and
package metadata are shown in the native library; transfer approval remains
separate.

The backend validates image size and digest, exact bundle fields, duplicate
IDs and immutable ID collisions before publishing. Receipts persist locally,
and identical repeated imports return the original receipt. Ordinary failures
roll back newly registered packages. Process interruption while publishing can
leave valid standalone catalog variants without the final set receipt.

The local importer receives a five-minute, single-success, import-only token.
The browser clears the URL fragment immediately; the token cannot authorize
device jobs, status, baseline reads or general catalog modification. Launch
receipts persist before opening the browser; ambiguous recovery uses GET with
the original ID and never opens another browser automatically. Service
restarts invalidate pending in-memory importer grants.

Routes: authenticated `POST /v1/library/open`, authenticated
`GET /v1/library/open/<id>`, local `GET /library-import`, and import-scoped
`POST /v1/library/sets`. Existing single-bundle catalog endpoints remain
compatible. No hardware operation is part of import.

Validation: 16 focused Python importer tests and 5 local importer JavaScript
tests passed on final sources. The broader remote contract suite passed 129
tests. The main repository's complete historical tools suite ran 402 tests
with five skips and seven stale-source fixture errors in existing CPU1 guard,
display, pre-OS, screen-first and stock-power deployment tests. These depend on
older local firmware builds; no firmware build, manifest or deployment gate
was changed to hide those failures.

The integration repository owns the native library UI, metadata-only outbound
relay, Site deployment and connection-level checks. Its user instructions are
`docs/LOCAL_APP_SETS.md`. Tracked work: fm1-tracker issue 15 and
fm1-marketplace-plugin issue 10, alongside the existing Forge draft PRs.
