#include "uvc_stream.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_uvc_stream stream;
static uint8_t frame_rgb[FM1_UVC_FRAME_BYTES];
static uint8_t frame_yuy2[FM1_UVC_FRAME_BYTES];
static uint8_t received_yuy2[FM1_UVC_FRAME_BYTES];

static void started(uint32_t now)
{
    fm1_uvc_init(&stream);
    fm1_uvc_start(&stream, now);
    assert(fm1_uvc_begin_frame(&stream, now) == FM1_UVC_OK);
}

static fm1_uvc_write offer(uint32_t now)
{
    fm1_uvc_write write;
    assert(fm1_uvc_reserve_bytes(&stream, now, &write) == FM1_UVC_OK);
    memset(write.bytes + 2, (uint8_t)write.first_row, write.length - 2);
    assert(fm1_uvc_publish_bytes(&stream, now, &write) == FM1_UVC_OK);
    return write;
}

static void complete_frame_and_cadence(void)
{
    uint8_t rgb[FM1_UVC_WIDTH * FM1_UVC_ROWS_PER_PACKET * 2u];
    unsigned pixel, packet, byte_count = 0, eof_count = 0;
    unsigned crossings = 0;
    const unsigned row_bytes = FM1_UVC_WIDTH * 2u;
    const unsigned packet_count = FM1_UVC_FRAME_BYTES / FM1_UVC_PAYLOAD_BYTES;
    uint8_t fid = 0;
    /* Distinct row/column colors expose skipped/repeated bytes when a packet
     * begins within one row and continues into the next. Reference conversion
     * uses isolated pixel pairs, independently of packet/row boundaries. */
    for (pixel = 0; pixel < FM1_UVC_WIDTH * FM1_UVC_HEIGHT; ++pixel) {
        unsigned row = pixel / FM1_UVC_WIDTH;
        uint16_t color = (uint16_t)(pixel * 127u + row * 159u);
#if FM1_UVC_HORIZONTAL_PAD
        unsigned column = pixel % FM1_UVC_WIDTH;
        if (column < FM1_UVC_HORIZONTAL_PAD ||
            column >= FM1_UVC_WIDTH - FM1_UVC_HORIZONTAL_PAD)
            color = 0;
#endif
        frame_rgb[pixel * 2u] = (uint8_t)(color >> 8);
        frame_rgb[pixel * 2u + 1u] = (uint8_t)color;
    }
    for (pixel = 0; pixel < FM1_UVC_WIDTH * FM1_UVC_HEIGHT; pixel += 2u)
        assert(fm1_uvc_rgb565be_to_yuy2(frame_yuy2 + pixel * 2u,
                                       frame_rgb + pixel * 2u, 2) == FM1_UVC_OK);
    memset(received_yuy2, 0, sizeof(received_yuy2));
    started(1000);
    assert(packet_count == (FM1_UVC_WIDTH == 256u ? 192u : 180u));
    assert(packet_count * FM1_UVC_PAYLOAD_BYTES == FM1_UVC_FRAME_BYTES);
    assert(packet_count < FM1_UVC_FRAME_PERIOD_MS);
    for (packet = 0; packet < packet_count; ++packet) {
        fm1_uvc_write write;
        fm1_uvc_read read;
        uint32_t offset;
        assert(fm1_uvc_reserve_bytes(&stream, 1000 + packet, &write) == FM1_UVC_OK);
        assert(write.first_byte == byte_count && write.first_byte == packet * 640u);
        assert(write.first_row == write.first_byte / row_bytes);
        assert(write.length == 642 && write.length <= FM1_UVC_ISO_MAX_PACKET);
        offset = write.first_byte % row_bytes;
        assert(offset == (FM1_UVC_WIDTH == 256u ?
                           (packet % 4u) * 128u : (packet % 3u) * 160u));
        assert(!(offset & 3u));
        assert(write.rows == (offset + write.length - 2u + row_bytes - 1u) / row_bytes);
        assert(write.rows <= FM1_UVC_ROWS_PER_PACKET &&
               write.first_row + write.rows <= FM1_UVC_HEIGHT);
        if (offset) ++crossings;
        memcpy(rgb, frame_rgb + write.first_row * row_bytes, write.rows * row_bytes);
        assert(fm1_uvc_rgb565be_to_yuy2(write.bytes + 2, rgb + offset,
                                       (write.length - 2u) / 2u) == FM1_UVC_OK);
        assert(fm1_uvc_publish_bytes(&stream, 1000 + packet, &write) == FM1_UVC_OK);
        assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
        assert(read.bytes[0] == 2);
        assert((read.bytes[1] & FM1_UVC_HEADER_EOH) != 0);
        assert((read.bytes[1] & FM1_UVC_HEADER_ERR) == 0);
        assert((read.bytes[1] & FM1_UVC_HEADER_FID) == fid);
        assert(!!(read.bytes[1] & FM1_UVC_HEADER_EOF) == (packet + 1u == packet_count));
        assert(!memcmp(read.bytes + 2, frame_yuy2 + byte_count, read.length - 2u));
        memcpy(received_yuy2 + byte_count, read.bytes + 2, read.length - 2u);
        byte_count += read.length - 2;
        eof_count += !!(read.bytes[1] & FM1_UVC_HEADER_EOF);
        assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    }
    assert(byte_count == FM1_UVC_FRAME_BYTES);
    assert(!memcmp(received_yuy2, frame_yuy2, FM1_UVC_FRAME_BYTES));
    assert(crossings == (FM1_UVC_WIDTH == 256u ? 144u : 120u));
    assert(eof_count == 1 && stream.next_byte == FM1_UVC_FRAME_BYTES);
    assert(stream.stats.frames_sent == 1 && stream.stats.packets_sent == packet_count);
    assert(!stream.frame_active);
    assert(fm1_uvc_begin_frame(&stream, 1199) == FM1_UVC_WAIT);
    assert(fm1_uvc_begin_frame(&stream, 1200) == FM1_UVC_OK);
    assert(stream.fid == 1 && stream.stats.frames_started == 2);
    assert(fm1_uvc_begin_frame(&stream, 1201) == FM1_UVC_WAIT);
}

