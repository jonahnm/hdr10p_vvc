#ifndef HDR10P_VVC_VVC_H
#define HDR10P_VVC_VVC_H

#include <stddef.h>
#include <stdint.h>

#include "st2094.h"

/*
 * VVC (Rec. ITU-T H.266 | ISO/IEC 23090-3) NAL unit types (Table 8 in H.266)
 * and Annex B byte-stream handling plus SEI message framing.
 */

enum {
    VVC_TRAIL_NUT      = 0,
    VVC_STSA_NUT       = 1,
    VVC_RADL_NUT       = 2,
    VVC_RASL_NUT       = 3,
    VVC_RSV_VCL_4      = 4,
    VVC_RSV_VCL_5      = 5,
    VVC_RSV_VCL_6      = 6,
    VVC_IDR_W_RADL     = 7,
    VVC_IDR_N_LP       = 8,
    VVC_CRA_NUT        = 9,
    VVC_GDR_NUT        = 10,
    VVC_RSV_IRAP_11    = 11,
    VVC_OPI_NUT        = 12,
    VVC_DCI_NUT        = 13,
    VVC_VPS_NUT        = 14,
    VVC_SPS_NUT        = 15,
    VVC_PPS_NUT        = 16,
    VVC_PREFIX_APS_NUT = 17,
    VVC_SUFFIX_APS_NUT = 18,
    VVC_PH_NUT         = 19,
    VVC_AUD_NUT        = 20,
    VVC_EOS_NUT        = 21,
    VVC_EOB_NUT        = 22,
    VVC_PREFIX_SEI_NUT = 23,
    VVC_SUFFIX_SEI_NUT = 24,
    VVC_FD_NUT         = 25,
    VVC_RSV_NVCL_26    = 26,
    VVC_RSV_NVCL_27    = 27,
    VVC_UNSPEC_28      = 28,
    VVC_UNSPEC_29      = 29,
    VVC_UNSPEC_30      = 30,
    VVC_UNSPEC_31      = 31,
};

/* SEI payload type: user data registered by Rec. ITU-T T.35 (H.274 8.3). */
#define VVC_SEI_USER_DATA_REGISTERED_ITU_T_T35 4

typedef struct {
    const uint8_t *data;  /* NAL bytes including the 2-byte NAL header */
    size_t         len;
    size_t         start;         /* offset of start code in input buffer */
    uint8_t        start_code_len;/* 3 or 4 */
    uint8_t        nal_unit_type;
    uint8_t        nuh_layer_id;
    uint8_t        nuh_temporal_id_plus1;
    int            is_vcl;
    int            is_irap;       /* IDR_W_RADL, IDR_N_LP, CRA, RSV_IRAP_11 */
    int            is_gdr;
} vvc_nal;

typedef struct {
    vvc_nal *nals;
    int      n;
    int      cap;
} vvc_nal_list;

typedef struct {
    uint32_t payload_type;
    uint32_t payload_size;
    size_t   payload_offset; /* offset of payload bytes inside the SEI RBSP */
} vvc_sei_msg;

/* --- NAL / stream scanning --- */

/* Split an Annex B byte stream into NAL units (data includes the NAL header). */
int vvc_scan_stream(const uint8_t *buf, size_t len, vvc_nal_list *out);
void vvc_nal_list_free(vvc_nal_list *l);

/* Emulation prevention byte (0x03) removal: NAL bytes -> RBSP bytes.
 * out must hold at least len bytes. Returns RBSP length. */
size_t vvc_rbsp_from_nal(const uint8_t *nal, size_t len, uint8_t *out);

/* RBSP bytes -> NAL bytes with emulation prevention inserted.
 * out must hold at least len * 2 bytes. Returns NAL length. */
size_t vvc_nal_from_rbsp(const uint8_t *rbsp, size_t len, uint8_t *out);

/* --- SEI messages --- */

/* Parse SEI messages out of an SEI RBSP (starting right after the NAL header).
 * Returns number of messages parsed (<= max). */
int vvc_parse_sei(const uint8_t *rbsp, size_t len, vvc_sei_msg *msgs, int max);

/*
 * Build a complete prefix SEI NAL unit (without start code) carrying one
 * user_data_registered_itu_t_t35 message with the given ST 2094-40 payload.
 * Returns malloc'd bytes on success.
 */
int vvc_build_hdr10p_nal(const uint8_t *st2094_payload, size_t payload_len,
                         uint8_t nuh_layer_id, uint8_t nuh_temporal_id_plus1,
                         uint8_t **out, size_t *out_len);

/* --- Access unit grouping --- */

/*
 * Compute access-unit boundaries. au_start is a caller-allocated array of at
 * least n ints; on return it holds the NAL index of the first NAL unit of each
 * access unit and *au_count holds the number of access units.
 *
 * A VCL NAL unit starts a new access unit when it is the first VCL NAL unit,
 * when it is an IRAP or GDR NAL unit, when it is preceded by a non-VCL NAL
 * unit (AUD, PH, SEI, parameter sets, EOS/EOB, ...), or when the stream
 * contains no AUD/PH NAL units at all (in which case one slice per picture is
 * assumed, matching the default output of VVC encoders).
 */
int vvc_compute_aus(const vvc_nal_list *nals, int *au_start, int *au_count);

#endif /* HDR10P_VVC_VVC_H */
