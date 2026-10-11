#include "uvc_control.h"
#include "uvc_stream.h"
#include <string.h>

#define LE16(value) ((value) & 255u), (((value) >> 8) & 255u)
#define LE32(value) LE16(value), LE16((value) >> 16)

#if defined(__GNUC__)
#define FM1_UVC_RETAIN __attribute__((used))
#else
#define FM1_UVC_RETAIN
#endif

FM1_UVC_RETAIN const uint8_t fm1_uvc_video_descriptor[FM1_UVC_VIDEO_DESCRIPTOR_BYTES] = {
    /* IAD: interfaces 2/3, Video Interface Collection. */
    8, 0x0b, 2, 2, 0x0e, 3, 0, 0,
    /* VC standard interface; the optional status endpoint is absent. */
    9, 4, 2, 0, 0, 0x0e, 1, 0, 0,
    /* VC header, UVC1.0. Millisecond timing is 1000 Hz; payload has no PTS/SCR. */
    13, 0x24, 1, LE16(0x0100u), LE16(40u), LE32(1000u), 1, 3,
    /* Camera input terminal ID1, no camera controls. */
    18, 0x24, 2, 1, LE16(0x0201u), 0, 0,
    LE16(0u), LE16(0u), LE16(0u), 3, 0, 0, 0,
    /* USB streaming output terminal ID2, sourced directly from ID1. */
    9, 0x24, 3, 2, LE16(0x0101u), 0, 1, 0,
    /* VS alternate 0 consumes no bandwidth. */
    9, 4, 3, 0, 0, 0x0e, 2, 0, 0,
    /* VS input header: one format, one control byte, terminal link ID2. */
    14, 0x24, 1, 1, LE16(77u), 0x84, 0, 2, 0, 0, 0, 1, 0,
    /* Uncompressed YUY2: standard FOURCC media subtype GUID, 16bpp. */
    27, 0x24, 4, 1, 1,
    'Y', 'U', 'Y', '2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71,
    16, 1, 0, 0, 0, 0,
    /* One discrete 240x240 frame at 5fps: 26 + 4*1 = 30 bytes. */
    30, 0x24, 5, 1, 0, LE16(FM1_UVC_WIDTH), LE16(FM1_UVC_HEIGHT),
    LE32(4608000u), LE32(4608000u), LE32(FM1_UVC_FRAME_BYTES),
    LE32(FM1_UVC_FRAME_INTERVAL_100NS), 1, LE32(FM1_UVC_FRAME_INTERVAL_100NS),
    /* SMPTE170M primaries/transfer/matrix: BT.601 limited-range YUY2. */
    6, 0x24, 0x0d, 4, 4, 4,
    /* VS alternate 1: native full-speed ISO data, no HS multiplier. */
    9, 4, 3, 1, 1, 0x0e, 2, 0, 0,
    7, 5, FM1_UVC_VIDEO_ENDPOINT, 1, LE16(FM1_UVC_PACKET_BYTES), 1
};

typedef char descriptor_size_is_exact[
    sizeof(fm1_uvc_video_descriptor) == FM1_UVC_VIDEO_DESCRIPTOR_BYTES ? 1 : -1];

static uint32_t next_token(uint32_t token)
{
    ++token;
    return token ? token : 1u;
}

static void put32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void fixed_mode(uint8_t *data)
{
    memset(data, 0, FM1_UVC_CONTROL_BYTES);
    data[0] = 1; /* bmHint D0: dwFrameInterval is fixed to the sole mode. */
    data[2] = 1;
    data[3] = 1;
    put32(data + 4, FM1_UVC_FRAME_INTERVAL_100NS);
    put32(data + 18, FM1_UVC_FRAME_BYTES);
    put32(data + 22, FM1_UVC_PACKET_BYTES);
}

const uint8_t *fm1_uvc_video_descriptors(size_t *length)
{
    if (length)
        *length = sizeof(fm1_uvc_video_descriptor);
    return fm1_uvc_video_descriptor;
}

size_t fm1_uvc_copy_video_descriptors(uint8_t *destination, size_t capacity)
{
    if (!destination || capacity < sizeof(fm1_uvc_video_descriptor))
        return 0;
    memcpy(destination, fm1_uvc_video_descriptor, sizeof(fm1_uvc_video_descriptor));
    return sizeof(fm1_uvc_video_descriptor);
}

