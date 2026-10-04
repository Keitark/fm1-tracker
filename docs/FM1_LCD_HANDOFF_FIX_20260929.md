# LCD pending-frame handoff correction

Integrated the tested guard in `firmware/nes/boot/display_test.c`: after
service(), check pending under the allocation lock before reserving BUILD.
If pending remains, count a busy presentation skip and return without altering
buffer ownership. No waits, clock changes, or volume/key/audio changes.

Maintained display tests now inject the actual completion ISR at the service
unlock, across all 30 strip boundaries and wanted/unwanted presentations.
They verify the pending frame survives, the next frame resumes building, and
three frames complete with correct pixels and no ownership errors. Original
reproduction and isolated candidate evidence remain in
`tmp/lcd-race-test-20260929/RESULTS.md`.

## Build and validation

Build: `firmware/usb-diag/build/nes-smb1-adc-volume-lcd-guard`.
Application: 214928 bytes, SHA256
`d01690af3e3afd99f6290f40c03a62400c17b8894cd4518f7d9224c080e9a6a7`.
Packaged full image SHA256:
`1d9a50016a3c18fa6a8c04dc3aeb066e0be2cb7b48df409616aac09f39b7a2f2`.

Passed: 27 rebuilt USB/display tests, 29 rebuilt NES host tests, 33 linked
image checks, and 29 broker/writer tests (118 total). The broker's current-build
fixture was updated to this build after its old build correctly failed the
source-hash freshness check. Package plan preserves boot/config/reserved bytes;
41 changed sectors, directory sector last, against the verified ADC-volume image.

## Deployment blocked before write

Old persistent session `FM1FlashSession-3edce9be5079421c8d72bca4118957af`
remains alive and latched after the previous firmware observation failure.
The existing launcher opened a new session successfully, using the unchanged
verified baseline c954d377... and the same existing Python/writer:
`C:\Program Files\FM1FlashSession-8e9845bda6fe44deb9a4af6bffd21a9e`.

The cycle stopped before entering UBOOT or writing:
`ValueError: Exactly one FM-1 CDC port required`.
Read-only Windows enumeration confirmed no FM-1 serial port (VID3654/PID5155)
and no WL82 UBOOT disk. Only COM1 and Bluetooth serial ports were present.
No flash or reset occurred. New session is latched at enter_uboot; do not send
retry_flash (there was no failed flash), edit its state, or claim deployment.

Next: user reconnects FM-1, preferably directly in UBOOT. Re-enumerate identity
before requesting another writer operation. Existing verified flash baseline
is unchanged. After successful write/readback/reset, perform multi-minute
physical keys/volume test with serial frame/error capture. Hardware acceptance
of this correction remains pending.

## Reconnection and successful deployment

After the user reconnected, Windows detected FM-1 CDC on COM10 (3654:5155).
Existing launcher started session
`C:\Program Files\FM1FlashSession-dbb696e8a2864b628149e93a00d593aa`.
Serial STOP/UBOOT identified the same WL82 target; the existing jluboottool.py
writer programmed 41 changed sectors, directory last. Exactly one full final
readback matched `1d9a50016a3c18fa6a8c04dc3aeb066e0be2cb7b48df409616aac09f39b7a2f2`.
Write run: `2bdb95b913f24731ba733291126e3f7b`.

Reset was sent once. The known UTF-8 decoding exception occurred afterward;
it was not retried. COM10 HELLO and advancing frames resolved reset status.
Initial observation plus ten further bounded captures succeeded. Last report:
uptime 183590ms, frame10976, LCD completed5759, key scans182826, ADC samples91412.
No recorded LCD errors, NES faults, ADC/input errors, audio underruns or
rebuffers in those post-flash captures. ADC changes (at least122..971) and
gain changes were captured, confirming live volume response after deployment.
This is a roughly three-minute telemetry check, not indefinite stability or
physical display/audio/button acceptance. User confirmation remains separate.

Deployment receipts, final readback, reset/UBOOT logs and eleven post-flash
serial captures are saved in `private-backups/nes-lcd-guard-write-20260929/`.
Session is idle, unblocked, reset_pending=false; persistent helper remains
running. Current verified baseline is its `runs/2bdb95b913f24731ba733291126e3f7b/final-1.bin`.

User subsequently confirmed: "ok now stable". This closes the reported freeze
retest for this bench run. Aggregate post-flash telemetry: 31.51 displayed FPS,
60.02 emulated FPS over182480ms; ADC122..971, applied gain0..121. Preserve this
image as the stable rollback baseline before further changes.
