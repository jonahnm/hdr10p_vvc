#ifndef HDR10P_VVC_BITBUF_H
#define HDR10P_VVC_BITBUF_H

#include <stddef.h>
#include <stdint.h>

/*
 * MSB-first bit writer backed by a growable byte buffer.
 */
typedef struct {
    uint8_t *data;    /* buffer, allocated size == cap */
    size_t   len;     /* number of bytes fully written */
    size_t   cap;     /* allocated capacity */
    uint32_t acc;     /* pending bits, left-justified (MSB in bit 31) */
    int      nbits;   /* number of valid bits in acc (0..7) */
} bit_writer;

/* MSB-first bit reader over a fixed buffer. */
typedef struct {
    const uint8_t *data;
    size_t         size;   /* bytes available */
    size_t         bitpos; /* current bit position, 0-based */
} bit_reader;

void bw_init(bit_writer *w);
void bw_free(bit_writer *w);
int  bw_write(bit_writer *w, uint32_t value, int nbits);
/* Pad with zeros until the next byte boundary; call before reading bytes out. */
void bw_align(bit_writer *w);
/* Flush pending bits, return a freshly allocated copy of the bytes written. */
uint8_t *bw_bytes(bit_writer *w, size_t *out_len);

void br_init(bit_reader *r, const uint8_t *data, size_t size);
int  br_read(bit_reader *r, uint32_t *value, int nbits);
int  br_bit(bit_reader *r, int *value);
/* Exp-Golomb codes (H.266 7.3.2): unsigned and signed. */
int  br_read_ue(bit_reader *r, uint32_t *value);
int  br_read_se(bit_reader *r, int32_t *value);
int  br_remaining_bits(const bit_reader *r);
/* True if at a byte boundary. */
int  br_byte_aligned(const bit_reader *r);
/* number of complete bytes consumed + whether a partial byte is started */
size_t br_consumed_bytes(const bit_reader *r);

#endif /* HDR10P_VVC_BITBUF_H */
