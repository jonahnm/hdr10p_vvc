#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "st2094.h"
#include "vvc.h"
#include "vvc_poc.h"
#include "rpu.h"

#define PROGRAM "hdr10p_vvc"
#define VERSION "1.0.0"

static void usage(FILE *f)
{
    fprintf(f,
        "%s %s - embed/remove/extract HDR10+ (SMPTE ST 2094-40) metadata in VVC streams\n"
        "\n"
        "Usage:\n"
        "  %s inject -i <input.vvc> [-j <metadata.json>] [-r <rpu.bin>] -o <output.vvc>\n"
        "  %s extract -i <input.vvc> [-o <metadata.json>]\n"
        "  %s remove  -i <input.vvc> [-o <output.vvc>]\n"
        "\n"
        "Commands:\n"
        "  inject   Interleave HDR10+ prefix SEI NAL units before the first slice of\n"
        "           each access unit and/or Dolby Vision RPU NAL units after the last\n"
        "           NAL unit of each access unit. Existing HDR10+ / RPU data is\n"
        "           replaced. Metadata is associated in presentation (display) order,\n"
        "           like hdr10plus_tool / dovi_tool.\n"
        "  extract  Read HDR10+ metadata out of the stream and write a JSON file\n"
        "           compatible with hdr10plus_tool.\n"
        "  remove   Strip all HDR10+ SEI messages and Dolby Vision RPU NAL units.\n"
        "\n"
        "Options:\n"
        "  -i FILE   input VVC bitstream (Annex B byte stream, e.g. -f vvc)\n"
        "  -o FILE   output file (default: output.vvc / extracted.json)\n"
        "  -j FILE   HDR10+ metadata JSON produced by hdr10plus_tool\n"
        "  -r FILE   Dolby Vision RPU binary (dovi_tool extract output)\n"
        "  --rpu-nal-type N  VVC NAL unit type carrying the RPU (default 27)\n"
        "  --no-reorder  (inject) associate metadata in decode order instead of\n"
        "           presentation order\n"
        "  -h        show this help\n",
        PROGRAM, VERSION, PROGRAM, PROGRAM, PROGRAM);
}

static int read_file(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *data = buf;
    *len = rd;
    return 0;
}

typedef struct {
    uint8_t *data;
    size_t   len;
    uint8_t  sc_len;  /* 3 or 4 */
    int      owned;
} write_item;

static void write_items_free(write_item *items, int n)
{
    for (int i = 0; i < n; i++)
        if (items[i].owned)
            free(items[i].data);
    free(items);
}

static int write_stream(FILE *out, const write_item *items, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t sc4[4] = { 0, 0, 0, 1 };
        const uint8_t sc3[3] = { 0, 0, 1 };
        const uint8_t *sc = items[i].sc_len == 4 ? sc4 : sc3;
        size_t scl = items[i].sc_len == 4 ? 4 : 3;
        if (fwrite(sc, 1, scl, out) != scl)
            return -1;
        if (fwrite(items[i].data, 1, items[i].len, out) != items[i].len)
            return -1;
    }
    return 0;
}

/*
 * Remove ST 2094-40 messages from a prefix SEI NAL unit.
 * Returns 0 if the NAL contains no HDR10+ message (unchanged),
 * 1 if rewritten (out/out_len set, *drop set when the NAL should be removed).
 */
