#include "uvc_control.h"
#include "uvc_stream.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_uvc_control control;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static fm1_uvc_setup request(uint8_t type, uint8_t code, uint16_t value,
                              uint16_t index, uint16_t length)
{
    fm1_uvc_setup s;
    s.request_type = type;
    s.request = code;
    s.value = value;
    s.index = index;
    s.length = length;
    return s;
}

static fm1_uvc_control_reply get(uint8_t selector, uint8_t code, uint16_t length)
{
    fm1_uvc_control_reply reply;
    fm1_uvc_setup setup = request(0xa1, code, (uint16_t)(selector << 8), FM1_UVC_VS_INTERFACE, length);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_IN);
    return reply;
}

static fm1_uvc_control_reply set(uint8_t selector)
{
    fm1_uvc_control_reply reply;
    fm1_uvc_setup setup = request(0x21, FM1_UVC_SET_CUR, (uint16_t)(selector << 8), FM1_UVC_VS_INTERFACE, 26);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_OUT);
    assert(reply.length == 26 && reply.event == FM1_UVC_EVENT_NONE);
    return reply;
}

static void configured(void)
{
    fm1_uvc_control_init(&control);
    assert(fm1_uvc_control_configure(&control, 1) == FM1_UVC_CONTROL_ACK);
    assert(control.configured && !control.committed && !control.alt);
}

