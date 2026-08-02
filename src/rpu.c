#include "rpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 0x19: the constant RPU payload prefix byte written by dovi_tool. */
#define RPU_PAYLOAD_PREFIX 0x19

static int find_start_code(const uint8_t *b, size_t len, size_t from,
                           size_t *start, uint8_t *scl)
{
    while (from + 2 < len) {
        if (b[from] == 0 && b[from + 1] == 0 && b[from + 2] == 1) {
            if (from >= 1 && b[from - 1] == 0) {
                *start = from - 1;
                *scl = 4;
            } else {
                *start = from;
                *scl = 3;
            }
            return 0;
        }
        from++;
    }
    return -1;
}

int rpu_parse_file(const uint8_t *buf, size_t len, rpu_frame **out, int *out_count)
{
    size_t *scs = malloc(sizeof(size_t) * (len / 4 + 2));
    uint8_t *scls = malloc(sizeof(uint8_t) * (len / 4 + 2));
    if (!scs || !scls) {
        free(scs);
        free(scls);
        return -1;
    }

    int n_sc = 0;
    size_t p = 0;
    while (p < len) {
        size_t sc;
        uint8_t scl;
        if (find_start_code(buf, len, p, &sc, &scl) < 0)
            break;
        scs[n_sc] = sc;
        scls[n_sc] = scl;
        n_sc++;
        p = sc + scl;
    }

    if (n_sc < 1) {
        fprintf(stderr, "error: RPU file contains no start codes\n");
        free(scs);
        free(scls);
        return -1;
    }

    rpu_frame *frames = NULL;
    int n = 0, cap = 0;

    for (int i = 0; i < n_sc; i++) {
        size_t start = scs[i] + scls[i];
        size_t end = (i + 1 < n_sc) ? scs[i + 1] : len;
        if (end <= start)
            continue;

        const uint8_t *d = buf + start;
        size_t dl = end - start;

        /* Strip a HEVC UNSPEC62 NAL header (0x7C 0x01) if present. */
        if (dl >= 2 && d[0] == 0x7C && d[1] == 0x01) {
            d += 2;
            dl -= 2;
        }
        if (dl == 0)
            continue;
        if (d[0] != RPU_PAYLOAD_PREFIX) {
            fprintf(stderr,
                    "error: invalid RPU payload at index %d (expected 0x%02X prefix, got 0x%02X)\n",
                    i, RPU_PAYLOAD_PREFIX, d[0]);
            free(scs);
            free(scls);
            for (int j = 0; j < n; j++)
                free(frames[j].data);
            free(frames);
            return -1;
        }

        if (n == cap) {
            int ncap = cap ? cap * 2 : 64;
            rpu_frame *nf = realloc(frames, sizeof(rpu_frame) * (size_t)ncap);
            if (!nf) {
                free(scs);
                free(scls);
                for (int j = 0; j < n; j++)
                    free(frames[j].data);
                free(frames);
                return -1;
            }
            frames = nf;
            cap = ncap;
        }
        frames[n].data = malloc(dl);
        if (!frames[n].data) {
            free(scs);
            free(scls);
            for (int j = 0; j < n; j++)
                free(frames[j].data);
            free(frames);
            return -1;
        }
        memcpy(frames[n].data, d, dl);
        frames[n].len = dl;
        n++;
    }

    free(scs);
    free(scls);

    *out = frames;
    *out_count = n;
    return 0;
}

void rpu_frames_free(rpu_frame *frames, int n)
{
    for (int i = 0; i < n; i++)
        free(frames[i].data);
    free(frames);
}

int rpu_build_vvc_nal(const rpu_frame *frame, uint8_t nal_type, uint8_t tid_plus_1,
                      uint8_t **out, size_t *out_len)
{
    uint8_t *nal = malloc(frame->len + 2);
    if (!nal)
        return -1;
    /* forbidden_zero_bit=0, nuh_reserved_zero_bit=0, nuh_layer_id=0 */
    nal[0] = 0x00;
    /* nal_unit_type (5 bits) | nuh_temporal_id_plus1 (3 bits) */
    nal[1] = (uint8_t)(((nal_type & 0x1F) << 3) | (tid_plus_1 & 0x07));
    memcpy(nal + 2, frame->data, frame->len);
    *out = nal;
    *out_len = frame->len + 2;
    return 0;
}
