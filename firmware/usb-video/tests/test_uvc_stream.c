#include "uvc_stream.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_uvc_stream stream;

static void started(uint32_t now)
{
    fm1_uvc_init(&stream);
    fm1_uvc_start(&stream, now);
    assert(fm1_uvc_begin_frame(&stream, now) == FM1_UVC_OK);
}

static fm1_uvc_write offer(uint32_t now, uint8_t rows)
{
    fm1_uvc_write write;
    assert(fm1_uvc_reserve_rows(&stream, now, rows, &write) == FM1_UVC_OK);
    memset(write.bytes + 2, (uint8_t)write.first_row, write.length - 2);
    assert(fm1_uvc_publish_rows(&stream, now, &write) == FM1_UVC_OK);
    return write;
}

static void complete_frame_and_cadence(void)
{
    uint8_t rgb[FM1_UVC_WIDTH * 4u] = {0};
    unsigned packet, byte_count = 0, row_count = 0, eof_count = 0;
    uint8_t fid = 0;
    started(1000);
    for (packet = 0; packet < 120; ++packet) {
        fm1_uvc_write write;
        fm1_uvc_read read;
        assert(fm1_uvc_reserve_rows(&stream, 1000 + packet, 2, &write) == FM1_UVC_OK);
        assert(write.first_row == packet * 2);
        assert(write.length == 962 && write.length <= FM1_UVC_ISO_MAX_PACKET);
        assert(fm1_uvc_rgb565be_to_yuy2(write.bytes + 2, rgb, 480) == FM1_UVC_OK);
        assert(fm1_uvc_publish_rows(&stream, 1000 + packet, &write) == FM1_UVC_OK);
        assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
        assert(read.bytes[0] == 2);
        assert((read.bytes[1] & FM1_UVC_HEADER_EOH) != 0);
        assert((read.bytes[1] & FM1_UVC_HEADER_ERR) == 0);
        assert((read.bytes[1] & FM1_UVC_HEADER_FID) == fid);
        assert(!!(read.bytes[1] & FM1_UVC_HEADER_EOF) == (packet == 119));
        assert(read.bytes[2] == 16 && read.bytes[3] == 128);
        byte_count += read.length - 2;
        row_count += write.rows;
        eof_count += !!(read.bytes[1] & FM1_UVC_HEADER_EOF);
        assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    }
    assert(byte_count == 115200 && byte_count == FM1_UVC_FRAME_BYTES);
    assert(row_count == 240 && eof_count == 1);
    assert(stream.stats.frames_sent == 1 && stream.stats.packets_sent == 120);
    assert(!stream.frame_active);
    assert(fm1_uvc_begin_frame(&stream, 1199) == FM1_UVC_WAIT);
    assert(fm1_uvc_begin_frame(&stream, 1200) == FM1_UVC_OK);
    assert(stream.fid == 1 && stream.stats.frames_started == 2);
    assert(fm1_uvc_begin_frame(&stream, 1201) == FM1_UVC_WAIT);
}

static void one_row_packets_and_final_row(void)
{
    unsigned i;
    started(0);
    for (i = 0; i < 240; ++i) {
        fm1_uvc_read read;
        fm1_uvc_write write = offer(i / 2, 1);
        assert(write.length == 482 && write.first_row == i);
        assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
        assert(!!(read.bytes[1] & FM1_UVC_HEADER_EOF) == (i == 239));
        assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    }
    assert(stream.stats.frames_sent == 1);
}