static void descriptor_walk_and_terminal_chain(void)
{
    static const uint8_t yuy2_guid[] = {
        0x59, 0x55, 0x59, 0x32, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71
    };
    size_t length, cursor = 0, vc_total = 0, vs_total = 0;
    const uint8_t *table = fm1_uvc_video_descriptors(&length);
    const uint8_t *vc_header = 0, *vs_header = 0;
    unsigned descriptors = 0, iad = 0, interfaces = 0, endpoints = 0;
    unsigned camera = 0, output = 0, format = 0, frame = 0, color = 0;
    uint8_t current_interface = 0xff, current_alt = 0xff;
    assert(length == 159 && length == FM1_UVC_VIDEO_DESCRIPTOR_BYTES);
    assert(table == fm1_uvc_video_descriptor);
    while (cursor < length) {
        const uint8_t *d = table + cursor;
        assert(d[0] >= 2 && cursor + d[0] <= length);
        assert(d[1] != 2);  /* Root owns the configuration descriptor. */
        ++descriptors;
        if (d[1] == 0x0b) {
            assert(d[0] == 8 && d[2] == FM1_UVC_VC_INTERFACE && d[3] == 2);
            assert(d[4] == 0x0e && d[5] == 3 && d[6] == 0);
            ++iad;
        } else if (d[1] == 4) {
            assert(d[0] == 9 && d[5] == 0x0e && d[7] == 0);
            current_interface = d[2];
            current_alt = d[3];
            if (interfaces == 0)
                assert(d[2] == FM1_UVC_VC_INTERFACE && d[3] == 0 && d[4] == 0 && d[6] == 1);
            else if (interfaces == 1)
                assert(d[2] == FM1_UVC_VS_INTERFACE && d[3] == 0 && d[4] == 0 && d[6] == 2);
            else
                assert(d[2] == FM1_UVC_VS_INTERFACE && d[3] == 1 && d[4] == 1 && d[6] == 2);
            ++interfaces;
        } else if (d[1] == 5) {
            assert(current_interface == FM1_UVC_VS_INTERFACE && current_alt == 1);
            assert(d[0] == 7 && d[2] == 0x84 && d[3] == 1);
            assert(le16(d + 4) == 642 && le16(d + 4) < 1024 && d[6] == 1);
            ++endpoints;
        } else {
            assert(d[1] == 0x24);
            if (current_interface == FM1_UVC_VC_INTERFACE) {
                vc_total += d[0];
                if (d[2] == 1) {
                    vc_header = d;
                    assert(d[0] == 13 && le16(d + 3) == 0x0100);
                    assert(le32(d + 7) == 1000 && d[11] == 1 && d[12] == FM1_UVC_VS_INTERFACE);
                } else if (d[2] == 2) {
                    assert(d[0] == 18 && d[3] == 1 && le16(d + 4) == 0x0201);
                    assert(d[14] == 3 && d[15] == 0 && d[16] == 0 && d[17] == 0);
                    ++camera;
                } else {
                    assert(d[2] == 3 && d[0] == 9 && d[3] == 2);
                    assert(le16(d + 4) == 0x0101 && d[7] == 1);
                    ++output;
                }
            } else {
                assert(current_interface == FM1_UVC_VS_INTERFACE && current_alt == 0);
                vs_total += d[0];
                if (d[2] == 1) {
                    vs_header = d;
                    assert(d[0] == 14 && d[3] == 1 && d[6] == 0x84);
                    assert(d[8] == 2 && d[9] == 0 && d[10] == 0 && d[11] == 0);
                    assert(d[12] == 1 && d[13] == 0);
                } else if (d[2] == 4) {
                    assert(d[0] == 27 && d[3] == 1 && d[4] == 1);
                    assert(memcmp(d + 5, yuy2_guid, 16) == 0);
                    assert(d[21] == 16 && d[22] == 1 && d[25] == 0 && d[26] == 0);
                    ++format;
                } else if (d[2] == 5) {
                    assert(d[0] == 30 && d[3] == 1 && d[4] == 0);
                    assert(le16(d + 5) == FM1_UVC_WIDTH && le16(d + 7) == FM1_UVC_HEIGHT);
                    assert(le32(d + 9) == FM1_UVC_FRAME_BITRATE &&
                           le32(d + 13) == FM1_UVC_FRAME_BITRATE);
                    assert(le32(d + 17) == FM1_UVC_FRAME_BYTES &&
                           le32(d + 21) == FM1_UVC_FRAME_INTERVAL_100NS);
                    assert(d[25] == 1 && le32(d + 26) == 2000000);
                    ++frame;
                } else {
                    assert(d[0] == 6 && d[2] == 0x0d);
                    assert(d[3] == 4 && d[4] == 4 && d[5] == 4);
                    ++color;
                }
            }
        }
        cursor += d[0];
    }
    assert(cursor == length && descriptors == 12);
    assert(iad == 1 && interfaces == 3 && endpoints == 1);
    assert(camera == 1 && output == 1 && format == 1 && frame == 1 && color == 1);
    assert(vc_header && vs_header && vc_total == 40 && vs_total == 77);
    assert(le16(vc_header + 5) == vc_total && le16(vs_header + 4) == vs_total);
}

static void bounded_descriptor_copy(void)
{
    uint8_t buffer[161], unchanged[161];
    const uint8_t *table;
    size_t length, capacity;
    table = fm1_uvc_video_descriptors(&length);
    memset(buffer, 0x5a, sizeof(buffer));
    memcpy(unchanged, buffer, sizeof(buffer));
    for (capacity = 0; capacity < length; ++capacity) {
        assert(fm1_uvc_copy_video_descriptors(buffer + 1, capacity) == 0);
        assert(memcmp(buffer, unchanged, sizeof(buffer)) == 0);
    }
    assert(fm1_uvc_copy_video_descriptors(0, 200) == 0);
    assert(fm1_uvc_copy_video_descriptors(buffer + 1, length) == length);
    assert(buffer[0] == 0x5a && buffer[160] == 0x5a);
    assert(memcmp(buffer + 1, table, length) == 0);
}