static int strip_hdr10p_from_sei(const uint8_t *nal, size_t len,
                                 uint8_t **out, size_t *out_len, int *drop)
{
    uint8_t *rbsp = malloc(len);
    if (!rbsp)
        return -1;
    size_t rlen = vvc_rbsp_from_nal(nal, len, rbsp);
    if (rlen < 2) {
        free(rbsp);
        return 0;
    }

    vvc_sei_msg msgs[32];
    int n = vvc_parse_sei(rbsp + 2, rlen - 2, msgs, 32);

    int has_hdr10p = 0;
    for (int i = 0; i < n; i++) {
        if (msgs[i].payload_type == VVC_SEI_USER_DATA_REGISTERED_ITU_T_T35 &&
            st2094_detect(rbsp + 2 + msgs[i].payload_offset, msgs[i].payload_size)) {
            has_hdr10p = 1;
            break;
        }
    }
    if (!has_hdr10p) {
        free(rbsp);
        return 0;
    }

    /* Rebuild RBSP with the remaining messages. */
    size_t cap = rlen + 8;
    uint8_t *nb = malloc(cap);
    if (!nb) {
        free(rbsp);
        return -1;
    }
    size_t o = 0;
    nb[o++] = rbsp[0];
    nb[o++] = rbsp[1];
    for (int i = 0; i < n; i++) {
        if (msgs[i].payload_type == VVC_SEI_USER_DATA_REGISTERED_ITU_T_T35 &&
            st2094_detect(rbsp + 2 + msgs[i].payload_offset, msgs[i].payload_size))
            continue;
        uint32_t t = msgs[i].payload_type;
        while (t >= 0xFF) { nb[o++] = 0xFF; t -= 0xFF; }
        nb[o++] = (uint8_t)t;
        uint32_t s = msgs[i].payload_size;
        while (s >= 0xFF) { nb[o++] = 0xFF; s -= 0xFF; }
        nb[o++] = (uint8_t)s;
        if (o + msgs[i].payload_size > cap) {
            free(rbsp);
            free(nb);
            return -1;
        }
        memcpy(nb + o, rbsp + 2 + msgs[i].payload_offset, msgs[i].payload_size);
        o += msgs[i].payload_size;
    }
    free(rbsp);

    if (o == 2) {
        *drop = 1;
        free(nb);
        return 1;
    }

    uint8_t *nalbuf = malloc(o * 2 + 8);
    if (!nalbuf) {
        free(nb);
        return -1;
    }
    nb[o++] = 0x80; /* rbsp_trailing_bits */
    size_t nal_len = vvc_nal_from_rbsp(nb, o, nalbuf);
    free(nb);
    *out = nalbuf;
    *out_len = nal_len;
    *drop = 0;
    return 1;
}

/* ---------- inject ---------- */

/* True if the NAL unit looks like a Dolby Vision RPU (payload starts 0x19). */
static int nal_is_rpu(const vvc_nal *nal, int nal_type)
{
    return nal->nal_unit_type == (uint8_t)nal_type &&
           nal->len >= 3 && nal->data[2] == 0x19;
}

/*
 * For every access unit, find the index of its last NAL unit (the last VCL
 * or suffix SEI NAL before the next access unit's prefix NAL units). The
 * Dolby Vision RPU NAL is inserted right after it, as the last NAL of the AU.
 */
static void compute_au_last_nal(const vvc_nal_list *nals, const int *au_start,
                                int au_count, int *au_last)
{
    for (int k = 0; k < au_count; k++) {
        int range_end = (k + 1 < au_count) ? au_start[k + 1] : nals->n;
        au_last[k] = au_start[k];
        for (int j = au_start[k]; j < range_end; j++) {
            const vvc_nal *n = &nals->nals[j];
            if (n->is_vcl || n->nal_unit_type == VVC_SUFFIX_SEI_NUT)
                au_last[k] = j;
        }
    }
}

