#include "uvc_stream.h"
#include <string.h>

static uint32_t next_nonzero(uint32_t value)
{
    ++value;
    return value ? value : 1u;
}

static int due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static int free_slot(fm1_uvc_stream *s)
{
    unsigned i;
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i)
        if (s->packets[i].state == FM1_UVC_FREE)
            return (int)i;
    return -1;
}

static void prepare_packet(fm1_uvc_stream *s, fm1_uvc_packet *p,
                           uint32_t frame, uint16_t length, uint8_t header)
{
    s->token = next_nonzero(s->token);
    p->epoch = s->epoch;
    p->token = s->token;
    p->frame = frame;
    p->first_byte = s->next_byte;
    p->length = length;
    p->discard = 0;
    p->bytes[0] = FM1_UVC_HEADER_BYTES;
    p->bytes[1] = FM1_UVC_HEADER_EOH | header;
}

static void publish_packet(fm1_uvc_stream *s, fm1_uvc_packet *p)
{
    p->sequence = ++s->sequence;
    p->state = FM1_UVC_READY;
}

static void publish_abort(fm1_uvc_stream *s)
{
    int slot;
    fm1_uvc_packet *p;
    if (!s->abort_pending || !s->active)
        return;
    slot = free_slot(s);
    if (slot < 0)
        return;
    p = &s->packets[slot];
    prepare_packet(s, p, s->abort_frame, FM1_UVC_HEADER_BYTES,
                   s->abort_fid | FM1_UVC_HEADER_ERR | FM1_UVC_HEADER_EOF);
    publish_packet(s, p);
    s->abort_pending = 0;
}

static void invalidate(fm1_uvc_stream *s)
{
    unsigned i;
    s->epoch = next_nonzero(s->epoch);
    s->active = 0;
    s->frame_active = 0;
    s->abort_pending = 0;
    s->next_byte = 0;
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i) {
        fm1_uvc_packet *p = &s->packets[i];
        if (p->state == FM1_UVC_READY)
            p->state = FM1_UVC_FREE;
        else if (p->state != FM1_UVC_FREE)
            p->discard = 1;
    }
}

void fm1_uvc_init(fm1_uvc_stream *s)
{
    memset(s, 0, sizeof(*s));
    s->epoch = 1;
    s->fid = FM1_UVC_HEADER_FID;
}

void fm1_uvc_start(fm1_uvc_stream *s, uint32_t now)
{
    invalidate(s);
    s->active = 1;
    s->next_frame_ms = now;
}

void fm1_uvc_stop(fm1_uvc_stream *s)
{
    invalidate(s);
}

void fm1_uvc_reset(fm1_uvc_stream *s)
{
    invalidate(s);
}

int fm1_uvc_abort_frame(fm1_uvc_stream *s, uint32_t now)
{
    unsigned i;
    (void)now;
    if (!s->active || !s->frame_active)
        return FM1_UVC_WAIT;
    s->abort_frame = s->frame;
    s->abort_fid = s->fid;
    s->abort_pending = 1;
    s->frame_active = 0;
    ++s->stats.frames_dropped;
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i) {
        fm1_uvc_packet *p = &s->packets[i];
        if (p->epoch != s->epoch || p->frame != s->frame)
            continue;
        if (p->state == FM1_UVC_READY)
            p->state = FM1_UVC_FREE;
        else if (p->state != FM1_UVC_FREE)
            p->discard = 1;
    }
    publish_abort(s);
    return FM1_UVC_DROPPED;
}

int fm1_uvc_begin_frame(fm1_uvc_stream *s, uint32_t now)
{
    if (!s->active)
        return FM1_UVC_WAIT;
    if (s->frame_active && due(now, s->next_frame_ms))
        fm1_uvc_abort_frame(s, now);
    publish_abort(s);
    if (s->frame_active || s->abort_pending || !due(now, s->next_frame_ms))
        return FM1_UVC_WAIT;
    s->frame = next_nonzero(s->frame);
    s->fid ^= FM1_UVC_HEADER_FID;
    s->next_byte = 0;
    s->frame_active = 1;
    s->next_frame_ms = now + FM1_UVC_FRAME_PERIOD_MS;
    ++s->stats.frames_started;
    return FM1_UVC_OK;
}