static void get_requests_and_little_endian_fields(void)
{
    uint8_t selectors[] = {1, 2};
    uint8_t codes[] = {FM1_UVC_GET_CUR, FM1_UVC_GET_MIN, FM1_UVC_GET_MAX, FM1_UVC_GET_DEF};
    unsigned i, j;
    configured();
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 4; ++j) {
            fm1_uvc_control_reply reply = get(selectors[i], codes[j], 26);
            assert(reply.length == 26 && le16(reply.data) == 1);
            assert(reply.data[2] == 1 && reply.data[3] == 1);
            assert(le32(reply.data + 4) == 2000000);
            assert(le32(reply.data + 18) == FM1_UVC_FRAME_BYTES && le32(reply.data + 22) == 642);
            assert(le16(reply.data + 8) == 0 && le16(reply.data + 10) == 0);
            assert(le16(reply.data + 12) == 0 && le16(reply.data + 14) == 0);
            assert(le16(reply.data + 16) == 0);
        }
        {
            fm1_uvc_control_reply reply = get(selectors[i], FM1_UVC_GET_LEN, 2);
            assert(reply.length == 2 && le16(reply.data) == 26);
            reply = get(selectors[i], FM1_UVC_GET_INFO, 1);
            assert(reply.length == 1 && reply.data[0] == 3);
            reply = get(selectors[i], FM1_UVC_GET_RES, 26);
            for (j = 0; j < 26; ++j)
                assert(reply.data[j] == (j == 2 || j == 3 ? 1 : 0));
            reply = get(selectors[i], FM1_UVC_GET_CUR, 2);
            assert(reply.length == 2 && le16(reply.data) == 1);
            reply = get(selectors[i], FM1_UVC_GET_CUR, 0xffff);
            assert(reply.length == 26);
        }
    }
    assert(!control.committed);  /* Reading COMMIT never enables streaming. */
}

static void probe_normalizes_and_commit_applies_after_complete_out(void)
{
    fm1_uvc_control_reply reply, final;
    uint8_t payload[26], before[26];
    configured();
    memset(payload, 0xff, sizeof(payload));
    payload[2] = payload[3] = 0; /* Probe default format/frame is accepted. */
    reply = set(1);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_ACK);
    assert(control.probe[2] == 1 && control.probe[3] == 1);
    assert(le32(control.probe + 4) == 2000000);
    assert(le16(control.probe + 12) == 0 && le16(control.probe + 16) == 0);
    assert(le32(control.probe + 18) == FM1_UVC_FRAME_BYTES && le32(control.probe + 22) == 642);
    assert(!control.committed);
    memcpy(payload, control.probe, sizeof(payload));
    reply = set(2);
    memcpy(before, control.commit, 26);
    assert(!control.committed && memcmp(before, control.commit, 26) == 0);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 25, &final) == FM1_UVC_CONTROL_STALL);
    assert(!control.committed && memcmp(before, control.commit, 26) == 0);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    reply = set(2);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_ACK);
    assert(control.committed && memcmp(control.commit, control.probe, 26) == 0);
    assert(final.event == FM1_UVC_EVENT_NONE && final.length == 0);
}

static void invalid_probe_and_commit_do_not_mutate_committed_mode(void)
{
    unsigned selector, invalid_index;
    fm1_uvc_control_reply reply, final;
    uint8_t payload[26], old_commit[26];
    configured();
    memcpy(payload, control.probe, 26);
    reply = set(2);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_ACK);
    memcpy(old_commit, control.commit, 26);
    for (selector = 1; selector <= 2; ++selector) {
        for (invalid_index = 2; invalid_index <= 3; ++invalid_index) {
            memcpy(payload, control.probe, 26);
            payload[invalid_index] = 2;
            reply = set((uint8_t)selector);
            assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
            assert(control.committed && memcmp(old_commit, control.commit, 26) == 0);
        }
    }
    for (invalid_index = 2; invalid_index <= 3; ++invalid_index) {
        memcpy(payload, control.probe, 26);
        payload[invalid_index] = 0;
        reply = set(2);
        assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    }
    reply = set(1);
    assert(fm1_uvc_control_out(&control, reply.token, 0, 26, &final) == FM1_UVC_CONTROL_STALL);
    reply = set(1);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 27, &final) == FM1_UVC_CONTROL_STALL);
}

