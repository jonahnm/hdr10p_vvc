#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mkv.h"

void mkv_init(mkv_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void mkv_free(mkv_buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int mkv_grow(mkv_buf *b, size_t need)
{
    if (b->len + need <= b->cap)
        return 0;
    size_t ncap = b->cap ? b->cap : 4096;
    while (ncap < b->len + need)
        ncap *= 2;
    uint8_t *nd = realloc(b->data, ncap);
    if (!nd)
        return -1;
    b->data = nd;
    b->cap = ncap;
    return 0;
}

int mkv_append(mkv_buf *b, const void *data, size_t n)
{
    if (mkv_grow(b, n) < 0)
        return -1;
    memcpy(b->data + b->len, data, n);
    b->len += n;
    return 0;
}

int mkv_u8(mkv_buf *b, uint8_t v)
{
    return mkv_append(b, &v, 1);
}

int mkv_vint(mkv_buf *b, uint64_t v)
{
    /* Minimal length: n bytes of payload can hold values < 2^(7n) - 1. */
    int sz = 1;
    while (sz < 8 && v >= ((uint64_t)1 << (7 * sz)) - 1)
        sz++;
    uint64_t mask = (uint64_t)0x80 >> (sz - 1);
    uint64_t value;
    if (v < mask - 1) {
        value = v | mask;
    } else {
        value = v | (mask << (8 * (sz - 1)));
    }
    uint8_t tmp[8];
    for (int i = sz - 1; i >= 0; i--) {
        tmp[i] = (uint8_t)value;
        value >>= 8;
    }
    return mkv_append(b, tmp, (size_t)sz);
}

static int mkv_id_bytes(uint32_t id, uint8_t *out)
{
    if (id & 0xFF000000u) {
        out[0] = (uint8_t)(id >> 24);
        out[1] = (uint8_t)(id >> 16);
        out[2] = (uint8_t)(id >> 8);
        out[3] = (uint8_t)id;
        return 4;
    }
    if (id & 0x00FF0000u) {
        out[0] = (uint8_t)(id >> 16);
        out[1] = (uint8_t)(id >> 8);
        out[2] = (uint8_t)id;
        return 3;
    }
    if (id & 0x0000FF00u) {
        out[0] = (uint8_t)(id >> 8);
        out[1] = (uint8_t)id;
        return 2;
    }
    out[0] = (uint8_t)id;
    return 1;
}

static int mkv_elem(mkv_buf *b, uint32_t id, const void *payload, size_t n)
{
    uint8_t idb[4];
    int idl = mkv_id_bytes(id, idb);
    if (mkv_append(b, idb, (size_t)idl) < 0)
        return -1;
    if (mkv_vint(b, (uint64_t)n) < 0)
        return -1;
    if (n && mkv_append(b, payload, n) < 0)
        return -1;
    return 0;
}

int mkv_uint(mkv_buf *b, uint32_t id, uint64_t v)
{
    int nb = 1;
    while (nb < 8 && v >= ((uint64_t)1 << (8 * nb)))
        nb++;
    uint8_t tmp[8];
    for (int i = nb - 1; i >= 0; i--) {
        tmp[i] = (uint8_t)v;
        v >>= 8;
    }
    return mkv_elem(b, id, tmp, (size_t)nb);
}

int mkv_str(mkv_buf *b, uint32_t id, const char *s)
{
    return mkv_elem(b, id, s, strlen(s));
}

int mkv_bin(mkv_buf *b, uint32_t id, const void *payload, size_t n)
{
    return mkv_elem(b, id, payload, n);
}

size_t mkv_master_begin(mkv_buf *b, uint32_t id)
{
    uint8_t idb[4];
    int idl = mkv_id_bytes(id, idb);
    mkv_append(b, idb, (size_t)idl);
    uint8_t tmp[8] = { 0 };
    mkv_append(b, tmp, sizeof(tmp));
    return b->len;
}

int mkv_master_end(mkv_buf *b, size_t size_pos)
{
    size_t body = b->len - size_pos;
    if (body >= ((size_t)1 << 56))
        return -1;
    uint8_t vint[8];
    /* Encode the size as a 8-byte vint (reserved at begin). */
    uint64_t v = (uint64_t)body | ((uint64_t)0x01 << 56);
    for (int i = 7; i >= 0; i--) {
        vint[i] = (uint8_t)v;
        v >>= 8;
    }
    memcpy(b->data + size_pos - 8, vint, 8);
    return 0;
}

int mkv_write_file(const mkv_buf *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (b->len && fwrite(b->data, 1, b->len, f) != b->len) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
        return -1;
    return 0;
}