int fm1_uvc_reserve_bytes(fm1_uvc_stream *s, uint32_t now,
                          fm1_uvc_write *w)
{
    unsigned i;
    uint32_t payload, row_offset;
    int slot;
    fm1_uvc_packet *p;
    if (!w)
        return FM1_UVC_INVALID;
    if (!s->active || !s->frame_active)
        return FM1_UVC_WAIT;
    if (due(now, s->next_frame_ms))
        return fm1_uvc_abort_frame(s, now);
    if (s->next_byte >= FM1_UVC_FRAME_BYTES)
        return FM1_UVC_INVALID;
    payload = FM1_UVC_FRAME_BYTES - s->next_byte;
    if (payload > FM1_UVC_PAYLOAD_BYTES)
        payload = FM1_UVC_PAYLOAD_BYTES;
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i)
        if (s->packets[i].state == FM1_UVC_WRITING)
            return FM1_UVC_WAIT;
    slot = free_slot(s);
    if (slot < 0) {
        ++s->stats.backpressure;
        return FM1_UVC_WAIT;
    }
    p = &s->packets[slot];
    prepare_packet(s, p, s->frame,
                   (uint16_t)(FM1_UVC_HEADER_BYTES + payload),
                   (uint8_t)(s->fid | (s->next_byte + payload == FM1_UVC_FRAME_BYTES ?
                                       FM1_UVC_HEADER_EOF : 0u)));
    p->state = FM1_UVC_WRITING;
    w->bytes = p->bytes;
    w->epoch = p->epoch;
    w->token = p->token;
    w->frame = p->frame;
    w->length = p->length;
    w->first_byte = p->first_byte;
    w->first_row = (uint16_t)(p->first_byte / (FM1_UVC_WIDTH * 2u));
    row_offset = p->first_byte % (FM1_UVC_WIDTH * 2u);
    w->rows = (uint8_t)((row_offset + payload + FM1_UVC_WIDTH * 2u - 1u) /
                        (FM1_UVC_WIDTH * 2u));
    w->slot = (uint8_t)slot;
    return FM1_UVC_OK;
}

static fm1_uvc_packet *valid_write(fm1_uvc_stream *s, const fm1_uvc_write *w)
{
    fm1_uvc_packet *p;
    uint32_t payload, row_offset, rows;
    if (!w || w->slot >= FM1_UVC_QUEUE_SLOTS)
        return 0;
    p = &s->packets[w->slot];
    payload = (uint32_t)p->length - FM1_UVC_HEADER_BYTES;
    row_offset = p->first_byte % (FM1_UVC_WIDTH * 2u);
    rows = (row_offset + payload + FM1_UVC_WIDTH * 2u - 1u) /
           (FM1_UVC_WIDTH * 2u);
    if (p->state != FM1_UVC_WRITING || p->token != w->token ||
        p->epoch != w->epoch || p->frame != w->frame || p->bytes != w->bytes ||
        p->length != w->length || p->first_byte != w->first_byte ||
        !payload || payload > FM1_UVC_PAYLOAD_BYTES || (payload & 3u) ||
        (p->first_byte & 3u) || p->first_byte + payload > FM1_UVC_FRAME_BYTES ||
        w->first_row != p->first_byte / (FM1_UVC_WIDTH * 2u) ||
        w->rows != rows || !rows || rows > FM1_UVC_ROWS_PER_PACKET)
        return 0;
    return p;
}

int fm1_uvc_publish_bytes(fm1_uvc_stream *s, uint32_t now,
                          const fm1_uvc_write *w)
{
    fm1_uvc_packet *p = valid_write(s, w);
    if (!p)
        return FM1_UVC_INVALID;
    if (s->active && s->frame_active && w->epoch == s->epoch &&
        w->frame == s->frame && due(now, s->next_frame_ms))
        fm1_uvc_abort_frame(s, now);
    if (!s->active || p->discard || w->epoch != s->epoch ||
        w->frame != s->frame || !s->frame_active) {
        p->state = FM1_UVC_FREE;
        ++s->stats.stale_releases;
        publish_abort(s);
        return FM1_UVC_STALE;
    }
    if (w->first_byte != s->next_byte)
        return FM1_UVC_INVALID;
    publish_packet(s, p);
    s->next_byte += (uint32_t)w->length - FM1_UVC_HEADER_BYTES;
    if (s->next_byte == FM1_UVC_FRAME_BYTES)
        s->frame_active = 0;
    return FM1_UVC_OK;
}