void fm1_uvc_control_init(fm1_uvc_control *c)
{
    memset(c, 0, sizeof(*c));
    c->token = 1;
    fixed_mode(c->probe);
    fixed_mode(c->commit);
}

void fm1_uvc_control_reset(fm1_uvc_control *c)
{
    c->token = next_token(c->token);
    c->configured = c->committed = c->alt = c->pending_selector = 0;
    c->request_error = FM1_UVC_ERROR_NONE;
    fixed_mode(c->probe);
    fixed_mode(c->commit);
}

void fm1_uvc_control_cancel_pending(fm1_uvc_control *c)
{
    c->token = next_token(c->token);
    c->pending_selector = 0;
}

int fm1_uvc_control_configure(fm1_uvc_control *c, uint8_t configuration)
{
    if (configuration > 1)
        return FM1_UVC_CONTROL_STALL;
    fm1_uvc_control_reset(c);
    c->configured = configuration;
    return FM1_UVC_CONTROL_ACK;
}

static int request_stall(fm1_uvc_control *c, uint8_t error)
{
    c->request_error = error;
    return FM1_UVC_CONTROL_STALL;
}

static int in_reply(fm1_uvc_control *c, fm1_uvc_control_reply *r,
                    uint16_t requested, uint16_t available)
{
    if (!requested)
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    r->length = requested < available ? requested : available;
    c->request_error = FM1_UVC_ERROR_NONE;
    return FM1_UVC_CONTROL_IN;
}

static int standard_interface(fm1_uvc_control *c, const fm1_uvc_setup *s,
                               fm1_uvc_control_reply *r)
{
    if (s->index != FM1_UVC_VC_INTERFACE && s->index != FM1_UVC_VS_INTERFACE)
        return FM1_UVC_CONTROL_STALL;
    if (s->request_type == 0x81 && s->request == 10 && s->value == 0) {
        r->data[0] = s->index == FM1_UVC_VS_INTERFACE ? c->alt : 0;
        return in_reply(c, r, s->length, 1);
    }
    if (s->request_type != 0x01 || s->request != 11 || s->length != 0)
        return FM1_UVC_CONTROL_STALL;
    if (s->index == FM1_UVC_VC_INTERFACE)
        return s->value == 0 ? FM1_UVC_CONTROL_ACK : FM1_UVC_CONTROL_STALL;
    if (s->value > 1 || (s->value == 1 && !c->committed))
        return FM1_UVC_CONTROL_STALL;
    if (s->value != c->alt)
        r->event = s->value ? FM1_UVC_EVENT_START : FM1_UVC_EVENT_STOP;
    c->alt = (uint8_t)s->value;
    return FM1_UVC_CONTROL_ACK;
}

