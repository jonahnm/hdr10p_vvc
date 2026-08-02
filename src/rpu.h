#ifndef HDR10P_VVC_RPU_H
#define HDR10P_VVC_RPU_H

#include <stddef.h>
#include <stdint.h>

/*
 * Dolby Vision RPU (Reference Processing Unit) injection.
 *
 * The RPU is carried in the same way dovi_tool carries it in HEVC: one RPU
 * NAL unit per access unit, placed after all other NAL units of the access
 * unit. HEVC uses NAL type 62 (UNSPEC62); VVC has no published RPU NAL type
 * yet, so it is carried in a VVC "reserved non-VCL" NAL unit (default type 27,
 * RSV_NVCL_27), configurable via --rpu-nal-type.
 *
 * Input format is the dovi_tool RPU binary (e.g. `dovi_tool extract` output):
 * a sequence of RPU NAL units separated by 0x00000001 start codes, each RPU
 * payload starting with the 0x19 prefix byte and already emulation-prevented.
 */

#define VVC_DEFAULT_RPU_NAL_TYPE 27 /* VVC_RSV_NVCL_27 */

typedef struct {
    uint8_t *data; /* RPU payload (emulation prevention intact), starts 0x19 */
    size_t   len;
} rpu_frame;

/*
 * Parse a dovi_tool RPU binary file into per-frame RPU payloads.
 * Returns 0 on success; out and out_count are set (free with rpu_frames_free).
 */
int rpu_parse_file(const uint8_t *buf, size_t len, rpu_frame **out, int *out_count);
void rpu_frames_free(rpu_frame *frames, int n);

/*
 * Wrap an RPU payload into a complete VVC NAL unit (without start code).
 * Returns malloc'd bytes on success.
 */
int rpu_build_vvc_nal(const rpu_frame *frame, uint8_t nal_type, uint8_t tid_plus_1,
                      uint8_t **out, size_t *out_len);

#endif /* HDR10P_VVC_RPU_H */