static int cmd_inject(int argc, char **argv)
{
    const char *in_path = NULL, *json_path = NULL, *rpu_path = NULL, *out_path = "output.vvc";
    int reorder = 1;
    int rpu_nal_type = VVC_DEFAULT_RPU_NAL_TYPE;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in_path = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) json_path = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rpu_path = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "--no-reorder")) reorder = 0;
        else if (!strcmp(argv[i], "--rpu-nal-type") && i + 1 < argc) {
            char *endp = NULL;
            long v = strtol(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || v < 0 || v > 31) {
                fprintf(stderr, "error: --rpu-nal-type must be a NAL unit type in 0..31\n");
                return 1;
            }
            rpu_nal_type = (int)v;
        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    if (!in_path || (!json_path && !rpu_path)) {
        fprintf(stderr, "error: inject requires -i and at least one of -j (HDR10+) or -r (Dolby Vision RPU)\n");
        return 1;
    }

    uint8_t *buf = NULL;
    size_t buflen = 0;
    if (read_file(in_path, &buf, &buflen) < 0)
        return 1;

    vvc_nal_list nals = { 0 };
    if (vvc_scan_stream(buf, buflen, &nals) < 0) {
        fprintf(stderr, "error: %s is not a VVC Annex B byte stream\n", in_path);
        free(buf);
        return 1;
    }

    int *au_start = malloc(sizeof(int) * (size_t)(nals.n + 1));
    int au_count = 0;
    vvc_compute_aus(&nals, au_start, &au_count);
    if (au_count == 0) {
        fprintf(stderr, "error: no access units (no VCL NAL units) found\n");
        free(au_start);
        vvc_nal_list_free(&nals);
        free(buf);
        return 1;
    }

    /* Presentation-order (display-order) reordering of the metadata, like
     * hdr10plus_tool inject. Falls back to decode order when the POC of any
     * access unit cannot be determined. */
    int *presentation = malloc(sizeof(int) * (size_t)au_count);
    int *au_last = NULL;
    if (!presentation) {
        fprintf(stderr, "error: out of memory\n");
        free(au_start);
        vvc_nal_list_free(&nals);
        free(buf);
        return 1;
    }
    if (reorder) {
        if (vvc_compute_presentation_order(&nals, au_start, au_count, presentation) == 0) {
            int changed = 0;
            for (int i = 0; i < au_count; i++)
                if (presentation[i] != i)
                    changed = 1;
            if (changed)
                printf("Reordered metadata to presentation order (%d access unit(s) differ)\n",
                       au_count);
            else
                reorder = 0; /* nothing to reorder */
        } else {
            fprintf(stderr,
                    "Warning: could not determine picture order count; "
                    "metadata will be associated in decode order\n");
            reorder = 0;
        }
    }
    if (!reorder) {
        for (int i = 0; i < au_count; i++)
            presentation[i] = i;
    }

    jval *root = NULL;
    int n_meta = 0;
    st2094_meta *metas = NULL;
    uint8_t **payloads = NULL;
    size_t *payload_lens = NULL;
    if (json_path) {
        root = json_parse_file(json_path);
        if (!root) {
            fprintf(stderr, "error: failed to parse JSON metadata %s\n", json_path);
            free(presentation);
            free(au_start);
            vvc_nal_list_free(&nals);
            free(buf);
            return 1;
        }
        const jval *scene_info = json_obj_get(root, "SceneInfo");
        if (!scene_info || scene_info->type != J_ARR || json_arr_len(scene_info) == 0) {
            fprintf(stderr, "error: JSON has no SceneInfo array\n");
            json_free(root);
            free(presentation);
            free(au_start);
            vvc_nal_list_free(&nals);
            free(buf);
            return 1;
        }
        n_meta = json_arr_len(scene_info);

        metas = calloc((size_t)n_meta, sizeof(st2094_meta));
        payloads = calloc((size_t)n_meta, sizeof(uint8_t *));
        payload_lens = calloc((size_t)n_meta, sizeof(size_t));
        if (!metas || !payloads || !payload_lens) {
            fprintf(stderr, "error: out of memory\n");
            goto fail_meta;
        }
        for (int i = 0; i < n_meta; i++) {
            if (st2094_from_json(scene_info->elems[i], &metas[i]) < 0) {
                fprintf(stderr, "error: invalid metadata at SceneInfo index %d\n", i);
                goto fail_meta;
            }
            if (st2094_encode(&metas[i], 1, &payloads[i], &payload_lens[i]) < 0) {
                fprintf(stderr, "error: failed to encode metadata at SceneInfo index %d\n", i);
                goto fail_meta;
            }
        }

        printf("Parsed %d access unit(s), %d metadata entr%s\n",
               au_count, n_meta, n_meta == 1 ? "y" : "ies");
        if (n_meta < au_count)
            printf("Warning: fewer metadata entries than frames; last entry will be repeated\n");
        else if (n_meta > au_count)
            printf("Warning: more metadata entries than frames; extras will be skipped\n");
    }

    rpu_frame *rpu_frames = NULL;
    int n_rpu = 0;
    if (rpu_path) {
        uint8_t *rpu_buf = NULL;
        size_t rpu_len = 0;
        if (read_file(rpu_path, &rpu_buf, &rpu_len) < 0)
            goto fail_meta;
        int rc = rpu_parse_file(rpu_buf, rpu_len, &rpu_frames, &n_rpu);
        free(rpu_buf);
        if (rc < 0) {
            fprintf(stderr, "error: failed to parse Dolby Vision RPU file %s\n", rpu_path);
            goto fail_meta;
        }
        printf("Parsed %d Dolby Vision RPU entr%s\n", n_rpu, n_rpu == 1 ? "y" : "ies");
        if (n_rpu < au_count)
            printf("Warning: fewer RPU entries than frames; last entry will be repeated\n");
        else if (n_rpu > au_count)
            printf("Warning: more RPU entries than frames; extras will be skipped\n");
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "error: cannot create %s\n", out_path);
        goto fail_meta;
    }

    /* Build the output item list. */
    write_item *items = calloc((size_t)(nals.n + au_count * 2), sizeof(write_item));
    if (!items) {
        fprintf(stderr, "error: out of memory\n");
        fclose(out);
        goto fail_meta;
    }
    int item_n = 0;
    int au_idx = 0;
    if (rpu_path) {
        au_last = malloc(sizeof(int) * (size_t)au_count);
        if (!au_last) {
            fprintf(stderr, "error: out of memory\n");
            goto fail_items;
        }
        compute_au_last_nal(&nals, au_start, au_count, au_last);
    }
    for (int i = 0; i < nals.n; i++) {
        /* New access unit: insert the HDR10+ prefix SEI before its first slice. */
        if (au_idx < au_count && i == au_start[au_idx]) {
            if (json_path) {
                int meta_idx = presentation[au_idx] < n_meta ? presentation[au_idx] : n_meta - 1;
                const vvc_nal *first_vcl = &nals.nals[i];
                uint8_t tid = first_vcl->nuh_temporal_id_plus1 ? first_vcl->nuh_temporal_id_plus1 : 1;
                uint8_t layer = first_vcl->nuh_layer_id;
                uint8_t *sei = NULL;
                size_t sei_len = 0;
                if (vvc_build_hdr10p_nal(payloads[meta_idx], payload_lens[meta_idx],
                                         layer, tid, &sei, &sei_len) < 0) {
                    fprintf(stderr, "error: failed to build HDR10+ SEI NAL\n");
                    goto fail_items;
                }
                items[item_n].data = sei;
                items[item_n].len = sei_len;
                items[item_n].sc_len = 4;
                items[item_n].owned = 1;
                item_n++;
            }
            au_idx++;
        }

        /* Existing RPU NAL units of the configured type are replaced. */
        if (rpu_path && nal_is_rpu(&nals.nals[i], rpu_nal_type)) {
            continue;
        }

        const vvc_nal *nal = &nals.nals[i];
        if (json_path && nal->nal_unit_type == VVC_PREFIX_SEI_NUT) {
            uint8_t *rewritten = NULL;
            size_t rlen = 0;
            int drop = 0;
            int rc = strip_hdr10p_from_sei(nal->data, nal->len, &rewritten, &rlen, &drop);
            if (rc < 0) {
                fprintf(stderr, "error: failed to rewrite SEI NAL\n");
                goto fail_items;
            }
            if (rc == 1) {
                if (drop) {
                    free(rewritten);
                    continue; /* NAL contained only HDR10+ messages: drop it */
                }
                /* NAL had HDR10+ and other messages: write the rewritten version. */
                items[item_n].data = rewritten;
                items[item_n].len = rlen;
                items[item_n].sc_len = 3;
                items[item_n].owned = 1;
                item_n++;
                continue;
            }
            /* rc == 0: no HDR10+ message, fall through to the normal path. */
        }

        items[item_n].data = (uint8_t *)nal->data;
        items[item_n].len = nal->len;
        items[item_n].sc_len = nal->start_code_len ? nal->start_code_len : 3;
        items[item_n].owned = 0;
        item_n++;

        /* Insert the Dolby Vision RPU NAL as the last NAL unit of the AU. */
        if (rpu_path && au_idx > 0) {
            int cur_au = au_idx - 1;
            if (i == au_last[cur_au]) {
                int rpu_idx = presentation[cur_au] < n_rpu ? presentation[cur_au] : n_rpu - 1;
                const vvc_nal *first_vcl = &nals.nals[au_start[cur_au]];
                uint8_t tid = first_vcl->nuh_temporal_id_plus1 ? first_vcl->nuh_temporal_id_plus1 : 1;
                uint8_t *rpu = NULL;
                size_t rpu_len = 0;
                if (rpu_build_vvc_nal(&rpu_frames[rpu_idx], (uint8_t)rpu_nal_type, tid,
                                      &rpu, &rpu_len) < 0) {
                    fprintf(stderr, "error: failed to build Dolby Vision RPU NAL\n");
                    goto fail_items;
                }
                items[item_n].data = rpu;
                items[item_n].len = rpu_len;
                items[item_n].sc_len = 4;
                items[item_n].owned = 1;
                item_n++;
            }
        }
    }

    if (write_stream(out, items, item_n) < 0) {
        fprintf(stderr, "error: failed to write output\n");
        fclose(out);
        goto fail_items;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "error: failed to write output\n");
        goto fail_items;
    }

    printf("Wrote %d NAL unit(s) to %s (%d HDR10+ SEI%s, %d Dolby Vision RPU%s inserted)\n",
           item_n, out_path, json_path ? au_count : 0, json_path ? "" : "",
           rpu_path ? au_count : 0, rpu_path ? "" : "");

    write_items_free(items, item_n);
    for (int i = 0; i < n_meta; i++)
        free(payloads[i]);
    free(payloads);
    free(payload_lens);
    for (int i = 0; i < n_meta; i++)
        st2094_free(&metas[i]);
    free(metas);
    json_free(root);
    rpu_frames_free(rpu_frames, n_rpu);
    free(au_last);
    free(presentation);
    free(au_start);
    vvc_nal_list_free(&nals);
    free(buf);
    return 0;