static void producer_metadata_and_stop_ownership(void)
{
    fm1_uvc_write write, forged, later;
    fm1_uvc_read read;
    started(0);
    assert(fm1_uvc_reserve_bytes(&stream, 0, 0) == FM1_UVC_INVALID);
    assert(fm1_uvc_reserve_bytes(&stream, 0, &write) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_bytes(&stream, 0, &later) == FM1_UVC_WAIT);
#define INVALID_FIELD(field, expression) do { \
    forged = write; forged.field = (expression); \
    assert(fm1_uvc_publish_bytes(&stream, 0, &forged) == FM1_UVC_INVALID); \
    assert(fm1_uvc_cancel_bytes(&stream, 0, &forged) == FM1_UVC_INVALID); \
    assert(stream.next_byte == 0 && stream.frame_active); \
} while (0)
    INVALID_FIELD(first_byte, write.first_byte + 4u);
    INVALID_FIELD(first_row, (uint16_t)(write.first_row + 1u));
    INVALID_FIELD(rows, 1);
    INVALID_FIELD(length, (uint16_t)(write.length - 4u));
    INVALID_FIELD(token, write.token + 1u);
    INVALID_FIELD(epoch, write.epoch + 1u);
    INVALID_FIELD(frame, write.frame + 1u);
    INVALID_FIELD(slot, FM1_UVC_QUEUE_SLOTS);
    INVALID_FIELD(bytes, write.bytes + 1);
#undef INVALID_FIELD
    memset(write.bytes + 2, 0x5a, write.length - 2u);
    fm1_uvc_stop(&stream);
    fm1_uvc_start(&stream, 100);
    assert(fm1_uvc_begin_frame(&stream, 100) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_bytes(&stream, 100, &later) == FM1_UVC_WAIT);
    assert(write.bytes[2] == 0x5a && write.bytes[641] == 0x5a);
    assert(fm1_uvc_cancel_bytes(&stream, 100, &write) == FM1_UVC_STALE);
    assert(fm1_uvc_cancel_bytes(&stream, 100, &write) == FM1_UVC_INVALID);
    later = offer(100);
    assert(later.epoch != write.epoch && later.first_byte == 0);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
    assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
}

static void queue_backpressure_and_write_retry(void)
{
    fm1_uvc_read first, retry, read;
    fm1_uvc_write write;
    uint8_t retained[FM1_UVC_PACKET_BYTES];
    unsigned i;
    started(0);
    for (i = 0; i < 3; ++i)
        offer(i);
    assert(fm1_uvc_reserve_bytes(&stream, 3, &write) == FM1_UVC_WAIT);
    assert(stream.next_byte == 1920 && stream.stats.backpressure == 1);
    assert(fm1_uvc_acquire(&stream, &first) == FM1_UVC_OK);
    memcpy(retained, first.bytes, first.length);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(fm1_uvc_commit(&stream, &first, 0) == FM1_UVC_RETRY);
    assert(fm1_uvc_acquire(&stream, &retry) == FM1_UVC_OK);
    assert(first.bytes == retry.bytes && first.token == retry.token);
    assert(memcmp(retained, retry.bytes, retry.length) == 0);
    assert(fm1_uvc_commit(&stream, &retry, 1) == FM1_UVC_OK);
    offer(4);  /* Free slot 0 is reused, but must remain last in FIFO. */
    for (i = 1; i < 4; ++i) {
        assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
        assert(read.bytes[2] == (i * FM1_UVC_PAYLOAD_BYTES) / (FM1_UVC_WIDTH * 2u));
        assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    }
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(stream.stats.retries == 1 && stream.stats.packets_sent == 4);
}

