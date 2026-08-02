#include "vvc.h"

#include <stdlib.h>
#include <string.h>

static int is_vcl_type(int t)
{
    return (t >= VVC_TRAIL_NUT && t <= VVC_RSV_VCL_6) ||
           (t >= VVC_IDR_W_RADL && t <= VVC_RSV_IRAP_11) ||
           t >= VVC_UNSPEC_29;
}

static int is_irap_type(int t)
{
    return t == VVC_IDR_W_RADL || t == VVC_IDR_N_LP || t == VVC_CRA_NUT ||
           t == VVC_RSV_IRAP_11;
}

static void parse_nal_header(const uint8_t *d, size_t len, vvc_nal *nal)
{
    nal->data = d;
    nal->len = len;
    if (len < 2)
        return;
    nal->nuh_layer_id = (uint8_t)(d[0] & 0x3F);
    nal->nal_unit_type = (uint8_t)((d[1] >> 3) & 0x1F);
    nal->nuh_temporal_id_plus1 = (uint8_t)(d[1] & 0x07);
    nal->is_vcl = is_vcl_type(nal->nal_unit_type);
    nal->is_irap = is_irap_type(nal->nal_unit_type);
    nal->is_gdr = (nal->nal_unit_type == VVC_GDR_NUT);
}

static int nal_list_push(vvc_nal_list *l, const vvc_nal *nal)
{
    if (l->n == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 128;
        vvc_nal *nn = realloc(l->nals, sizeof(vvc_nal) * (size_t)ncap);
        if (!nn)
            return -1;
        l->nals = nn;
        l->cap = ncap;
    }
    l->nals[l->n++] = *nal;
    return 0;
}

void vvc_nal_list_free(vvc_nal_list *l)
{
    free(l->nals);
    l->nals = NULL;
    l->n = l->cap = 0;
}

static int find_start_code(const uint8_t *buf, size_t len, size_t from,
                           size_t *start, uint8_t *sc_len)
{
    while (from + 2 < len) {
        if (buf[from] == 0 && buf[from + 1] == 0 && buf[from + 2] == 1) {
            if (from >= 1 && buf[from - 1] == 0) {
                *start = from - 1;
                *sc_len = 4;
            } else {
                *start = from;
                *sc_len = 3;
            }
            return 0;
        }
        from++;
    }
    return -1;
}

int vvc_scan_stream(const uint8_t *buf, size_t len, vvc_nal_list *out)
{
    size_t sc = 0;
    uint8_t scl = 0;
    if (find_start_code(buf, len, 0, &sc, &scl) < 0)
        return -1; /* not an Annex B byte stream */

    size_t nal_start = sc + scl;
    uint8_t nal_scl = scl;
    size_t pos = nal_start;

    while (pos < len) {
        size_t ns = 0;
        uint8_t nsl = 0;
        if (find_start_code(buf, len, pos, &ns, &nsl) < 0) {
            if (len > nal_start) {
                vvc_nal nal;
                memset(&nal, 0, sizeof(nal));
                parse_nal_header(buf + nal_start, len - nal_start, &nal);
                nal.start = nal_start;
                nal.start_code_len = nal_scl;
                if (nal_list_push(out, &nal) < 0)
                    return -1;
            }
            break;
        }
        if (ns > nal_start) {
            vvc_nal nal;
            memset(&nal, 0, sizeof(nal));
            parse_nal_header(buf + nal_start, ns - nal_start, &nal);
            nal.start = nal_start;
            nal.start_code_len = nal_scl;
            if (nal_list_push(out, &nal) < 0)
                return -1;
        }
        nal_start = ns + nsl;
        nal_scl = nsl;
        pos = nal_start;
    }
    return 0;
}

size_t vvc_rbsp_from_nal(const uint8_t *nal, size_t len, uint8_t *out)
{
    size_t o = 0;
    int zeros = 0;
    for (size_t i = 0; i < len; i++) {
        if (zeros >= 2 && nal[i] == 0x03) {
            if (i + 1 < len && nal[i + 1] <= 0x03) {
                zeros = 0;
                continue;
            }
        }
        if (nal[i] == 0)
            zeros++;
        else
            zeros = 0;
        out[o++] = nal[i];
    }
    return o;
}

size_t vvc_nal_from_rbsp(const uint8_t *rbsp, size_t len, uint8_t *out)
{
    size_t o = 0;
    int zeros = 0;
    for (size_t i = 0; i < len; i++) {
        if (zeros >= 2 && rbsp[i] <= 0x03) {
            out[o++] = 0x03;
            zeros = 0;
        }
        if (rbsp[i] == 0)
            zeros++;
        else
            zeros = 0;
        out[o++] = rbsp[i];
    }
    return o;
}

