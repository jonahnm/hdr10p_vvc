#include "vvc_poc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The parsing below follows Rec. ITU-T H.266 (V4) syntax tables:
 *   seq_parameter_set_rbsp()      (7.3.3.3)
 *   pic_parameter_set_rbsp()      (7.3.3.4)
 *   picture_header_structure()    (7.3.8.1)
 * and the POC derivation of clause 8.3.1.
 */

/* ---------------- general_constraints_info() ---------------- */

static int skip_gci(bit_reader *r)
{
    uint32_t v;
    int b;

    if (br_bit(r, &b) < 0) return -1; /* gci_present_flag */
    if (!b)
        goto align;

    /* general */
    if (br_bit(r, &b) < 0) return -1; /* gci_intra_only_constraint_flag */
    if (br_bit(r, &b) < 0) return -1; /* gci_all_layers_independent_constraint_flag */
    if (br_bit(r, &b) < 0) return -1; /* gci_one_au_only_constraint_flag */

    /* picture format */
    if (br_read(r, &v, 4) < 0) return -1; /* gci_sixteen_minus_max_bitdepth_constraint_idc */
    if (br_read(r, &v, 2) < 0) return -1; /* gci_three_minus_max_chroma_format_constraint_idc */

    /* NAL unit type related */
    if (br_bit(r, &b) < 0) return -1; /* no_mixed_nalu_types_in_pic */
    if (br_bit(r, &b) < 0) return -1; /* no_trail */
    if (br_bit(r, &b) < 0) return -1; /* no_stsa */
    if (br_bit(r, &b) < 0) return -1; /* no_rasl */
    if (br_bit(r, &b) < 0) return -1; /* no_radl */
    if (br_bit(r, &b) < 0) return -1; /* no_idr */
    if (br_bit(r, &b) < 0) return -1; /* no_cra */
    if (br_bit(r, &b) < 0) return -1; /* no_gdr */
    if (br_bit(r, &b) < 0) return -1; /* no_aps */
    if (br_bit(r, &b) < 0) return -1; /* no_idr_rpl */

    /* tile, slice, subpicture partitioning */
    if (br_bit(r, &b) < 0) return -1; /* one_tile_per_pic */
    if (br_bit(r, &b) < 0) return -1; /* pic_header_in_slice_header */
    if (br_bit(r, &b) < 0) return -1; /* one_slice_per_pic */
    if (br_bit(r, &b) < 0) return -1; /* no_rectangular_slice */
    if (br_bit(r, &b) < 0) return -1; /* one_slice_per_subpic */
    if (br_bit(r, &b) < 0) return -1; /* no_subpic_info */

    /* CTU and block partitioning */
    if (br_read(r, &v, 2) < 0) return -1; /* three_minus_max_log2_ctu_size */
    if (br_bit(r, &b) < 0) return -1; /* no_partition_constraints_override */
    if (br_bit(r, &b) < 0) return -1; /* no_mtt */
    if (br_bit(r, &b) < 0) return -1; /* no_qtbtt_dual_tree_intra */

    /* intra */
    if (br_bit(r, &b) < 0) return -1; /* no_palette */
    if (br_bit(r, &b) < 0) return -1; /* no_ibc */
    if (br_bit(r, &b) < 0) return -1; /* no_isp */
    if (br_bit(r, &b) < 0) return -1; /* no_mrl */
    if (br_bit(r, &b) < 0) return -1; /* no_mip */
    if (br_bit(r, &b) < 0) return -1; /* no_cclm */

    /* inter */
    if (br_bit(r, &b) < 0) return -1; /* no_ref_pic_resampling */
    if (br_bit(r, &b) < 0) return -1; /* no_res_change_in_clvs */
    if (br_bit(r, &b) < 0) return -1; /* no_weighted_prediction */
    if (br_bit(r, &b) < 0) return -1; /* no_ref_wraparound */
    if (br_bit(r, &b) < 0) return -1; /* no_temporal_mvp */
    if (br_bit(r, &b) < 0) return -1; /* no_sbtmvp */
    if (br_bit(r, &b) < 0) return -1; /* no_amvr */
    if (br_bit(r, &b) < 0) return -1; /* no_bdof */
    if (br_bit(r, &b) < 0) return -1; /* no_smvd */
    if (br_bit(r, &b) < 0) return -1; /* no_dmvr */
    if (br_bit(r, &b) < 0) return -1; /* no_mmvd */
    if (br_bit(r, &b) < 0) return -1; /* no_affine_motion */
    if (br_bit(r, &b) < 0) return -1; /* no_prof */
    if (br_bit(r, &b) < 0) return -1; /* no_bcw */
    if (br_bit(r, &b) < 0) return -1; /* no_ciip */
    if (br_bit(r, &b) < 0) return -1; /* no_gpm */

    /* transform, quantization, residual */
    if (br_bit(r, &b) < 0) return -1; /* no_luma_transform_size_64 */
    if (br_bit(r, &b) < 0) return -1; /* no_transform_skip */
    if (br_bit(r, &b) < 0) return -1; /* no_bdpcm */
    if (br_bit(r, &b) < 0) return -1; /* no_mts */
    if (br_bit(r, &b) < 0) return -1; /* no_lfnst */
    if (br_bit(r, &b) < 0) return -1; /* no_joint_cbcr */
    if (br_bit(r, &b) < 0) return -1; /* no_sbt */
    if (br_bit(r, &b) < 0) return -1; /* no_act */
    if (br_bit(r, &b) < 0) return -1; /* no_explicit_scaling_list */
    if (br_bit(r, &b) < 0) return -1; /* no_dep_quant */
    if (br_bit(r, &b) < 0) return -1; /* no_sign_data_hiding */
    if (br_bit(r, &b) < 0) return -1; /* no_cu_qp_delta */
    if (br_bit(r, &b) < 0) return -1; /* no_chroma_qp_offset */

    /* loop filter */
    if (br_bit(r, &b) < 0) return -1; /* no_sao */
    if (br_bit(r, &b) < 0) return -1; /* no_alf */
    if (br_bit(r, &b) < 0) return -1; /* no_ccalf */
    if (br_bit(r, &b) < 0) return -1; /* no_lmcs */
    if (br_bit(r, &b) < 0) return -1; /* no_ladf */
    if (br_bit(r, &b) < 0) return -1; /* no_virtual_boundaries */

    if (br_read(r, &v, 8) < 0) return -1; /* gci_num_additional_bits */
    uint32_t num_extra = v;
    uint32_t used = 0;
    if (num_extra > 5) {
        if (br_bit(r, &b) < 0) return -1; /* all_rap_pictures */
        if (br_bit(r, &b) < 0) return -1; /* no_extended_precision_processing */
        if (br_bit(r, &b) < 0) return -1; /* no_ts_residual_coding_rice */
        if (br_bit(r, &b) < 0) return -1; /* no_rrc_rice_extension */
        if (br_bit(r, &b) < 0) return -1; /* no_persistent_rice_adaptation */
        if (br_bit(r, &b) < 0) return -1; /* no_reverse_last_sig_coeff */
        used = 6;
    }
    for (uint32_t i = 0; i < num_extra - used; i++) {
        if (br_bit(r, &b) < 0) return -1; /* gci_reserved_bit */
    }

align:
    while (!br_byte_aligned(r)) {
        if (br_bit(r, &b) < 0) return -1; /* gci_alignment_zero_bit */
    }
    return 0;
}

