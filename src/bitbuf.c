#include "bitbuf.h"

#include <stdlib.h>
#include <string.h>

void bw_init(bit_writer *w)
{
    w->data  = NULL;
    w->len   = 0;
    w->cap   = 0;
    w->acc   = 0;
    w->nbits = 0;
}

void bw_free(bit_writer *w)
{
    free(w->data);
    w->data = NULL;
    w->cap  = w->len = 0;
    w->acc  = 0;
    w->nbits = 0;
}

static int bw_grow(bit_writer *w, size_t need)
{
    if (need <= w->cap)
        return 0;
    size_t ncap = w->cap ? w->cap : 64;
    while (ncap < need)
        ncap *= 2;
    uint8_t *nd = realloc(w->data, ncap);
    if (!nd)
        return -1;
    w->data = nd;
    w->cap  = ncap;
    return 0;
}

static int bw_flush_byte(bit_writer *w)
{
    if (bw_grow(w, w->len + 1) < 0)
        return -1;
    w->data[w->len++] = (uint8_t)(w->acc & 0xFF);
    w->acc            = 0;
    w->nbits          = 0;
    return 0;
}

int bw_write(bit_writer *w, uint32_t value, int nbits)
{
    if (nbits < 0 || nbits > 32)
        return -1;
    for (int i = nbits - 1; i >= 0; i--) {
        w->acc = (w->acc << 1) | ((value >> i) & 1U);
        if (++w->nbits == 8) {
            if (bw_flush_byte(w) < 0)
                return -1;
        }
    }
    return 0;
}

void bw_align(bit_writer *w)
{
    while (w->nbits > 0) {
        w->acc <<= 1;
        w->nbits++;
        if (w->nbits == 8) {
            /* zero-fill on flush */
            if (bw_flush_byte(w) < 0)
                return; /* best effort; out-of-memory path */
        }
    }
}

uint8_t *bw_bytes(bit_writer *w, size_t *out_len)
{
    bw_align(w);
    uint8_t *out = NULL;
    if (w->len > 0) {
        out = malloc(w->len);
        if (!out)
            return NULL;
        memcpy(out, w->data, w->len);
    }
    if (out_len)
        *out_len = w->len;
    return out;
}

void br_init(bit_reader *r, const uint8_t *data, size_t size)
{
    r->data   = data;
    r->size   = size;
    r->bitpos = 0;
}

int br_remaining_bits(const bit_reader *r)
{
    return (int)(r->size * 8 - r->bitpos);
}

int br_byte_aligned(const bit_reader *r)
{
    return (r->bitpos & 7) == 0;
}

size_t br_consumed_bytes(const bit_reader *r)
{
    return (r->bitpos + 7) / 8;
}

int br_bit(bit_reader *r, int *value)
{
    if (r->bitpos >= r->size * 8)
        return -1;
    *value = (r->data[r->bitpos >> 3] >> (7 - (r->bitpos & 7))) & 1;
    r->bitpos++;
    return 0;
}

int br_read(bit_reader *r, uint32_t *value, int nbits)
{
    if (nbits < 0 || nbits > 32)
        return -1;
    if (r->bitpos + (size_t)nbits > r->size * 8)
        return -1;
    uint32_t v = 0;
    for (int i = 0; i < nbits; i++) {
        int b;
        if (br_bit(r, &b) < 0)
            return -1;
        v = (v << 1) | (uint32_t)b;
    }
    *value = v;
    return 0;
}

int br_read_ue(bit_reader *r, uint32_t *value)
{
    int leading = 0;
    while (br_remaining_bits(r) > 0) {
        int b;
        if (br_bit(r, &b) < 0)
            return -1;
        if (b)
            break;
        leading++;
        if (leading > 31)
            return -1;
    }
    if (leading == 31 && br_remaining_bits(r) == 0)
        return -1;
    uint32_t suffix = 0;
    if (leading > 0) {
        if (br_read(r, &suffix, leading) < 0)
            return -1;
    }
    *value = (uint32_t)((1u << leading) - 1u) + suffix;
    return 0;
}

int br_read_se(bit_reader *r, int32_t *value)
{
    uint32_t code_num;
    if (br_read_ue(r, &code_num) < 0)
        return -1;
    if (code_num == 0) {
        *value = 0;
    } else if (code_num & 1) {
        *value = (int32_t)((code_num + 1) / 2); /* positive */
    } else {
        *value = -(int32_t)(code_num / 2);      /* negative */
    }
    return 0;
}