static void interface_alt_requires_config_and_commit(void)
{
    fm1_uvc_setup setup;
    fm1_uvc_control_reply reply, pending;
    uint8_t payload[26];
    fm1_uvc_control_init(&control);
    setup = request(0x01, 11, 1, FM1_UVC_VS_INTERFACE, 0);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_STALL);
    assert(fm1_uvc_control_configure(&control, 1) == FM1_UVC_CONTROL_ACK);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_STALL);
    assert(!control.alt);
    pending = set(2);
    memcpy(payload, control.probe, 26);
    assert(fm1_uvc_control_out(&control, pending.token, payload, 26, &reply) == FM1_UVC_CONTROL_ACK);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_ACK);
    assert(control.alt == 1 && reply.event == FM1_UVC_EVENT_START);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_ACK);
    assert(reply.event == FM1_UVC_EVENT_NONE);
    setup = request(0x81, 10, 0, FM1_UVC_VS_INTERFACE, 1);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_IN);
    assert(reply.length == 1 && reply.data[0] == 1);
    setup = request(0x01, 11, 0, FM1_UVC_VS_INTERFACE, 0);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_ACK);
    assert(!control.alt && control.committed && reply.event == FM1_UVC_EVENT_STOP);
    setup = request(0x01, 11, 1, FM1_UVC_VS_INTERFACE, 0);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_ACK);
    assert(control.alt == 1 && reply.event == FM1_UVC_EVENT_START);
    setup = request(0x81, 10, 0, FM1_UVC_VC_INTERFACE, 1);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_IN);
    assert(reply.data[0] == 0);
    setup = request(0x01, 11, 0, FM1_UVC_VC_INTERFACE, 0);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_ACK);
    assert(control.alt == 1); /* VC alt0 cannot stop the separate VS interface. */
}

static void reset_and_new_setup_invalidate_pending_transfers(void)
{
    fm1_uvc_control_reply old, newer, final;
    uint8_t payload[26];
    configured();
    memcpy(payload, control.probe, 26);
    old = set(2);
    newer = set(1);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    assert(!control.committed && control.pending_selector == 1);
    assert(fm1_uvc_control_out(&control, newer.token, payload, 26, &final) == FM1_UVC_CONTROL_ACK);
    old = set(2);
    (void)get(1, FM1_UVC_GET_CUR, 26);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    old = set(2);
    fm1_uvc_control_cancel_pending(&control); /* An unrelated CDC/device SETUP. */
    assert(control.configured && !control.committed && !control.alt && !control.pending_selector);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    old = set(2);
    fm1_uvc_control_reset(&control);
    assert(!control.configured && !control.committed && !control.alt && !control.pending_selector);
    assert(fm1_uvc_control_configure(&control, 1) == FM1_UVC_CONTROL_ACK);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    newer = set(2);
    assert(fm1_uvc_control_out(&control, newer.token, payload, 26, &final) == FM1_UVC_CONTROL_ACK);
    control.alt = 1;
    fm1_uvc_control_cancel_pending(&control);
    assert(control.configured && control.committed && control.alt == 1);
    assert(fm1_uvc_control_configure(&control, 0) == FM1_UVC_CONTROL_ACK);
    assert(!control.configured && !control.committed && !control.alt);
    assert(fm1_uvc_control_configure(&control, 2) == FM1_UVC_CONTROL_STALL);
    assert(!control.configured && !control.committed);
}