/* ---------------- profile_tier_level() ---------------- */

static int skip_profile_tier_level(bit_reader *r, int profile_tier_present,
                                   int max_sub_layers_minus1)
{
    uint32_t v;
    int b;
    uint8_t sub_layer_present[8] = { 0 };

    if (profile_tier_present) {
        if (br_read(r, &v, 7) < 0) return -1; /* general_profile_idc */
        if (br_bit(r, &b) < 0) return -1;     /* general_tier_flag */
    }
    if (br_read(r, &v, 8) < 0) return -1; /* general_level_idc */
    if (br_bit(r, &b) < 0) return -1;     /* ptl_frame_only_constraint_flag */
    if (br_bit(r, &b) < 0) return -1;     /* ptl_multilayer_enabled_flag */
    if (profile_tier_present) {
        if (skip_gci(r) < 0)
            return -1;
    }
    for (int i = max_sub_layers_minus1 - 1; i >= 0; i--) {
        if (br_bit(r, &b) < 0) return -1;
        sub_layer_present[i] = (uint8_t)b;
    }
    while (!br_byte_aligned(r)) {
        if (br_bit(r, &b) < 0) return -1; /* ptl_reserved_zero_bit */
    }
    for (int i = max_sub_layers_minus1 - 1; i >= 0; i--) {
        if (sub_layer_present[i]) {
            if (br_read(r, &v, 8) < 0) return -1; /* sublayer_level_idc */
        }
    }
    if (profile_tier_present) {
        if (br_read(r, &v, 8) < 0) return -1; /* ptl_num_sub_profiles */
        for (uint32_t i = 0; i < v; i++) {
            if (br_read(r, &v, 32) < 0) return -1; /* general_sub_profile_idc */
        }
    }
    return 0;
}

