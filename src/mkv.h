#ifndef HDR10P_VVC_MKV_H
#define HDR10P_VVC_MKV_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal EBML / Matroska writer.
 *
 * Element IDs are passed as uint32_t values holding the ID bytes in the
 * same order they appear in the file (e.g. 0x18538067 for Segment). The
 * number of bytes written equals the number of leading nonzero bytes of
 * the value as read from the most significant byte (at least one).
 *
 * Matroska element IDs used by the VVC muxer:
 *   EBML header:  0x1A45DFA3 (DocType, DocTypeVersion, DocTypeReadVersion)
 *   Segment:      0x18538067 (Info, Tracks, Cluster)
 *   Info:         0x1549A966 (TimestampScale, MuxingApp, WritingApp)
 *   Tracks:       0x1654AE6B (TrackEntry)
 *   TrackEntry:   0xAE (TrackNumber, TrackUID, TrackType, FlagLacing,
 *                       CodecID, MaxBlockAdditionID, BlockAdditionMapping,
 *                       Video)
 *   Video:        0xE0 (PixelWidth, PixelHeight)
 *   Cluster:      0x1F43B675 (Timestamp, BlockGroup)
 *   BlockGroup:   0xA0 (SimpleBlock, BlockAdditions)
 *   BlockAdditions: 0x75A1 (BlockMore)
 *   BlockMore:    0xA6 (BlockAddID, BlockAdditional)
 */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} mkv_buf;

void mkv_init(mkv_buf *b);
void mkv_free(mkv_buf *b);

/* Append raw bytes. Returns -1 on allocation failure. */
int mkv_append(mkv_buf *b, const void *data, size_t n);
int mkv_u8(mkv_buf *b, uint8_t v);

/* EBML variable-length integer (unknown-size marker not produced). */
int mkv_vint(mkv_buf *b, uint64_t v);

/* Element with an unsigned integer payload of minimal byte length. */
int mkv_uint(mkv_buf *b, uint32_t id, uint64_t v);

/* Element with an ASCII string payload. */
int mkv_str(mkv_buf *b, uint32_t id, const char *s);

/* Element with a binary payload. */
int mkv_bin(mkv_buf *b, uint32_t id, const void *payload, size_t n);

/* Master element: writes the ID and an 8-byte size placeholder, returns the
 * buffer offset for mkv_master_end. */
size_t mkv_master_begin(mkv_buf *b, uint32_t id);
int mkv_master_end(mkv_buf *b, size_t size_pos);

int mkv_write_file(const mkv_buf *b, const char *path);

#endif /* HDR10P_VVC_MKV_H */