static void malformed_and_unsupported_setup_stall(void)
{
    fm1_uvc_setup setups[] = {
        {0xa1, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VC_INTERFACE, 26},
        {0xa1, FM1_UVC_GET_CUR, 0x0100, (0x0100 | FM1_UVC_VS_INTERFACE), 26},
        {0xa1, FM1_UVC_GET_CUR, 0x0101, FM1_UVC_VS_INTERFACE, 26},
        {0xa1, FM1_UVC_GET_CUR, 0x0300, FM1_UVC_VS_INTERFACE, 26},
        {0xa1, 0xff, 0x0100, FM1_UVC_VS_INTERFACE, 26},
        {0xa1, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 0},
        {0xa1, FM1_UVC_SET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 26},
        {0x21, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 26},
        {0x21, FM1_UVC_SET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 25},
        {0x21, FM1_UVC_SET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 27},
        {0xc1, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 26},
        {0xa2, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VS_INTERFACE, 26},
        {0x01, 11, 2, FM1_UVC_VS_INTERFACE, 0},
        {0x01, 11, 1, FM1_UVC_VC_INTERFACE, 0},
        {0x01, 11, 0, FM1_UVC_VS_INTERFACE, 1},
        {0x81, 10, 1, FM1_UVC_VS_INTERFACE, 1},
        {0x81, 10, 0, 0, 1},
    };
    fm1_uvc_control_reply reply;
    unsigned i;
    configured();
    for (i = 0; i < sizeof(setups) / sizeof(setups[0]); ++i) {
        assert(fm1_uvc_control_setup(&control, &setups[i], &reply) == FM1_UVC_CONTROL_STALL);
        assert(reply.length == 0 && reply.event == FM1_UVC_EVENT_NONE);
        assert(!control.committed && !control.alt && !control.pending_selector);
    }
    assert(fm1_uvc_control_setup(0, &setups[0], &reply) == FM1_UVC_CONTROL_STALL);
    assert(fm1_uvc_control_setup(&control, 0, &reply) == FM1_UVC_CONTROL_STALL);
    assert(fm1_uvc_control_setup(&control, &setups[0], 0) == FM1_UVC_CONTROL_STALL);
}

static fm1_uvc_control_reply get_error(uint8_t code, uint16_t length)
{
    fm1_uvc_control_reply reply;
    fm1_uvc_setup setup = request(0xa1, code, 0x0200, FM1_UVC_VC_INTERFACE, length);
    assert(fm1_uvc_control_setup(&control, &setup, &reply) == FM1_UVC_CONTROL_IN);
    return reply;
}

static void request_error_control_is_get_only_and_bounded(void)
{
    fm1_uvc_control_reply reply, final;
    uint8_t payload[26];
    unsigned i;
    configured();
    reply = get_error(FM1_UVC_GET_CUR, 1);
    assert(reply.length == 1 && reply.data[0] == FM1_UVC_ERROR_NONE);
    assert(reply.event == FM1_UVC_EVENT_NONE && !control.committed && !control.alt);
    reply = get_error(FM1_UVC_GET_INFO, 0xffff);
    assert(reply.length == 1 && reply.data[0] == 1); /* GET only, not SET. */
    for (i = 1; i < sizeof(reply.data); ++i)
        assert(reply.data[i] == 0);
    memcpy(payload, control.probe, sizeof(payload));
    reply = set(FM1_UVC_COMMIT_SELECTOR);
    final = get_error(FM1_UVC_GET_CUR, 2);
    assert(final.length == 1 && final.data[0] == FM1_UVC_ERROR_NONE);
    assert(!control.pending_selector);
    assert(fm1_uvc_control_out(&control, reply.token, payload, 26, &final) == FM1_UVC_CONTROL_STALL);
    assert(!control.committed && control.request_error == FM1_UVC_ERROR_NONE);
}