/* ---------------- SPS ---------------- */

int vvc_parse_sps(const uint8_t *rbsp, size_t len, vvc_sps *out)
{
    memset(out, 0, sizeof(*out));
    bit_reader r;
    br_init(&r, rbsp, len);
    uint32_t v;
    int b;

    if (br_read(&r, &v, 4) < 0) return -1;
    out->sps_id = (uint8_t)v;
    if (br_read(&r, &v, 4) < 0) return -1; /* sps_video_parameter_set_id */
    if (br_read(&r, &v, 3) < 0) return -1;
    int max_sublayers_minus1 = (int)v;
    if (br_read(&r, &v, 2) < 0) return -1;
    int chroma_format_idc = (int)v;
    if (chroma_format_idc == 3) {
        if (br_bit(&r, &b) < 0) return -1; /* sps_separate_colour_plane_flag */
    }
    if (br_read(&r, &v, 2) < 0) return -1;
    int ctb_log2_size_y = (int)v + 5;
    int ctb_size_y = 1 << ctb_log2_size_y;

    if (br_bit(&r, &b) < 0) return -1;
    int ptl_dpb_hrd_params_present_flag = b;
    if (ptl_dpb_hrd_params_present_flag) {
        if (skip_profile_tier_level(&r, 1, max_sublayers_minus1) < 0)
            return -1;
    }

    if (br_bit(&r, &b) < 0) return -1; /* sps_gdr_enabled_flag */
    if (br_bit(&r, &b) < 0) return -1; /* sps_ref_pic_resampling_enabled_flag */
    if (b) {
        if (br_bit(&r, &b) < 0) return -1; /* sps_res_change_in_clvs_allowed_flag */
    }

    uint32_t pic_width_max = 0, pic_height_max = 0;
    if (br_read_ue(&r, &pic_width_max) < 0) return -1;
    if (br_read_ue(&r, &pic_height_max) < 0) return -1;

    if (br_bit(&r, &b) < 0) return -1; /* sps_conformance_window_flag */
    if (b) {
        for (int i = 0; i < 4; i++) {
            if (br_read_ue(&r, &v) < 0) return -1;
        }
    }

    if (br_bit(&r, &b) < 0) return -1; /* sps_subpic_info_present_flag */
    if (b) {
        uint32_t num_subpics_minus1 = 0;
        if (br_read_ue(&r, &num_subpics_minus1) < 0) return -1;
        int independent_subpics_flag = 0;
        int subpic_same_size_flag = 0;
        if (num_subpics_minus1 > 0) {
            if (br_bit(&r, &b) < 0) return -1;
            independent_subpics_flag = b;
            if (br_bit(&r, &b) < 0) return -1;
            subpic_same_size_flag = b;
        }
        for (uint32_t i = 0; num_subpics_minus1 > 0 && i <= num_subpics_minus1; i++) {
            if (!subpic_same_size_flag || i == 0) {
                if (i > 0 && pic_width_max > (uint32_t)ctb_size_y)
                    if (br_read(&r, &v, ctb_log2_size_y) < 0) return -1;
                if (i > 0 && pic_height_max > (uint32_t)ctb_size_y)
                    if (br_read(&r, &v, ctb_log2_size_y) < 0) return -1;
                if (i < num_subpics_minus1 && pic_width_max > (uint32_t)ctb_size_y)
                    if (br_read(&r, &v, ctb_log2_size_y) < 0) return -1;
                if (i < num_subpics_minus1 && pic_height_max > (uint32_t)ctb_size_y)
                    if (br_read(&r, &v, ctb_log2_size_y) < 0) return -1;
            }
            if (!independent_subpics_flag) {
                if (br_bit(&r, &b) < 0) return -1;
                if (br_bit(&r, &b) < 0) return -1;
            }
        }
        uint32_t subpic_id_len_minus1 = 0;
        if (br_read_ue(&r, &subpic_id_len_minus1) < 0) return -1;
        if (br_bit(&r, &b) < 0) return -1; /* sps_subpic_id_mapping_explicitly_signalled_flag */
        if (b) {
            if (br_bit(&r, &b) < 0) return -1; /* sps_subpic_id_mapping_present_flag */
            if (b) {
                for (uint32_t i = 0; i <= num_subpics_minus1; i++) {
                    if (br_read(&r, &v, (int)subpic_id_len_minus1 + 1) < 0)
                        return -1;
                }
            }
        }
    }

    if (br_read_ue(&r, &v) < 0) return -1; /* sps_bitdepth_minus8 */
    if (br_bit(&r, &b) < 0) return -1;     /* sps_entropy_coding_sync_enabled_flag */
    if (br_bit(&r, &b) < 0) return -1;     /* sps_entry_point_offsets_present_flag */

    if (br_read(&r, &v, 4) < 0) return -1;
    out->log2_max_pic_order_cnt_lsb_minus4 = (uint8_t)v;

    if (br_bit(&r, &b) < 0) return -1;
    out->poc_msb_cycle_flag = (uint8_t)b;
    if (b) {
        if (br_read_ue(&r, &v) < 0) return -1;
        out->poc_msb_cycle_len_minus1 = (uint8_t)v;
    }

    if (br_read(&r, &v, 2) < 0) return -1;
    out->num_extra_ph_bytes = (uint8_t)v;

    out->valid = 1;
    return 0;
}