int fm1_uvc_control_setup(fm1_uvc_control *c, const fm1_uvc_setup *s,
                          fm1_uvc_control_reply *r)
{
    uint8_t selector;
    if (!c || !s || !r)
        return FM1_UVC_CONTROL_STALL;
    memset(r, 0, sizeof(*r));
    fm1_uvc_control_cancel_pending(c);
    r->token = c->token;
    if (!c->configured)
        return request_stall(c, FM1_UVC_ERROR_WRONG_STATE);
    if (s->request_type == 0x01 || s->request_type == 0x81) {
        int result = standard_interface(c, s, r);
        c->request_error = result == FM1_UVC_CONTROL_STALL ?
            FM1_UVC_ERROR_INVALID_REQUEST : FM1_UVC_ERROR_NONE;
        return result;
    }
    if (s->request_type != 0x21 && s->request_type != 0xa1) {
        if ((s->request_type & 0x60u) == 0x20u)
            return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
        /* Vendor/device requests are not video class requests. */
        return FM1_UVC_CONTROL_STALL;
    }
    if ((s->index & 0xffu) != FM1_UVC_VC_INTERFACE &&
        (s->index & 0xffu) != FM1_UVC_VS_INTERFACE)
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    if (s->value & 0xffu)
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    if (s->index >> 8) {
        /* Camera/output terminal IDs exist, but have no advertised controls. */
        uint8_t error = (s->index & 0xffu) == FM1_UVC_VC_INTERFACE &&
            (s->index >> 8) <= 2u ? FM1_UVC_ERROR_INVALID_CONTROL : FM1_UVC_ERROR_INVALID_UNIT;
        return request_stall(c, error);
    }
    selector = (uint8_t)(s->value >> 8);
    if (s->index == FM1_UVC_VC_INTERFACE) {
        if (selector != FM1_UVC_REQUEST_ERROR_SELECTOR)
            return request_stall(c, FM1_UVC_ERROR_INVALID_CONTROL);
        if (s->request_type != 0xa1 ||
            (s->request != FM1_UVC_GET_CUR && s->request != FM1_UVC_GET_INFO))
            return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
        /* Preserve the prior code in the response before successful dispatch
         * clears it, matching TinyUSB's handle_video_ctl_cs_req/callback. */
        r->data[0] = s->request == FM1_UVC_GET_CUR ? c->request_error : 1u;
        return in_reply(c, r, s->length, 1);
    }
    if (selector != FM1_UVC_PROBE_SELECTOR && selector != FM1_UVC_COMMIT_SELECTOR)
        return request_stall(c, FM1_UVC_ERROR_INVALID_CONTROL);
    if (s->request_type == 0x21 && s->request == FM1_UVC_SET_CUR) {
        if (s->length != FM1_UVC_CONTROL_BYTES)
            return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
        c->pending_selector = selector;
        r->length = FM1_UVC_CONTROL_BYTES;
        return FM1_UVC_CONTROL_OUT;
    }
    if (s->request_type != 0xa1)
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    switch (s->request) {
    case FM1_UVC_GET_CUR:
        memcpy(r->data, selector == FM1_UVC_PROBE_SELECTOR ? c->probe : c->commit,
               FM1_UVC_CONTROL_BYTES);
        return in_reply(c, r, s->length, FM1_UVC_CONTROL_BYTES);
    case FM1_UVC_GET_MIN:
    case FM1_UVC_GET_MAX:
    case FM1_UVC_GET_DEF:
        fixed_mode(r->data);
        return in_reply(c, r, s->length, FM1_UVC_CONTROL_BYTES);
    case FM1_UVC_GET_RES:
        /* Match SDK probe/commit resolution: index steps are one; the sole
         * discrete interval and unsupported/readonly fields have no step. */
        r->data[2] = r->data[3] = 1;
        return in_reply(c, r, s->length, FM1_UVC_CONTROL_BYTES);
    case FM1_UVC_GET_LEN:
        r->data[0] = FM1_UVC_CONTROL_BYTES;
        return in_reply(c, r, s->length, 2);
    case FM1_UVC_GET_INFO:
        r->data[0] = 3; /* GET and SET, synchronous; no auto-update. */
        return in_reply(c, r, s->length, 1);
    default:
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    }
}

int fm1_uvc_control_out(fm1_uvc_control *c, uint32_t token,
                        const uint8_t *payload, size_t length,
                        fm1_uvc_control_reply *r)
{
    uint8_t selector;
    if (!c || !r)
        return FM1_UVC_CONTROL_STALL;
    memset(r, 0, sizeof(*r));
    r->token = token;
    if (token != c->token)
        return FM1_UVC_CONTROL_STALL; /* Do not cancel a newer valid transfer. */
    selector = c->pending_selector;
    c->pending_selector = 0;
    if (!c->configured || !selector)
        return request_stall(c, FM1_UVC_ERROR_WRONG_STATE);
    if (!payload || length != FM1_UVC_CONTROL_BYTES)
        return request_stall(c, FM1_UVC_ERROR_INVALID_REQUEST);
    if (payload[2] > 1 || payload[3] > 1 ||
        (selector == FM1_UVC_COMMIT_SELECTOR && (payload[2] != 1 || payload[3] != 1)))
        return request_stall(c, FM1_UVC_ERROR_OUT_OF_RANGE);
    /* The sole mode is the closest available interval for every uint32 host
     * request, including zero/default. Unsupported uncompressed quality/rate
     * hints, sizes and delays are normalized to the fixed canonical reply. */
    if (selector == FM1_UVC_PROBE_SELECTOR)
        fixed_mode(c->probe);
    else {
        fixed_mode(c->commit);
        c->committed = 1;
    }
    c->request_error = FM1_UVC_ERROR_NONE;
    return FM1_UVC_CONTROL_ACK;
}