static void request_error_rejects_malformed_vc_requests(void)
{
    static const struct {
        fm1_uvc_setup setup;
        uint8_t error;
    } cases[] = {
        {{0xa1, FM1_UVC_GET_CUR, 0x0200, (0x0300 | FM1_UVC_VC_INTERFACE), 1}, FM1_UVC_ERROR_INVALID_UNIT},
        {{0xa1, FM1_UVC_GET_CUR, 0x0200, (0x0100 | FM1_UVC_VC_INTERFACE), 1}, FM1_UVC_ERROR_INVALID_CONTROL},
        {{0xa1, FM1_UVC_GET_CUR, 0x0200, (0x0200 | FM1_UVC_VC_INTERFACE), 1}, FM1_UVC_ERROR_INVALID_CONTROL},
        {{0xa1, FM1_UVC_GET_CUR, 0x0100, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_CONTROL},
        {{0xa1, FM1_UVC_GET_CUR, 0x0300, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_CONTROL},
        {{0xa1, FM1_UVC_GET_CUR, 0x0201, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_CUR, 0x0200, 0, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 0}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_INFO, 0x0200, FM1_UVC_VC_INTERFACE, 0}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0x21, FM1_UVC_GET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0x21, FM1_UVC_SET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_LEN, 0x0200, FM1_UVC_VC_INTERFACE, 2}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_MIN, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_MAX, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_RES, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, FM1_UVC_GET_DEF, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa1, 0xff, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
        {{0xa2, FM1_UVC_GET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 1}, FM1_UVC_ERROR_INVALID_REQUEST},
    };
    fm1_uvc_control_reply reply;
    unsigned i;
    configured();
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(fm1_uvc_control_setup(&control, &cases[i].setup, &reply) == FM1_UVC_CONTROL_STALL);
        assert(reply.length == 0 && reply.event == FM1_UVC_EVENT_NONE);
        assert(control.request_error == cases[i].error);
        assert(!control.pending_selector && !control.committed && !control.alt);
        reply = get_error(FM1_UVC_GET_CUR, 1);
        assert(reply.length == 1 && reply.data[0] == cases[i].error);
        assert(control.request_error == FM1_UVC_ERROR_NONE);
        reply = get_error(FM1_UVC_GET_CUR, 1);
        assert(reply.data[0] == FM1_UVC_ERROR_NONE);
    }
    /* Vendor requests do not consume or replace a video class request error. */
    {
        fm1_uvc_setup bad = request(0xa1, FM1_UVC_GET_CUR, 0x0300, FM1_UVC_VC_INTERFACE, 1);
        fm1_uvc_setup vendor = request(0xc1, FM1_UVC_GET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 1);
        assert(fm1_uvc_control_setup(&control, &bad, &reply) == FM1_UVC_CONTROL_STALL);
        assert(fm1_uvc_control_setup(&control, &vendor, &reply) == FM1_UVC_CONTROL_STALL);
        reply = get_error(FM1_UVC_GET_CUR, 1);
        assert(reply.data[0] == FM1_UVC_ERROR_INVALID_CONTROL);
    }
    assert(fm1_uvc_control_configure(&control, 0) == FM1_UVC_CONTROL_ACK);
    {
        fm1_uvc_setup query = request(0xa1, FM1_UVC_GET_CUR, 0x0200, FM1_UVC_VC_INTERFACE, 1);
        assert(fm1_uvc_control_setup(&control, &query, &reply) == FM1_UVC_CONTROL_STALL);
        assert(reply.length == 0 && control.request_error == FM1_UVC_ERROR_WRONG_STATE);
    }
}