static void queue_backpressure_and_write_retry(void)
{
    fm1_uvc_read first, retry, read;
    fm1_uvc_write write;
    uint8_t retained[FM1_UVC_PACKET_BYTES];
    unsigned i;
    started(0);
    for (i = 0; i < 3; ++i)
        offer(i, 2);
    assert(fm1_uvc_reserve_rows(&stream, 3, 2, &write) == FM1_UVC_WAIT);
    assert(stream.next_row == 6 && stream.stats.backpressure == 1);
    assert(fm1_uvc_acquire(&stream, &first) == FM1_UVC_OK);
    memcpy(retained, first.bytes, first.length);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(fm1_uvc_commit(&stream, &first, 0) == FM1_UVC_RETRY);
    assert(fm1_uvc_acquire(&stream, &retry) == FM1_UVC_OK);
    assert(first.bytes == retry.bytes && first.token == retry.token);
    assert(memcmp(retained, retry.bytes, retry.length) == 0);
    assert(fm1_uvc_commit(&stream, &retry, 1) == FM1_UVC_OK);
    offer(4, 2);  /* Free slot 0 is reused, but must remain last in FIFO. */
    for (i = 1; i < 4; ++i) {
        assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
        assert(read.bytes[2] == i * 2);
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
    offer(0, 2);
    assert(fm1_uvc_acquire(&stream, &old) == FM1_UVC_OK);
    memcpy(retained, old.bytes, old.length);
    fm1_uvc_reset(&stream);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    fm1_uvc_start(&stream, 300);
    assert(fm1_uvc_begin_frame(&stream, 300) == FM1_UVC_OK);
    write = offer(300, 2);
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
    assert(fm1_uvc_reserve_rows(&stream, 0, 2, &old) == FM1_UVC_OK);
    fm1_uvc_reset(&stream);
    fm1_uvc_start(&stream, 100);
    assert(fm1_uvc_begin_frame(&stream, 100) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_rows(&stream, 100, 2, &write) == FM1_UVC_WAIT);
    memset(old.bytes + 2, 0x5a, old.length - 2);
    assert(fm1_uvc_publish_rows(&stream, 100, &old) == FM1_UVC_STALE);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    write = offer(100, 2);
    assert(write.epoch != old.epoch);
    assert(fm1_uvc_publish_rows(&stream, 100, &old) == FM1_UVC_INVALID);
}

static void abort_marks_incomplete_frame_and_discards_queued_rows(void)
{
    fm1_uvc_read old, error, next;
    uint8_t retained[FM1_UVC_PACKET_BYTES];
    unsigned i;
    started(0);
    for (i = 0; i < 3; ++i)
        offer(i, 2);
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
    offer(200, 2);
    assert(fm1_uvc_acquire(&stream, &next) == FM1_UVC_OK);
    assert(next.bytes[1] == (FM1_UVC_HEADER_EOH | FM1_UVC_HEADER_FID));
    assert(fm1_uvc_commit(&stream, &next, 1) == FM1_UVC_OK);
}

static void late_publish_and_cancel_emit_error(void)
{
    fm1_uvc_write write;
    fm1_uvc_read error;
    started(0);
    assert(fm1_uvc_reserve_rows(&stream, 199, 2, &write) == FM1_UVC_OK);
    assert(fm1_uvc_publish_rows(&stream, 200, &write) == FM1_UVC_STALE);
    assert(stream.stats.frames_dropped == 1);
    assert(fm1_uvc_acquire(&stream, &error) == FM1_UVC_OK);
    assert(error.length == 2 && (error.bytes[1] & FM1_UVC_HEADER_ERR));
    assert(fm1_uvc_commit(&stream, &error, 1) == FM1_UVC_OK);
    assert(fm1_uvc_begin_frame(&stream, 200) == FM1_UVC_OK);
    assert(fm1_uvc_reserve_rows(&stream, 201, 2, &write) == FM1_UVC_OK);
    assert(fm1_uvc_cancel_rows(&stream, 201, &write) == FM1_UVC_DROPPED);
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
    offer(99, 2);
    assert(fm1_uvc_reserve_rows(&stream, 100, 2, &write) == FM1_UVC_DROPPED);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_OK);
    assert(read.length == 2 && (read.bytes[1] & FM1_UVC_HEADER_ERR));
    assert(fm1_uvc_commit(&stream, &read, 1) == FM1_UVC_OK);
    assert(fm1_uvc_begin_frame(&stream, 100) == FM1_UVC_OK);
    offer(100, 2);
    fm1_uvc_stop(&stream);
    assert(fm1_uvc_acquire(&stream, &read) == FM1_UVC_WAIT);
    assert(fm1_uvc_begin_frame(&stream, 500) == FM1_UVC_WAIT);
    assert(fm1_uvc_reserve_rows(&stream, 500, 2, &write) == FM1_UVC_WAIT);
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
    assert(fm1_uvc_rgb565be_to_yuy2(yuy2, rgb, 482) == FM1_UVC_INVALID);
}

int main(void)
{
    complete_frame_and_cadence();
    one_row_packets_and_final_row();
    queue_backpressure_and_write_retry();
    reset_while_leased_never_reuses_memory();
    reset_while_converting_preserves_reservation();
    abort_marks_incomplete_frame_and_discards_queued_rows();
    late_publish_and_cancel_emit_error();
    deadlines_wrap_and_stop_flushes_ready_packets();
    color_conversion();
    printf("UVC stream: 9 focused test groups passed; stream state %zu bytes.\n",
           sizeof(fm1_uvc_stream));
    return 0;
}
