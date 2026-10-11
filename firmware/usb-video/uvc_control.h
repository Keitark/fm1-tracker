#ifndef FM1_UVC_CONTROL_H
#define FM1_UVC_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#define FM1_UVC_VC_INTERFACE 2u
#define FM1_UVC_VS_INTERFACE 3u
#define FM1_UVC_VIDEO_ENDPOINT 0x84u
#define FM1_UVC_VIDEO_DESCRIPTOR_BYTES 159u
#define FM1_UVC_CONTROL_BYTES 26u
#define FM1_UVC_FRAME_INTERVAL_100NS 2000000u
#define FM1_UVC_PROBE_SELECTOR 1u
#define FM1_UVC_COMMIT_SELECTOR 2u

#define FM1_UVC_SET_CUR 0x01u
#define FM1_UVC_GET_CUR 0x81u
#define FM1_UVC_GET_MIN 0x82u
#define FM1_UVC_GET_MAX 0x83u
#define FM1_UVC_GET_RES 0x84u
#define FM1_UVC_GET_LEN 0x85u
#define FM1_UVC_GET_INFO 0x86u
#define FM1_UVC_GET_DEF 0x87u

enum fm1_uvc_control_result {
    FM1_UVC_CONTROL_STALL = -1,
    FM1_UVC_CONTROL_ACK = 0,
    FM1_UVC_CONTROL_IN = 1,
    FM1_UVC_CONTROL_OUT = 2
};

enum fm1_uvc_control_event {
    FM1_UVC_EVENT_NONE = 0,
    FM1_UVC_EVENT_START = 1,
    FM1_UVC_EVENT_STOP = 2
};

/* Decoded USB SETUP fields. No packed structure casts or native byte order. */
typedef struct {
    uint8_t request_type, request;
    uint16_t value, index, length;
} fm1_uvc_setup;

typedef struct {
    uint32_t token;
    uint16_t length;
    uint8_t event;
    uint8_t data[FM1_UVC_CONTROL_BYTES];
} fm1_uvc_control_reply;

typedef struct {
    uint32_t token;
    uint8_t configured, committed, alt, pending_selector;
    uint8_t probe[FM1_UVC_CONTROL_BYTES];
    uint8_t commit[FM1_UVC_CONTROL_BYTES];
} fm1_uvc_control;

/* Append only this video function to the root-owned configuration descriptor.
 * CDC interfaces 0/1, device descriptors and configuration totals are external.
 * This full-speed function has VC2, VS3 alt0/1, and one ISO IN84 endpoint. No
 * status interrupt endpoint, device qualifier, HS mode, or OS acceptance claim.
 * Class-specific VC total is 40 bytes; class-specific VS total is 77 bytes.
 * Definitions follow Linux include/uapi/linux/usb/video.h and pinned WL82 SDK
 * apps/common/usb/device/uvc.c. Camera controls are absent, so VC requests stall.
 */
extern const uint8_t fm1_uvc_video_descriptor[FM1_UVC_VIDEO_DESCRIPTOR_BYTES];
const uint8_t *fm1_uvc_video_descriptors(size_t *length);
/* Returns 159 on success or zero without writing any bytes on bad capacity. */
size_t fm1_uvc_copy_video_descriptors(uint8_t *destination, size_t capacity);

/* No allocation, rendering, endpoint writes or locking. Caller serializes all
 * control calls with SETUP/OUT/reset handling. SET_CUR OUT data is exactly one
 * complete 26-byte UVC1.0 payload. The adapter must retain reply.token across
 * EP0 OUT and pass it back; a newer SETUP/reset invalidates the old transfer.
 * Apply reply.event START/STOP to the separately owned stream. configure/reset
 * always clear alt/commit; the adapter must also reset/stop that stream.
 */
void fm1_uvc_control_init(fm1_uvc_control *control);
void fm1_uvc_control_reset(fm1_uvc_control *control);
/* Call on every non-video SETUP too (CDC/device/endpoint requests), since any
 * USB SETUP cancels a preceding EP0 OUT stage. This preserves commit/alt state. */
void fm1_uvc_control_cancel_pending(fm1_uvc_control *control);
int fm1_uvc_control_configure(fm1_uvc_control *control, uint8_t configuration);
int fm1_uvc_control_setup(fm1_uvc_control *control, const fm1_uvc_setup *setup,
                          fm1_uvc_control_reply *reply);
int fm1_uvc_control_out(fm1_uvc_control *control, uint32_t token,
                        const uint8_t *payload, size_t length,
                        fm1_uvc_control_reply *reply);
/* Probe format/frame zero selects defaults; COMMIT requires both exactly one.
 * Any host interval request is negotiated to the sole 2000000*100ns mode.
 * GET_CUR reports that actual mode; no requested faster rate is retained. */

#endif