fail_items:
    write_items_free(items, item_n);
    fclose(out);
fail_meta:
    for (int i = 0; i < n_meta; i++)
        free(payloads[i]);
    free(payloads);
    free(payload_lens);
    for (int i = 0; i < n_meta; i++)
        st2094_free(&metas[i]);
    free(metas);
    json_free(root);
    rpu_frames_free(rpu_frames, n_rpu);
    free(au_last);
    free(presentation);
    free(au_start);
    vvc_nal_list_free(&nals);
    free(buf);
    return 1;
}

/* ---------- remove ---------- */

static int cmd_remove(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = "output.vvc";
    int rpu_nal_type = VVC_DEFAULT_RPU_NAL_TYPE;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in_path = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "--rpu-nal-type") && i + 1 < argc) {
            char *endp = NULL;
            long v = strtol(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || v < 0 || v > 31) {
                fprintf(stderr, "error: --rpu-nal-type must be a NAL unit type in 0..31\n");
                return 1;
            }
            rpu_nal_type = (int)v;
        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    if (!in_path) {
        fprintf(stderr, "error: remove requires -i\n");
        return 1;
    }

    uint8_t *buf = NULL;
    size_t buflen = 0;
    if (read_file(in_path, &buf, &buflen) < 0)
        return 1;

    vvc_nal_list nals = { 0 };
    if (vvc_scan_stream(buf, buflen, &nals) < 0) {
        fprintf(stderr, "error: %s is not a VVC Annex B byte stream\n", in_path);
        free(buf);
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "error: cannot create %s\n", out_path);
        vvc_nal_list_free(&nals);
        free(buf);
        return 1;
    }

    write_item *items = calloc((size_t)nals.n, sizeof(write_item));
    if (!items) {
        fclose(out);
        vvc_nal_list_free(&nals);
        free(buf);
        return 1;
    }
    int item_n = 0, removed = 0, rpu_removed = 0;
    for (int i = 0; i < nals.n; i++) {
        const vvc_nal *nal = &nals.nals[i];
        if (nal_is_rpu(nal, rpu_nal_type)) {
            rpu_removed++;
            continue;
        }
        if (nal->nal_unit_type == VVC_PREFIX_SEI_NUT) {
            uint8_t *rewritten = NULL;
            size_t rlen = 0;
            int drop = 0;
            int rc = strip_hdr10p_from_sei(nal->data, nal->len, &rewritten, &rlen, &drop);
            if (rc < 0) {
                fprintf(stderr, "error: failed to rewrite SEI NAL\n");
                goto fail;
            }
            if (rc == 1) {
                removed++;
                if (drop) {
                    free(rewritten);
                    continue;
                }
                items[item_n].data = rewritten;
                items[item_n].len = rlen;
                items[item_n].sc_len = 3;
                items[item_n].owned = 1;
                item_n++;
                continue;
            }
        }
        items[item_n].data = (uint8_t *)nal->data;
        items[item_n].len = nal->len;
        items[item_n].sc_len = nal->start_code_len ? nal->start_code_len : 3;
        items[item_n].owned = 0;
        item_n++;
    }

    if (write_stream(out, items, item_n) < 0) {
        fprintf(stderr, "error: failed to write output\n");
        goto fail;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "error: failed to write output\n");
        goto fail;
    }
    printf("Removed HDR10+ messages from %d SEI NAL unit(s), %d Dolby Vision RPU NAL unit(s); wrote %s\n",
           removed, rpu_removed, out_path);
    write_items_free(items, item_n);
    vvc_nal_list_free(&nals);
    free(buf);
    return 0;

