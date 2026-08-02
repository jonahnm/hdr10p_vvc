#ifndef HDR10P_VVC_VVC_POC_H
#define HDR10P_VVC_VVC_POC_H

#include <stddef.h>
#include <stdint.h>

#include "bitbuf.h"
#include "vvc.h"

/*
 * Minimal parsing of Rec. ITU-T H.266 SPS / PPS / picture header enough to
 * derive the picture order count (POC, clause 8.3.1) of every access unit,
 * which is used for presentation-order (display-order) metadata reordering.
 */

#define VVC_MAX_SPS 16   /* sps_seq_parameter_set_id is u(4) */
#define VVC_MAX_PPS 64   /* pps_pic_parameter_set_id is u(6) */

typedef struct {
    uint8_t sps_id;
    uint8_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t poc_msb_cycle_flag;
    uint8_t poc_msb_cycle_len_minus1;
    uint8_t num_extra_ph_bytes;
    int     valid;
} vvc_sps;

typedef struct {
    uint8_t pps_id;
    uint8_t sps_id;
    int     valid;
} vvc_pps;

/* Parse an SPS RBSP (bytes after the 2-byte NAL header). Returns 0 on success. */
int vvc_parse_sps(const uint8_t *rbsp, size_t len, vvc_sps *out);

/* Parse a PPS RBSP. Returns 0 on success. */
int vvc_parse_pps(const uint8_t *rbsp, size_t len, vvc_pps *out);

typedef struct {
    uint8_t  gdr_or_irap_pic_flag;
    uint8_t  gdr_pic_flag;
    uint8_t  non_ref_pic_flag;
    uint8_t  pps_id;
    uint8_t  recovery_poc_cnt;   /* valid when gdr_pic_flag */
    uint32_t pic_order_cnt_lsb;
    uint8_t  poc_msb_cycle_present_flag;
    uint32_t poc_msb_cycle_val;
    int      valid;
} vvc_ph;

/*
 * Parse a picture_header_structure() from the bit reader (r must be positioned
 * at the start of the structure). sps_list / pps_list are the tables collected
 * from the bitstream. Returns 0 on success.
 */
int vvc_parse_ph_bits(bit_reader *r, const vvc_sps *sps_list, int n_sps,
                      const vvc_pps *pps_list, int n_pps, vvc_ph *out);

/*
 * Compute the presentation order of the access units described by
 * au_start/au_count (as produced by vvc_compute_aus).
 *
 * On success fills presentation_number[au] with a permutation of
 * 0..au_count-1 giving, for each access unit in decode order, its
 * presentation (display) index. Returns 0 on success; returns non-zero if the
 * POC of any access unit could not be determined (the caller should then fall
 * back to decode-order association).
 */
int vvc_compute_presentation_order(const vvc_nal_list *nals,
                                   const int *au_start, int au_count,
                                   int *presentation_number);

#endif /* HDR10P_VVC_VVC_POC_H */