static void reset_while_leased_never_reuses_memory(void)
{
    fm1_uvc_read old, forged, read;
    fm1_uvc_write write;
    uint8_t retained[FM1_UVC_PACKET_BYTES];
    started(0);
    offer(0);
    assert(fm1_uvc_acquire(&stream, &old) == FM1_UVC_OK);
    memcpy(retained, old.bytes, old.length);
    fm1_uvc_reset(&stream);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    fm1_uvc_start(&stream, 300);
    assert(fm1_uvc_begin_frame(&stream, 300) == FM1_UVC_OK);
    write = offer(300);
    assert(write.bytes != old.bytes && write.epoch != old.epoch);
    assert(memcmp(retained, old.bytes, old.length) == 0);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    forged = old;
    ++forged.token;
    assert(fm1_uvc_commit(&stream, &forged, 1) == FM1_UVC_INVALID);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(fm1_uvc_commit(&stream, &old, 1) == FM1_UVC_STALE);
    assert(fm1_uvc_commit(&stream, &old, 1) == FM1_UVC_INVALID);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
    assert(read.epoch == stream.epoch && read.epoch != old.epoch);
    assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
}

static void reset_while_converting_preserves_reservation(void)
{
    fm1_uvc_write old, write;
    fm1_uvc_read read;
    started(0);
    assert(fm1_uvc_reserve_bytes(&stream, 0, &old) == FM1_UVC_OK);
    fm1_uvc_reset(&stream);
    fm1_uvc_start(&stream, 100);
    assert(fm1_uvc_begin_frame(&stream, 100) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_bytes(&stream, 100, &write) == FM1_UVC_WAIT);
    memset(old.bytes + 2, 0x5a, old.length - 2);
    assert(fm1_uvc_publish_bytes(&stream, 100, &old) == FM1_UVC_STALE);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    write = offer(100);
    assert(write.epoch != old.epoch);
    assert(fm1_uvc_publish_bytes(&stream, 100, &old) == FM1_UVC_INVALID);
}

static void abort_marks_incomplete_frame_and_discards_queued_rows(void)
{
    fm1_uvc_read old, error, next;
    uint8_t retained[FM1_UVC_PACKET_BYTES];
    unsigned i;
    started(0);
    for (i = 0; i < 3; ++i)
        offer(i);
    assert(fm1_uvc_acquire(&stream, &old) == FM1_UVC_OK);
    memcpy(retained, old.bytes, old.length);
    assert(fm1_uvc_abort_frame(&stream, 30) == FM1_UVC_DROPPED);
    assert(memcmp(retained, old.bytes, old.length) == 0);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_WAIT);
    assert(fm1_uvc_commit(&stream, &old, 1) == FM1_UVC_DROPPED);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_OK);
    assert(error.length == 2);
    assert(error.bytes[1] == (FM1_UVC_HEADER_EOH | FM1_UVC_HEADER_ERR |
                              FM1_UVC_HEADER_EOF));
    assert(fm1_uvc_commit(&stream, &error, 0) == FM1_UVC_RETRY);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_OK);
    assert(fm1_uvc_commit(&stream, &error, 1) == FM1_UVC_OK);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_WAIT);
    assert(stream.stats.frames_dropped == 1 && stream.stats.frames_sent == 0);
    assert(fm1_uvc_begin_frame(&stream, 199) == FM1_UVC_WAIT);
    assert(fm1_uvc_begin_frame(&stream, 200) == FM1_UVC_OK);
    offer(200);
    assert(fm1_uvc_acquire(&stream, &next) == FM1_UVC_OK);
    assert(next.bytes[1] == (FM1_UVC_HEADER_EOH | FM1_UVC_HEADER_FID));
    assert(fm1_uvc_commit(&stream, &next, 1) == FM1_UVC_OK);
}