fail:
    write_items_free(items, item_n);
    fclose(out);
    vvc_nal_list_free(&nals);
    free(buf);
    return 1;
}

/* ---------- extract ---------- */

static int st2094_same_scene(const st2094_meta *a, const st2094_meta *b)
{
    if (a->num_windows != b->num_windows)
        return 0;
    if (a->targeted_system_display_maximum_luminance !=
        b->targeted_system_display_maximum_luminance)
        return 0;
    if (memcmp(a->maxscl, b->maxscl, sizeof(a->maxscl)) != 0)
        return 0;
    if (a->average_maxrgb != b->average_maxrgb)
        return 0;
    if (a->num_distribution_maxrgb_percentiles != b->num_distribution_maxrgb_percentiles)
        return 0;
    for (int i = 0; i < a->num_distribution_maxrgb_percentiles; i++) {
        if (a->distribution[i].percentage != b->distribution[i].percentage ||
            a->distribution[i].percentile != b->distribution[i].percentile)
            return 0;
    }
    if (a->tone_mapping_flag != b->tone_mapping_flag)
        return 0;
    if (a->tone_mapping_flag) {
        if (a->knee_point_x != b->knee_point_x ||
            a->knee_point_y != b->knee_point_y ||
            a->num_bezier_curve_anchors != b->num_bezier_curve_anchors)
            return 0;
        for (int i = 0; i < a->num_bezier_curve_anchors; i++)
            if (a->bezier_curve_anchors[i] != b->bezier_curve_anchors[i])
                return 0;
    }
    return 1;
}

