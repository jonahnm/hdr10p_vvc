#ifndef HDR10P_VVC_ST2094_H
#define HDR10P_VVC_ST2094_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"

/*
 * SMPTE ST 2094-40 dynamic HDR metadata (HDR10+).
 *
 * The binary representation implemented here matches the payload produced and
 * consumed by hdr10plus_tool (Samsung/HDR10+ tools) and defined in
 * SMPTE ST 2094-40, application identifier 4, version 1:
 *
 *   country_code                              u(8)
 *   terminal_provider_code                    u(16)
 *   terminal_provider_oriented_code           u(16)
 *   application_identifier                    u(8)
 *   application_version                       u(8)
 *   num_windows                               u(2)
 *   (num_windows - 1) processing windows
 *   targeted_system_display_maximum_luminance u(27)
 *   targeted_system_display_actual_peak_luminance_flag u(1)
 *   [targeted system display peak luminance table]
 *   per window: maxscl[3] u(17), average_maxrgb u(17),
 *               num_distribution_maxrgb_percentiles u(4),
 *               distribution (percentage u(7), percentile u(17)),
 *               fraction_bright_pixels u(10)
 *   mastering_display_actual_peak_luminance_flag u(1)
 *   [mastering display peak luminance table]
 *   per window: tone_mapping_flag u(1) [+ bezier curve]
 *   color_saturation_mapping_flag u(1) [+ color_saturation_weight u(6)]
 *   byte aligned (zero padded)
 */

#define ST2094_COUNTRY_CODE        0xB5
#define ST2094_TERMINAL_PROVIDER   0x003C
#define ST2094_PROVIDER_ORIENTED   0x0001
#define ST2094_APPLICATION_ID      4
#define ST2094_APPLICATION_VERSION 1

#define ST2094_MAX_WINDOWS 4
#define ST2094_MAX_DISTRIBUTION 10
#define ST2094_MAX_BEZIER_ANCHORS 9

typedef struct {
    uint16_t window_upper_left_corner_x;
    uint16_t window_upper_left_corner_y;
    uint16_t window_lower_right_corner_x;
    uint16_t window_lower_right_corner_y;
    uint16_t center_of_ellipse_x;
    uint16_t center_of_ellipse_y;
    uint8_t  rotation_angle;
    uint16_t semimajor_axis_internal_ellipse;
    uint16_t semimajor_axis_external_ellipse;
    uint16_t semiminor_axis_external_ellipse;
    uint8_t  overlap_process_option;
} st2094_window;

typedef struct {
    uint8_t  percentage; /* 7-bit */
    uint32_t percentile; /* 17-bit, nits * 10 */
} st2094_distribution;

typedef struct {
    uint8_t  num_rows;
    uint8_t  num_cols;
    uint8_t *cells; /* num_rows * num_cols, 4-bit each */
} st2094_peak_table;

typedef struct {
    /* ST 2094-40 / ITU-T T.35 application header */
    uint8_t  country_code;
    uint16_t terminal_provider_code;
    uint16_t terminal_provider_oriented_code;
    uint8_t  application_identifier;
    uint8_t  application_version;

    uint8_t num_windows; /* 1..4 */
    st2094_window processing_windows[ST2094_MAX_WINDOWS - 1];

    uint32_t targeted_system_display_maximum_luminance; /* 27-bit, nits */

    uint8_t targeted_system_display_actual_peak_luminance_flag;
    st2094_peak_table atsd;

    uint32_t maxscl[3];                        /* 17-bit, nits * 10 */
    uint32_t average_maxrgb;                   /* 17-bit, nits * 10 */
    uint8_t  num_distribution_maxrgb_percentiles;
    st2094_distribution distribution[ST2094_MAX_DISTRIBUTION];
    uint16_t fraction_bright_pixels;           /* 10-bit, nits * 10 */

    uint8_t mastering_display_actual_peak_luminance_flag;
    st2094_peak_table amd;

    uint8_t  tone_mapping_flag;
    uint16_t knee_point_x;                          /* 12-bit */
    uint16_t knee_point_y;                          /* 12-bit */
    uint8_t  num_bezier_curve_anchors;
    uint16_t bezier_curve_anchors[ST2094_MAX_BEZIER_ANCHORS]; /* 10-bit each */

    uint8_t color_saturation_mapping_flag;
    uint8_t color_saturation_weight; /* 6-bit */

    int valid; /* 1 after a successful decode / population */
} st2094_meta;

void st2094_init(st2094_meta *m);
void st2094_free(st2094_meta *m);

/* Return 1 if data looks like an ST 2094-40 (app 4 / v1) payload. */
int st2094_detect(const uint8_t *data, size_t len);

/*
 * Encode the metadata. When with_country_code is non-zero the payload starts
 * with the 8-bit country code (used for VVC/HEVC T.35 user data and AV1
 * metadata OBUs when required). Returns a malloc'd buffer on success.
 */
int st2094_encode(const st2094_meta *m, int with_country_code,
                  uint8_t **out, size_t *out_len);

/* Decode a payload that starts with the country code byte. */
int st2094_decode(const uint8_t *data, size_t len, st2094_meta *m);

/* Parse a hdr10plus_tool "SceneInfo" JSON entry into the binary model. */
int st2094_from_json(const jval *entry, st2094_meta *m);

/* Serialize the binary model back to a "SceneInfo" JSON entry. The
 * scene/sequence frame indices are emitted inside the entry object. */
void st2094_to_json(const st2094_meta *m, int scene_frame_index, int scene_id,
                    int sequence_frame_index, jbuf *b);

#endif /* HDR10P_VVC_ST2094_H */