/* ---------------- PPS ---------------- */

int vvc_parse_pps(const uint8_t *rbsp, size_t len, vvc_pps *out)
{
    memset(out, 0, sizeof(*out));
    bit_reader r;
    br_init(&r, rbsp, len);
    uint32_t v;
    if (br_read(&r, &v, 6) < 0) return -1;
    out->pps_id = (uint8_t)v;
    if (br_read(&r, &v, 4) < 0) return -1;
    out->sps_id = (uint8_t)v;
    out->valid = 1;
    return 0;
}

/* ---------------- picture header ---------------- */

static int find_pps_sps(const vvc_pps *pps_list, int n_pps, uint8_t pps_id,
                        const vvc_sps *sps_list, int n_sps, const vvc_pps **pps,
                        const vvc_sps **sps)
{
    for (int i = 0; i < n_pps; i++) {
        if (pps_list[i].valid && pps_list[i].pps_id == pps_id) {
            *pps = &pps_list[i];
            for (int j = 0; j < n_sps; j++) {
                if (sps_list[j].valid && sps_list[j].sps_id == pps_list[i].sps_id) {
                    *sps = &sps_list[j];
                    return 0;
                }
            }
            return -1;
        }
    }
    return -1;
}

int vvc_parse_ph_bits(bit_reader *r, const vvc_sps *sps_list, int n_sps,
                      const vvc_pps *pps_list, int n_pps, vvc_ph *out)
{
    memset(out, 0, sizeof(*out));
    uint32_t v;
    int b;

    if (br_bit(r, &b) < 0) return -1;
    out->gdr_or_irap_pic_flag = (uint8_t)b;
    if (br_bit(r, &b) < 0) return -1;
    out->non_ref_pic_flag = (uint8_t)b;
    if (out->gdr_or_irap_pic_flag) {
        if (br_bit(r, &b) < 0) return -1;
        out->gdr_pic_flag = (uint8_t)b;
    }
    if (br_bit(r, &b) < 0) return -1; /* ph_inter_slice_allowed_flag */
    if (b) {
        if (br_bit(r, &b) < 0) return -1; /* ph_intra_slice_allowed_flag */
    }
    if (br_read_ue(r, &v) < 0) return -1;
    out->pps_id = (uint8_t)v;

    const vvc_pps *pps = NULL;
    const vvc_sps *sps = NULL;
    if (find_pps_sps(pps_list, n_pps, out->pps_id, sps_list, n_sps, &pps, &sps) < 0)
        return -1;

    if (br_read(r, &v, (int)sps->log2_max_pic_order_cnt_lsb_minus4 + 4) < 0)
        return -1;
    out->pic_order_cnt_lsb = v;

    if (out->gdr_pic_flag) {
        if (br_read_ue(r, &v) < 0) return -1;
        out->recovery_poc_cnt = (uint8_t)v;
    }

    for (int i = 0; i < (int)sps->num_extra_ph_bytes * 8; i++) {
        if (br_bit(r, &b) < 0) return -1; /* ph_extra_bit */
    }

    if (sps->poc_msb_cycle_flag) {
        if (br_bit(r, &b) < 0) return -1;
        out->poc_msb_cycle_present_flag = (uint8_t)b;
        if (b) {
            if (br_read(r, &v, (int)sps->poc_msb_cycle_len_minus1 + 1) < 0)
                return -1;
            out->poc_msb_cycle_val = v;
        }
    }

    out->valid = 1;
    return 0;
}