int fm1_uvc_cancel_bytes(fm1_uvc_stream *s, uint32_t now,
                         const fm1_uvc_write *w)
{
    fm1_uvc_packet *p = valid_write(s, w);
    int result;
    if (!p)
        return FM1_UVC_INVALID;
    if (s->active && !p->discard && w->epoch == s->epoch &&
        w->frame == s->frame && s->frame_active)
        result = fm1_uvc_abort_frame(s, now);
    else {
        ++s->stats.stale_releases;
        result = FM1_UVC_STALE;
    }
    p->state = FM1_UVC_FREE;
    publish_abort(s);
    return result;
}

int fm1_uvc_acquire(fm1_uvc_stream *s, fm1_uvc_read *r)
{
    unsigned i;
    int slot = -1;
    fm1_uvc_packet *p;
    if (!r)
        return FM1_UVC_INVALID;
    if (!s->active)
        return FM1_UVC_WAIT;
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i)
        if (s->packets[i].state == FM1_UVC_LEASED)
            return FM1_UVC_WAIT;
    publish_abort(s);
    for (i = 0; i < FM1_UVC_QUEUE_SLOTS; ++i) {
        fm1_uvc_packet *candidate = &s->packets[i];
        if (candidate->state == FM1_UVC_READY &&
            (slot < 0 || (int32_t)(candidate->sequence -
                                   s->packets[slot].sequence) < 0))
            slot = (int)i;
    }
    if (slot < 0)
        return FM1_UVC_WAIT;
    p = &s->packets[slot];
    p->state = FM1_UVC_LEASED;
    r->bytes = p->bytes;
    r->epoch = p->epoch;
    r->token = p->token;
    r->length = p->length;
    r->slot = (uint8_t)slot;
    return FM1_UVC_OK;
}

int fm1_uvc_commit(fm1_uvc_stream *s, const fm1_uvc_read *r,
                    int write_succeeded)
{
    fm1_uvc_packet *p;
    int result;
    if (!r || r->slot >= FM1_UVC_QUEUE_SLOTS)
        return FM1_UVC_INVALID;
    p = &s->packets[r->slot];
    if (p->state != FM1_UVC_LEASED || p->epoch != r->epoch ||
        p->token != r->token || p->bytes != r->bytes || p->length != r->length)
        return FM1_UVC_INVALID;
    if (p->epoch != s->epoch || !s->active) {
        ++s->stats.stale_releases;
        result = FM1_UVC_STALE;
    } else if (p->discard) {
        result = FM1_UVC_DROPPED;
    } else if (!write_succeeded) {
        p->state = FM1_UVC_READY;
        ++s->stats.retries;
        return FM1_UVC_RETRY;
    } else {
        ++s->stats.packets_sent;
        if ((p->bytes[1] & (FM1_UVC_HEADER_EOF | FM1_UVC_HEADER_ERR)) ==
            FM1_UVC_HEADER_EOF)
            ++s->stats.frames_sent;
        result = FM1_UVC_OK;
    }
    p->state = FM1_UVC_FREE;
    publish_abort(s);
    return result;
}

static int floor256(int value)
{
    /* C does not define right-shift rounding for negative signed values. */
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

static void pixel_yuv(const uint8_t *src, int *y, int *u, int *v)
{
    unsigned rgb = ((unsigned)src[0] << 8) | src[1];
    int r = (int)((((rgb >> 11) & 31u) * 255u + 15u) / 31u);
    int g = (int)((((rgb >> 5) & 63u) * 255u + 31u) / 63u);
    int b = (int)(((rgb & 31u) * 255u + 15u) / 31u);
    *y = floor256(66 * r + 129 * g + 25 * b + 128) + 16;
    *u = floor256(-38 * r - 74 * g + 112 * b + 128) + 128;
    *v = floor256(112 * r - 94 * g - 18 * b + 128) + 128;
}

int fm1_uvc_rgb565be_to_yuy2(uint8_t *dst, const uint8_t *src, uint32_t pixels)
{
    uint32_t i;
    if (!dst || !src || !pixels || (pixels & 1u) ||
        pixels > FM1_UVC_WIDTH * FM1_UVC_ROWS_PER_PACKET)
        return FM1_UVC_INVALID;
    for (i = 0; i < pixels; i += 2) {
        int y0, u0, v0, y1, u1, v1;
        pixel_yuv(src, &y0, &u0, &v0);
        pixel_yuv(src + 2, &y1, &u1, &v1);
        dst[0] = (uint8_t)y0;
        dst[1] = (uint8_t)((u0 + u1 + 1) / 2);
        dst[2] = (uint8_t)y1;
        dst[3] = (uint8_t)((v0 + v1 + 1) / 2);
        src += 4;
        dst += 4;
    }
    return FM1_UVC_OK;
}