static int cmd_extract(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in_path = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    if (!in_path) {
        fprintf(stderr, "error: extract requires -i\n");
        return 1;
    }
    if (!out_path)
        out_path = "extracted.json";

    uint8_t *buf = NULL;
    size_t buflen = 0;
    if (read_file(in_path, &buf, &buflen) < 0)
        return 1;

    vvc_nal_list nals = { 0 };
    if (vvc_scan_stream(buf, buflen, &nals) < 0) {
        fprintf(stderr, "error: %s is not a VVC Annex B byte stream\n", in_path);
        free(buf);
        return 1;
    }

    st2094_meta *metas = NULL;
    int n_meta = 0, cap_meta = 0;

    for (int i = 0; i < nals.n; i++) {
        const vvc_nal *nal = &nals.nals[i];
        if (nal->nal_unit_type != VVC_PREFIX_SEI_NUT && nal->nal_unit_type != VVC_SUFFIX_SEI_NUT)
            continue;
        uint8_t *rbsp = malloc(nal->len);
        if (!rbsp)
            goto oom;
        size_t rlen = vvc_rbsp_from_nal(nal->data, nal->len, rbsp);
        if (rlen < 2) {
            free(rbsp);
            continue;
        }
        vvc_sei_msg msgs[32];
        int n = vvc_parse_sei(rbsp + 2, rlen - 2, msgs, 32);
        for (int j = 0; j < n; j++) {
            if (msgs[j].payload_type != VVC_SEI_USER_DATA_REGISTERED_ITU_T_T35)
                continue;
            const uint8_t *payload = rbsp + 2 + msgs[j].payload_offset;
            if (!st2094_detect(payload, msgs[j].payload_size))
                continue;
            if (n_meta == cap_meta) {
                int ncap = cap_meta ? cap_meta * 2 : 64;
                st2094_meta *nm = realloc(metas, sizeof(st2094_meta) * (size_t)ncap);
                if (!nm)
                    goto oom;
                metas = nm;
                cap_meta = ncap;
            }
            st2094_init(&metas[n_meta]);
            if (st2094_decode(payload, msgs[j].payload_size, &metas[n_meta]) < 0) {
                fprintf(stderr, "warning: invalid ST 2094-40 payload at NAL %d, skipped\n", i);
                continue;
            }
            n_meta++;
        }
        free(rbsp);
    }

    if (n_meta == 0) {
        printf("No HDR10+ metadata found in %s\n", in_path);
        free(metas);
        vvc_nal_list_free(&nals);
        free(buf);
        return 1;
    }

    int profile_b = 1, profile_a = 1;
    for (int i = 0; i < n_meta; i++) {
        if (!metas[i].tone_mapping_flag || metas[i].targeted_system_display_maximum_luminance == 0)
            profile_b = 0;
        if (metas[i].tone_mapping_flag || metas[i].targeted_system_display_maximum_luminance != 0)
            profile_a = 0;
    }
    const char *profile = profile_b ? "B" : (profile_a ? "A" : "N/A");

    jbuf b;
    jbuf_init(&b);
    jbuf_puts(&b, "{\n  \"JSONInfo\": {\"HDR10plusProfile\": \"");
    jbuf_puts(&b, profile);
    jbuf_puts(&b, "\", \"Version\": \"1.0\"},\n  \"SceneInfo\": [");

    int scene_id = 0, scene_frame_index = 0;
    for (int i = 0; i < n_meta; i++) {
        if (i > 0 && !st2094_same_scene(&metas[i - 1], &metas[i])) {
            scene_id++;
            scene_frame_index = 0;
        }
        if (i)
            jbuf_puts(&b, ",");
        jbuf_puts(&b, "\n    ");
        st2094_to_json(&metas[i], scene_frame_index, scene_id, i, &b);
        scene_frame_index++;
    }

    jbuf_puts(&b, "\n  ],\n  \"SceneInfoSummary\": {\"SceneFirstFrameIndex\": [");
    int printed_first = 0;
    for (int i = 0; i < n_meta; i++) {
        if (i == 0 || !st2094_same_scene(&metas[i - 1], &metas[i])) {
            if (printed_first)
                jbuf_puts(&b, ",");
            jbuf_printf(&b, "%d", i);
            printed_first = 1;
        }
    }
    jbuf_puts(&b, "], \"SceneFrameNumbers\": [");
    printed_first = 0;
    for (int i = 0; i < n_meta; i++) {
        if (i == 0 || !st2094_same_scene(&metas[i - 1], &metas[i])) {
            int len = 1;
            for (int k = i + 1; k < n_meta; k++) {
                if (st2094_same_scene(&metas[i], &metas[k]))
                    len++;
                else
                    break;
            }
            if (printed_first)
                jbuf_puts(&b, ",");
            jbuf_printf(&b, "%d", len);
            printed_first = 1;
        }
    }
    jbuf_puts(&b, "]},\n  \"ToolInfo\": {\"Tool\": \"");
    jbuf_puts(&b, PROGRAM);
    jbuf_puts(&b, "\", \"Version\": \"");
    jbuf_puts(&b, VERSION);
    jbuf_puts(&b, "\"}\n}\n");

    if (jbuf_write_file(&b, out_path) < 0) {
        fprintf(stderr, "error: cannot write %s\n", out_path);
        jbuf_free(&b);
        goto oom;
    }
    printf("Extracted %d HDR10+ metadata entr%s to %s (profile %s)\n",
           n_meta, n_meta == 1 ? "y" : "ies", out_path, profile);

    jbuf_free(&b);
    for (int i = 0; i < n_meta; i++)
        st2094_free(&metas[i]);
    free(metas);
    vvc_nal_list_free(&nals);
    free(buf);
    return 0;

oom:
    fprintf(stderr, "error: out of memory\n");
    if (metas)
        for (int i = 0; i < n_meta; i++)
            st2094_free(&metas[i]);
    free(metas);
    vvc_nal_list_free(&nals);
    free(buf);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }
    if (!strcmp(argv[1], "inject"))
        return cmd_inject(argc, argv);
    if (!strcmp(argv[1], "remove"))
        return cmd_remove(argc, argv);
    if (!strcmp(argv[1], "extract"))
        return cmd_extract(argc, argv);

    fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    usage(stderr);
    return 1;
}