static size_t sei_write_size_field(uint8_t *dst, uint32_t value)
{
    size_t o = 0;
    while (value >= 0xFF) {
        dst[o++] = 0xFF;
        value -= 0xFF;
    }
    dst[o++] = (uint8_t)value;
    return o;
}

int vvc_parse_sei(const uint8_t *rbsp, size_t len, vvc_sei_msg *msgs, int max)
{
    size_t idx = 0;
    int count = 0;
    while (idx < len) {
        /* Stop at rbsp_trailing_bits: a single 0x80 followed by zeros, or all
         * zeros (some encoders pad with extra zero bytes). */
        size_t rest = len - idx;
        int all_zero = 1;
        for (size_t i = idx; i < len; i++) {
            if (rbsp[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
            break;
        if (rest == 1 && rbsp[idx] == 0x80)
            break;
        if (rest >= 2 && rbsp[idx] == 0x80 && rbsp[idx + 1] == 0)
            break;

        uint32_t payload_type = 0;
        while (idx < len && rbsp[idx] == 0xFF) {
            payload_type += 0xFF;
            idx++;
        }
        if (idx >= len)
            break;
        payload_type += rbsp[idx++];

        uint32_t payload_size = 0;
        while (idx < len && rbsp[idx] == 0xFF) {
            payload_size += 0xFF;
            idx++;
        }
        if (idx >= len)
            break;
        payload_size += rbsp[idx++];

        if (idx + payload_size > len)
            break;

        if (count < max) {
            msgs[count].payload_type = payload_type;
            msgs[count].payload_size = payload_size;
            msgs[count].payload_offset = idx;
            count++;
        }
        idx += payload_size;
    }
    return count;
}

int vvc_build_hdr10p_nal(const uint8_t *st2094_payload, size_t payload_len,
                         uint8_t nuh_layer_id, uint8_t nuh_temporal_id_plus1,
                         uint8_t **out, size_t *out_len)
{
    size_t hdr = 2;
    size_t o = hdr;
    /* payload type field: at most 2 bytes, size field: at most 5 bytes */
    uint8_t type_field[2], size_field[5];
    size_t type_len = sei_write_size_field(type_field, VVC_SEI_USER_DATA_REGISTERED_ITU_T_T35);
    size_t size_len = sei_write_size_field(size_field, (uint32_t)payload_len);
    o += type_len + size_len;

    size_t rbsp_len = o + payload_len + 1; /* + rbsp_trailing_bits byte */
    uint8_t *rbsp = malloc(rbsp_len);
    uint8_t *nal = NULL;
    if (!rbsp)
        return -1;

    rbsp[0] = (uint8_t)(nuh_layer_id & 0x3F); /* forbidden=0, reserved=0 */
    rbsp[1] = (uint8_t)((VVC_PREFIX_SEI_NUT << 3) | (nuh_temporal_id_plus1 & 0x07));

    size_t p = 2;
    memcpy(rbsp + p, type_field, type_len);
    p += type_len;
    memcpy(rbsp + p, size_field, size_len);
    p += size_len;
    memcpy(rbsp + p, st2094_payload, payload_len);
    p += payload_len;
    rbsp[p++] = 0x80; /* rbsp_trailing_bits */

    uint8_t *scratch = malloc(rbsp_len * 2 + 4);
    if (!scratch) {
        free(rbsp);
        return -1;
    }
    size_t nal_len = vvc_nal_from_rbsp(rbsp, p, scratch);
    nal = malloc(nal_len);
    if (!nal) {
        free(rbsp);
        free(scratch);
        return -1;
    }
    memcpy(nal, scratch, nal_len);
    free(rbsp);
    free(scratch);
    *out = nal;
    *out_len = nal_len;
    return 0;
}

int vvc_compute_aus(const vvc_nal_list *nals, int *au_start, int *au_count)
{
    int has_aud_or_ph = 0;
    for (int i = 0; i < nals->n; i++) {
        if (nals->nals[i].nal_unit_type == VVC_AUD_NUT ||
            nals->nals[i].nal_unit_type == VVC_PH_NUT)
            has_aud_or_ph = 1;
    }

    int count = 0;
    int seen_vcl = 0;
    for (int i = 0; i < nals->n; i++) {
        const vvc_nal *n = &nals->nals[i];
        if (!n->is_vcl)
            continue;
        int new_au = 0;
        if (!seen_vcl)
            new_au = 1;                       /* first VCL of stream */
        else if (n->is_irap || n->is_gdr)
            new_au = 1;
        else if (!nals->nals[i - 1].is_vcl)
            new_au = 1;                       /* separated by non-VCL NAL */
        else if (!has_aud_or_ph)
            new_au = 1;                       /* assume one slice per picture */
        else
            new_au = 0;                       /* continuation slice */
        if (new_au) {
            au_start[count++] = i;
            if (count > nals->n)
                break;
        }
        seen_vcl = 1;
    }
    *au_count = count;
    return 0;
}
