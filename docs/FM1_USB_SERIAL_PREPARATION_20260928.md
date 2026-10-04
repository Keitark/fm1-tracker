# USB serial preparation

Status: Windows receive-only terminal prepared; **CDC firmware is not yet
integrated, linked, packaged, flashed or hardware-tested**. Serial ports were
enumerated, but none was opened; no device commands or driver changes were made.
Existing NES/stock artifacts
and the pinned SDK checkout are unchanged.

The user has reopened USB serial work. This supersedes the earlier cancellation
for this preparation only; it does not authorize flashing an unreviewed image.

## Use the existing Jieli CDC implementation

The pinned AC79 SDK, commit
`e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d`, contains these Git objects, even
though the sparse working checkout does not currently materialize them:

- `apps/common/usb/device/cdc.c`, `cdc.h`, `cdc_defs.h`;
- `apps/common/usb/device/usb_device.c`;
- `apps/common/usb/device/descriptor.c`, `user_setup.c`;
- `apps/common/usb/usb_config.c`, `usb_config.h` and endpoint-buffer support.

They were inspected with `git show HEAD:<path>`. No new USB stack or downloader
has been written. CDC is gated by `TCFG_USB_SLAVE_CDC_ENABLE`; device mode is
selected through `usb_device_mode(usb_id, CDC_CLASS)` after prerequisite setup.
Do not assume that this single call supplies the complete clock/PHY/attach flow.

`cdc_register` creates RTOS mutexes and allocates endpoint buffers. Reads and
writes use `os_mutex_pend`; USB device setup registers an interrupt handler.
Therefore this existing implementation is an **RTOS-stage diagnostic**, not
an application-entry/before-cache console. Serial output would establish that
execution reached functioning USB services; absence of a COM port would not
localize the original failure. Buffered early markers can be reported only if
USB eventually starts in the same boot, not after a reset into UBOOT.

## Required review before a firmware build can be called test-ready

1. Use a separate USB-only diagnostic profile with no NES worker, LCD, audio,
   scanner, mass-storage class or host-mode application. Retain boot integrity
   checks and the established no-persistent-config-repair policy.
2. Trace the selected controller, clock/PHY/attach sequence, IRQ ownership,
   DMA buffer allocation and linker placement. Do not assume USB0 or USB1 from
   the UBOOT product string alone, and do not initialize both speculatively.
3. Correct the CDC line-coding handler in an isolated build copy. The reviewed
   source allocates `u8 read_ep[8]`, assigns `len = ctrl_req->wLength`, and calls
   `usb_read_ep0(..., len)` **before** clamping the length. Reject malformed
   SET_LINE_CODING requests before reception/copy and validate the actual
   received length. Add regression tests; changing the later memcpy clamp
   alone would not fix the earlier unbounded read.
4. Review CDC-only descriptors and class requests, endpoint validity, reset,
   disconnect and reconnect behavior. The stock generic `user_setup.c` has
   non-CDC private update behavior and its endpoint whitelist has no CDC branch;
   do not bring that whole filter into a logging-only console unexamined.
5. Avoid blind `printf` redirection. Use a dedicated task and bounded queue;
   no mutex waits or blocking USB transmissions from exception/IRQ/early-boot
   context. The vendor write routine's loop limit does not establish that its
   lower-level bulk write or mutex wait is bounded. Audit those dependencies.
6. Start with an identification banner and heartbeat, repeating the same-boot
   stage summary so opening the terminal late does not lose all evidence.
   No flash, reset, key-query or arbitrary-memory commands in the first console.
7. Verify the linked image, descriptors and tests before packaging a separately
   identified diagnostic. Preserve the working stock rollback. No flash has
   been performed or scheduled by this document.

These are outstanding integration gates, not claims of completed firmware.
In particular, merely enabling the SDK CDC macro is not sufficient.

## Windows-side terminal

`tools/jieli-wl82/usb-serial-terminal.ps1` lists serial ports by default and
requires explicit port selection before opening one. It receives ASCII logs,
sets 115200/8N1 plus DTR/RTS, sends no application bytes, captures to a new file,
and closes on timeout or Ctrl+C. Opening any serial port does issue line-control
requests; verify the port identity first. It is a log monitor, not a command shell.

```powershell
powershell -NoProfile -File .\tools\jieli-wl82\usb-serial-terminal.ps1 -List
# Only after CDC firmware has been integrated, installed and enumerated:
powershell -NoProfile -File .\tools\jieli-wl82\usb-serial-terminal.ps1 -Port COM7 -Seconds 60
```

`COM7` is an example, not a detected FM-1 port. Logs default to
`F:\dev\fm1\tmp\usb-serial\`; an explicit `-LogPath` must not already exist.
UBOOT `WL82 UBOOT1.00` is a different USB personality and cannot be opened as
this CDC port. No driver replacement or UAC is required to prepare the script.

Windows supports CDC ACM through its inbox USB serial driver; actual binding
depends on correct descriptors and successful enumeration, not on our script.
See [Microsoft's USB serial driver documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids).

Validation scope: PowerShell syntax and port-listing path only. No CDC firmware
enumeration, terminal-open handshake or physical log reception is qualified yet.