static void late_publish_and_cancel_emit_error(void)
{
    fm1_uvc_write write;
    fm1_uvc_read error;
    started(0);
    assert(fm1_uvc_reserve_bytes(&stream, 199, &write) == FM1_UVC_OK);
    assert(fm1_uvc_publish_bytes(&stream, 200, &write) == FM1_UVC_STALE);
    assert(stream.stats.frames_dropped == 1);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_OK);
    assert(error.length == 2 && (error.bytes[1] & FM1_UVC_HEADER_ERR));
    assert(fm1_uvc_commit(&stream, &error, 1) == FM1_UVC_OK);
    assert(fm1_uvc_begin_frame(&stream, 200) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_bytes(&stream, 201, &write) == FM1_UVC_OK);
    assert(fm1_uvc_cancel_bytes(&stream, 201, &write) == FM1_UVC_DROPPED);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_OK);
    assert(error.length == 2 && (error.bytes[1] & FM1_UVC_HEADER_FID));
    assert((error.bytes[1] & (FM1_UVC_HEADER_ERR | FM1_UVC_HEADER_EOF)) ==
           (FM1_UVC_HEADER_ERR | FM1_UVC_HEADER_EOF));
    assert(fm1_uvc_commit(&stream, &error, 1) == FM1_UVC_OK);
    assert(stream.stats.frames_dropped == 2);
}

static void deadlines_wrap_and_stop_flushes_ready_packets(void)
{
    fm1_uvc_write write;
    fm1_uvc_read read;
    started(UINT32_MAX - 99u);  /* Deadline wraps to 100 ms. */
    assert(stream.next_frame_ms == 100);
    offer(99);
    assert(fm1_uvc_reserve_bytes(&stream, 100, &write) == FM1_UVC_DROPPED);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
    assert(read.length == 2 && (read.bytes[1] & FM1_UVC_HEADER_ERR));
    assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    assert(fm1_uvc_begin_frame(&stream, 100) == FM1_UVC_OK);
    offer(100);
    fm1_uvc_stop(&stream);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(fm1_uvc_begin_frame(&stream, 500) == FM1_UVC_WAIT);
    assert(fm1_uvc_reserve_bytes(&stream, 500, &write) == FM1_UVC_WAIT);
}

static void color_conversion(void)
{
    static const uint16_t colors[] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f};
    static const uint8_t expected[][4] = {
        {16, 128, 16, 128}, {235, 128, 235, 128}, {82, 90, 82, 240},
        {144, 54, 144, 34}, {41, 240, 41, 110}
    };
    uint8_t rgb[4], yuy2[4];
    unsigned i;
    for (i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        rgb[0] = rgb[2] = (uint8_t)(colors[i] >> 8);
        rgb[1] = rgb[3] = (uint8_t)colors[i];
        assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 2) == FM1_UVC_OK);
        assert(memcmp(yuy2, expected[i], 4) == 0);
    }
    rgb[0] = 0xf8; rgb[1] = 0x00; rgb[2] = 0x00; rgb[3] = 0x1f;
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 2) == FM1_UVC_OK);
    assert(yuy2[0] == 82 && yuy2[2] == 41); /* Independent luma. */
    assert(yuy2[1] == 165 && yuy2[3] == 175); /* Pair-average chroma. */
    for (i = 0; i <= 0xffff; ++i) {
        rgb[0] = rgb[2] = (uint8_t)(i >> 8);
        rgb[1] = rgb[3] = (uint8_t)i;
        assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 2) == FM1_UVC_OK);
        assert(yuy2[0] >= 16 && yuy2[0] <= 235);
        assert(yuy2[1] >= 16 && yuy2[1] <= 240);
        assert(yuy2[3] >= 16 && yuy2[3] <= 240);
    }
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 1) == FM1_UVC_INVALID);
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 0) == FM1_UVC_INVALID);
    assert(fm1_uvc_rgb565be_to_yuy2(0, rgb, 2) == FM1_UVC_INVALID);
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, 0, 2) == FM1_UVC_INVALID);
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb,
           FM1_UVC_WIDTH * FM1_UVC_ROWS_PER_PACKET + 2u) == FM1_UVC_INVALID);
}

int main(void)
{
    complete_frame_and_cadence();
    producer_metadata_and_stop_ownership();
    queue_backpressure_and_write_retry();
    reset_while_leased_never_reuses_memory();
    reset_while_converting_preserves_reservation();
    abort_marks_incomplete_frame_and_discards_queued_rows();
    late_publish_and_cancel_emit_error();
    deadlines_wrap_and_stop_flushes_ready_packets();
    color_conversion();
    printf("UVC stream: 9 focused test groups passed; %ux%u; stream state %zu bytes.\n",
           FM1_UVC_WIDTH, FM1_UVC_HEIGHT, sizeof(fm1_uvc_stream));
    return 0;
}