static void request_error_lifecycle_and_stale_out_isolation(void)
{
    fm1_uvc_setup bad = request(0xa1, FM1_UVC_GET_CUR, 0x0300, FM1_UVC_VS_INTERFACE, 26);
    fm1_uvc_setup standard = request(0x81, 10, 0, FM1_UVC_VC_INTERFACE, 1);
    fm1_uvc_control_reply old, newer, reply;
    uint8_t payload[26];
    configured();
    assert(fm1_uvc_control_setup(&control, &bad, &reply) == FM1_UVC_CONTROL_STALL);
    fm1_uvc_control_cancel_pending(&control);
    assert(control.request_error == FM1_UVC_ERROR_INVALID_CONTROL);
    reply = get_error(FM1_UVC_GET_INFO, 1);
    assert(reply.data[0] == 1 && control.request_error == FM1_UVC_ERROR_NONE);
    assert(fm1_uvc_control_setup(&control, &bad, &reply) == FM1_UVC_CONTROL_STALL);
    (void)get(FM1_UVC_PROBE_SELECTOR, FM1_UVC_GET_CUR, 26);
    assert(control.request_error == FM1_UVC_ERROR_NONE);
    assert(fm1_uvc_control_setup(&control, &bad, &reply) == FM1_UVC_CONTROL_STALL);
    assert(fm1_uvc_control_setup(&control, &standard, &reply) == FM1_UVC_CONTROL_IN);
    assert(control.request_error == FM1_UVC_ERROR_NONE);

    memcpy(payload, control.probe, sizeof(payload));
    old = set(FM1_UVC_COMMIT_SELECTOR);
    newer = set(FM1_UVC_PROBE_SELECTOR);
    assert(fm1_uvc_control_out(&control, newer.token, payload, 25, &reply) == FM1_UVC_CONTROL_STALL);
    assert(control.request_error == FM1_UVC_ERROR_INVALID_REQUEST && !control.committed);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &reply) == FM1_UVC_CONTROL_STALL);
    assert(control.request_error == FM1_UVC_ERROR_INVALID_REQUEST);
    reply = get_error(FM1_UVC_GET_CUR, 1);
    assert(reply.data[0] == FM1_UVC_ERROR_INVALID_REQUEST && control.request_error == FM1_UVC_ERROR_NONE);

    newer = set(FM1_UVC_COMMIT_SELECTOR);
    payload[2] = 2;
    assert(fm1_uvc_control_out(&control, newer.token, payload, 26, &reply) == FM1_UVC_CONTROL_STALL);
    assert(control.request_error == FM1_UVC_ERROR_OUT_OF_RANGE && !control.committed);
    payload[2] = 1;
    newer = set(FM1_UVC_COMMIT_SELECTOR);
    /* SETUP starts the transfer; it has not completed successfully yet. */
    assert(control.request_error == FM1_UVC_ERROR_OUT_OF_RANGE);
    assert(fm1_uvc_control_out(&control, old.token, payload, 26, &reply) == FM1_UVC_CONTROL_STALL);
    assert(control.request_error == FM1_UVC_ERROR_OUT_OF_RANGE && control.pending_selector == 2);
    assert(fm1_uvc_control_out(&control, newer.token, payload, 26, &reply) == FM1_UVC_CONTROL_ACK);
    assert(control.request_error == FM1_UVC_ERROR_NONE && control.committed);
    assert(fm1_uvc_control_setup(&control, &bad, &reply) == FM1_UVC_CONTROL_STALL);
    fm1_uvc_control_reset(&control);
    assert(control.request_error == FM1_UVC_ERROR_NONE && !control.configured && !control.committed);
}

int main(void)
{
    descriptor_walk_and_terminal_chain();
    bounded_descriptor_copy();
    get_requests_and_little_endian_fields();
    probe_normalizes_and_commit_applies_after_complete_out();
    invalid_probe_and_commit_do_not_mutate_committed_mode();
    interface_alt_requires_config_and_commit();
    reset_and_new_setup_invalidate_pending_transfers();
    malformed_and_unsupported_setup_stall();
    request_error_control_is_get_only_and_bounded();
    request_error_rejects_malformed_vc_requests();
    request_error_lifecycle_and_stale_out_isolation();
    printf("UVC control: 11 focused test groups passed; state %zu, reply %zu, setup %zu bytes.\n",
           sizeof(fm1_uvc_control), sizeof(fm1_uvc_control_reply), sizeof(fm1_uvc_setup));
    return 0;
}