/* ---------------- presentation order ---------------- */

typedef struct {
    int64_t poc;
    int     idx;
} poc_entry;

static int poc_entry_cmp(const void *a, const void *b)
{
    const poc_entry *x = a, *y = b;
    if (x->poc < y->poc) return -1;
    if (x->poc > y->poc) return 1;
    return (x->idx < y->idx) ? -1 : (x->idx > y->idx ? 1 : 0);
}

static int is_irap_or_gdr_type(int t)
{
    return (t >= VVC_IDR_W_RADL && t <= VVC_RSV_IRAP_11); /* 7..11 */
}

int vvc_compute_presentation_order(const vvc_nal_list *nals,
                                   const int *au_start, int au_count,
                                   int *presentation_number)
{
    vvc_sps sps_list[VVC_MAX_SPS];
    vvc_pps pps_list[VVC_MAX_PPS];
    memset(sps_list, 0, sizeof(sps_list));
    memset(pps_list, 0, sizeof(pps_list));
    int n_sps = 0, n_pps = 0;

    /* First pass: collect SPS and PPS. */
    for (int i = 0; i < nals->n; i++) {
        const vvc_nal *n = &nals->nals[i];
        if (n->nal_unit_type != VVC_SPS_NUT && n->nal_unit_type != VVC_PPS_NUT)
            continue;
        uint8_t *rbsp = malloc(n->len);
        if (!rbsp)
            return -1;
        size_t rlen = vvc_rbsp_from_nal(n->data, n->len, rbsp);
        int rc = -1;
        if (rlen >= 2) {
            if (n->nal_unit_type == VVC_SPS_NUT) {
                vvc_sps sps;
                if (vvc_parse_sps(rbsp + 2, rlen - 2, &sps) == 0) {
                    sps_list[sps.sps_id] = sps;
                    n_sps++;
                    rc = 0;
                }
            } else {
                vvc_pps pps;
                if (vvc_parse_pps(rbsp + 2, rlen - 2, &pps) == 0) {
                    pps_list[pps.pps_id] = pps;
                    n_pps++;
                    rc = 0;
                }
            }
        }
        free(rbsp);
        if (rc < 0)
            return -1;
    }

    if (n_sps == 0 || n_pps == 0)
        return -1;

    size_t n_au = (au_count > 0) ? (size_t)au_count : 1;
    int64_t *poc = malloc(sizeof(int64_t) * n_au);
    if (!poc)
        return -1;

    /* Second pass: derive POC for every access unit. */
    int have_prev_tid0 = 0;
    int64_t prev_tid0_poc = 0;
    int ok = 1;

    for (int a = 0; a < au_count && ok; a++) {
        int first_n = au_start[a];
        int prev_au_end = (a == 0) ? 0 : au_start[a - 1];
        int vcl_idx = first_n;

        /* Find the picture header: either a PH NAL unit preceding the first
         * slice, or inside the first slice header. */
        int ph_nal_idx = -1;
        for (int j = prev_au_end; j < first_n; j++) {
            if (nals->nals[j].nal_unit_type == VVC_PH_NUT)
                ph_nal_idx = j;
        }

        bit_reader r;
        vvc_ph ph;
        const vvc_nal *first_vcl = &nals->nals[vcl_idx];
        const vvc_nal *src_nal = (ph_nal_idx >= 0) ? &nals->nals[ph_nal_idx] : first_vcl;
        int slice_case = (ph_nal_idx < 0);

        uint8_t *rbsp = malloc(src_nal->len);
        if (!rbsp) {
            ok = 0;
            break;
        }
        size_t rlen = vvc_rbsp_from_nal(src_nal->data, src_nal->len, rbsp);
        if (rlen < 2) {
            ok = 0;
        } else {
            br_init(&r, rbsp + 2, rlen - 2);
            if (slice_case) {
                int b;
                if (br_bit(&r, &b) < 0 || !b) {
                    /* picture_header_in_slice_header_flag must be 1 here */
                    ok = 0;
                } else {
                    ok = (vvc_parse_ph_bits(&r, sps_list, n_sps, pps_list, n_pps, &ph) == 0);
                }
            } else {
                ok = (vvc_parse_ph_bits(&r, sps_list, n_sps, pps_list, n_pps, &ph) == 0);
            }
        }
        free(rbsp);
        if (!ok)
            break;

        /* POC derivation (clause 8.3.1, following VTM/ffmpeg). */
        const vvc_pps *pps = NULL;
        const vvc_sps *sps = NULL;
        if (find_pps_sps(pps_list, n_pps, ph.pps_id, sps_list, n_sps, &pps, &sps) < 0) {
            ok = 0;
            break;
        }
        int64_t max_poc_lsb = (int64_t)1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
        int64_t poc_msb;

        int clvss = 0;
        int first_type = first_vcl->nal_unit_type;
        /* CLVSS: an IRAP picture, or a GDR picture with recovery POC 0. */
        if ((first_type >= VVC_IDR_W_RADL && first_type <= VVC_CRA_NUT) ||
            first_type == VVC_RSV_IRAP_11)
            clvss = 1;
        else if (first_type == VVC_GDR_NUT)
            clvss = (ph.recovery_poc_cnt == 0);

        if (ph.poc_msb_cycle_present_flag) {
            poc_msb = (int64_t)ph.poc_msb_cycle_val * max_poc_lsb;
        } else if (clvss) {
            poc_msb = 0;
        } else if (!have_prev_tid0) {
            poc_msb = 0; /* first picture of the stream */
        } else {
            int64_t prev_lsb = prev_tid0_poc & (max_poc_lsb - 1);
            int64_t prev_msb = prev_tid0_poc - prev_lsb;
            int64_t lsb = ph.pic_order_cnt_lsb;
            if (lsb < prev_lsb && (prev_lsb - lsb) >= max_poc_lsb / 2)
                poc_msb = prev_msb + max_poc_lsb;
            else if (lsb > prev_lsb && (lsb - prev_lsb) > max_poc_lsb / 2)
                poc_msb = prev_msb - max_poc_lsb;
            else
                poc_msb = prev_msb;
        }
        poc[a] = poc_msb + (int64_t)ph.pic_order_cnt_lsb;

        if (first_vcl->nuh_temporal_id_plus1 == 1 && !ph.non_ref_pic_flag &&
            first_type != VVC_RADL_NUT && first_type != VVC_RASL_NUT) {
            prev_tid0_poc = poc[a];
            have_prev_tid0 = 1;
        }
    }

    if (!ok) {
        free(poc);
        return -1;
    }

    /* Assign presentation numbers: buffer frames per segment, sort by POC at
     * each IRAP/GDR, mirroring hdr10plus_tool's key-frame reordering. */
    poc_entry *buffer = malloc(sizeof(poc_entry) * n_au);
    if (!buffer) {
        free(poc);
        return -1;
    }
    int buf_n = 0;
    int counter = 0;
    for (int a = 0; a < au_count; a++) {
        int first_type = nals->nals[au_start[a]].nal_unit_type;
        if (is_irap_or_gdr_type(first_type) && buf_n > 0) {
            qsort(buffer, (size_t)buf_n, sizeof(poc_entry), poc_entry_cmp);
            for (int i = 0; i < buf_n; i++)
                presentation_number[buffer[i].idx] = counter++;
            buf_n = 0;
        }
        buffer[buf_n].poc = poc[a];
        buffer[buf_n].idx = a;
        buf_n++;
    }
    qsort(buffer, (size_t)buf_n, sizeof(poc_entry), poc_entry_cmp);
    for (int i = 0; i < buf_n; i++)
        presentation_number[buffer[i].idx] = counter++;

    free(buffer);
    free(poc);
    return 0;
}
