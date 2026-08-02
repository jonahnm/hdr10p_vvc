#include "st2094.h"
#include "bitbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void st2094_init(st2094_meta *m)
{
    memset(m, 0, sizeof(*m));
    m->country_code                  = ST2094_COUNTRY_CODE;
    m->terminal_provider_code        = ST2094_TERMINAL_PROVIDER;
    m->terminal_provider_oriented_code = ST2094_PROVIDER_ORIENTED;
    m->application_identifier        = ST2094_APPLICATION_ID;
    m->application_version           = ST2094_APPLICATION_VERSION;
    m->num_windows                   = 1;
}

static void st2094_free_peak(st2094_peak_table *t)
{
    free(t->cells);
    t->cells = NULL;
    t->num_rows = t->num_cols = 0;
}

void st2094_free(st2094_meta *m)
{
    st2094_free_peak(&m->atsd);
    st2094_free_peak(&m->amd);
}

int st2094_detect(const uint8_t *data, size_t len)
{
    if (len < 8)
        return 0;
    if (data[0] != ST2094_COUNTRY_CODE)
        return 0;
    uint16_t provider = (uint16_t)((data[1] << 8) | data[2]);
    uint16_t oriented = (uint16_t)((data[3] << 8) | data[4]);
    if (provider != ST2094_TERMINAL_PROVIDER)
        return 0;
    if (oriented != ST2094_PROVIDER_ORIENTED)
        return 0;
    if (data[5] != ST2094_APPLICATION_ID)
        return 0;
    if (data[6] != ST2094_APPLICATION_VERSION)
        return 0;
    return 1;
}

/* ---------- encode ---------- */

static int bw_u(bit_writer *w, uint32_t value, int nbits)
{
    return bw_write(w, value, nbits);
}

static int encode_peak_table(bit_writer *w, const st2094_peak_table *t)
{
    if (t->num_rows > 31 || t->num_cols > 31)
        return -1;
    if (bw_u(w, t->num_rows, 5) < 0 || bw_u(w, t->num_cols, 5) < 0)
        return -1;
    for (int i = 0; i < (int)(t->num_rows * t->num_cols); i++) {
        if (t->cells[i] > 15)
            return -1;
        if (bw_u(w, t->cells[i], 4) < 0)
            return -1;
    }
    return 0;
}

int st2094_encode(const st2094_meta *m, int with_country_code,
                  uint8_t **out, size_t *out_len)
{
    bit_writer w;
    bw_init(&w);

    if (with_country_code && bw_u(&w, m->country_code, 8) < 0)
        goto fail;
    if (bw_u(&w, m->terminal_provider_code, 16) < 0)
        goto fail;
    if (bw_u(&w, m->terminal_provider_oriented_code, 16) < 0)
        goto fail;
    if (bw_u(&w, m->application_identifier, 8) < 0)
        goto fail;
    if (bw_u(&w, m->application_version, 8) < 0)
        goto fail;

    if (m->num_windows < 1 || m->num_windows > ST2094_MAX_WINDOWS)
        goto fail;
    if (bw_u(&w, m->num_windows, 2) < 0)
        goto fail;

    for (int i = 0; i + 1 < m->num_windows; i++) {
        const st2094_window *pw = &m->processing_windows[i];
        if (bw_u(&w, pw->window_upper_left_corner_x, 16) < 0 ||
            bw_u(&w, pw->window_upper_left_corner_y, 16) < 0 ||
            bw_u(&w, pw->window_lower_right_corner_x, 16) < 0 ||
            bw_u(&w, pw->window_lower_right_corner_y, 16) < 0 ||
            bw_u(&w, pw->center_of_ellipse_x, 16) < 0 ||
            bw_u(&w, pw->center_of_ellipse_y, 16) < 0 ||
            bw_u(&w, pw->rotation_angle, 8) < 0 ||
            bw_u(&w, pw->semimajor_axis_internal_ellipse, 16) < 0 ||
            bw_u(&w, pw->semimajor_axis_external_ellipse, 16) < 0 ||
            bw_u(&w, pw->semiminor_axis_external_ellipse, 16) < 0 ||
            bw_u(&w, pw->overlap_process_option, 1) < 0)
            goto fail;
    }

    if (bw_u(&w, m->targeted_system_display_maximum_luminance, 27) < 0)
        goto fail;
    if (bw_u(&w, m->targeted_system_display_actual_peak_luminance_flag, 1) < 0)
        goto fail;
    if (m->targeted_system_display_actual_peak_luminance_flag) {
        if (encode_peak_table(&w, &m->atsd) < 0)
            goto fail;
    }

    for (int win = 0; win < m->num_windows; win++) {
        for (int i = 0; i < 3; i++) {
            if (m->maxscl[i] > 0x1FFFF || bw_u(&w, m->maxscl[i], 17) < 0)
                goto fail;
        }
        if (m->average_maxrgb > 0x1FFFF || bw_u(&w, m->average_maxrgb, 17) < 0)
            goto fail;
        if (m->num_distribution_maxrgb_percentiles > ST2094_MAX_DISTRIBUTION)
            goto fail;
        if (bw_u(&w, m->num_distribution_maxrgb_percentiles, 4) < 0)
            goto fail;
        for (int i = 0; i < m->num_distribution_maxrgb_percentiles; i++) {
            if (m->distribution[i].percentage > 127)
                goto fail;
            if (m->distribution[i].percentile > 0x1FFFF)
                goto fail;
            if (bw_u(&w, m->distribution[i].percentage, 7) < 0 ||
                bw_u(&w, m->distribution[i].percentile, 17) < 0)
                goto fail;
        }
        if (m->fraction_bright_pixels > 0x3FF ||
            bw_u(&w, m->fraction_bright_pixels, 10) < 0)
            goto fail;
    }

    if (bw_u(&w, m->mastering_display_actual_peak_luminance_flag, 1) < 0)
        goto fail;
    if (m->mastering_display_actual_peak_luminance_flag) {
        if (encode_peak_table(&w, &m->amd) < 0)
            goto fail;
    }

    for (int win = 0; win < m->num_windows; win++) {
        if (bw_u(&w, m->tone_mapping_flag, 1) < 0)
            goto fail;
        if (m->tone_mapping_flag) {
            if (m->knee_point_x > 0xFFF || m->knee_point_y > 0xFFF)
                goto fail;
            if (m->num_bezier_curve_anchors > ST2094_MAX_BEZIER_ANCHORS)
                goto fail;
            if (bw_u(&w, m->knee_point_x, 12) < 0 ||
                bw_u(&w, m->knee_point_y, 12) < 0 ||
                bw_u(&w, m->num_bezier_curve_anchors, 4) < 0)
                goto fail;
            for (int i = 0; i < m->num_bezier_curve_anchors; i++) {
                if (m->bezier_curve_anchors[i] > 0x3FF ||
                    bw_u(&w, m->bezier_curve_anchors[i], 10) < 0)
                    goto fail;
            }
        }
    }

    if (bw_u(&w, m->color_saturation_mapping_flag, 1) < 0)
        goto fail;
    if (m->color_saturation_mapping_flag) {
        if (m->color_saturation_weight > 0x3F ||
            bw_u(&w, m->color_saturation_weight, 6) < 0)
            goto fail;
    }

    bw_align(&w);

    *out = bw_bytes(&w, out_len);
    bw_free(&w);
    return *out ? 0 : -1;

fail:
    bw_free(&w);
    return -1;
}

/* ---------- decode ---------- */

static int decode_peak_table(bit_reader *r, st2094_peak_table *t)
{
    uint32_t rows = 0, cols = 0;
    if (br_read(r, &rows, 5) < 0 || br_read(r, &cols, 5) < 0)
        return -1;
    if (rows == 0 || cols == 0 || rows > 31 || cols > 31)
        return -1;
    t->num_rows = (uint8_t)rows;
    t->num_cols = (uint8_t)cols;
    int n = (int)(rows * cols);
    t->cells = malloc((size_t)n);
    if (!t->cells)
        return -1;
    for (int i = 0; i < n; i++) {
        uint32_t v;
        if (br_read(r, &v, 4) < 0)
            return -1;
        t->cells[i] = (uint8_t)v;
    }
    return 0;
}

int st2094_decode(const uint8_t *data, size_t len, st2094_meta *m)
{
    st2094_init(m);
    bit_reader r;
    br_init(&r, data, len);
    uint32_t v;

    if (br_read(&r, &v, 8) < 0)
        goto fail;
    m->country_code = (uint8_t)v;
    if (br_read(&r, &v, 16) < 0)
        goto fail;
    m->terminal_provider_code = (uint16_t)v;
    if (br_read(&r, &v, 16) < 0)
        goto fail;
    m->terminal_provider_oriented_code = (uint16_t)v;
    if (br_read(&r, &v, 8) < 0)
        goto fail;
    m->application_identifier = (uint8_t)v;
    if (br_read(&r, &v, 8) < 0)
        goto fail;
    m->application_version = (uint8_t)v;

    if (m->application_identifier != ST2094_APPLICATION_ID ||
        m->application_version != ST2094_APPLICATION_VERSION)
        goto fail;

    if (br_read(&r, &v, 2) < 0)
        goto fail;
    m->num_windows = (uint8_t)v;
    if (m->num_windows < 1 || m->num_windows > ST2094_MAX_WINDOWS)
        goto fail;

    for (int i = 0; i + 1 < m->num_windows; i++) {
        st2094_window *pw = &m->processing_windows[i];
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->window_upper_left_corner_x = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->window_upper_left_corner_y = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->window_lower_right_corner_x = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->window_lower_right_corner_y = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->center_of_ellipse_x = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->center_of_ellipse_y = (uint16_t)v;
        if (br_read(&r, &v, 8) < 0) goto fail;
        pw->rotation_angle = (uint8_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->semimajor_axis_internal_ellipse = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->semimajor_axis_external_ellipse = (uint16_t)v;
        if (br_read(&r, &v, 16) < 0) goto fail;
        pw->semiminor_axis_external_ellipse = (uint16_t)v;
        if (br_read(&r, &v, 1) < 0) goto fail;
        pw->overlap_process_option = (uint8_t)v;
    }

    if (br_read(&r, &v, 27) < 0)
        goto fail;
    m->targeted_system_display_maximum_luminance = v;

    if (br_read(&r, &v, 1) < 0)
        goto fail;
    m->targeted_system_display_actual_peak_luminance_flag = (uint8_t)v;
    if (m->targeted_system_display_actual_peak_luminance_flag) {
        if (decode_peak_table(&r, &m->atsd) < 0)
            goto fail;
    }

    for (int win = 0; win < m->num_windows; win++) {
        for (int i = 0; i < 3; i++) {
            if (br_read(&r, &v, 17) < 0)
                goto fail;
            m->maxscl[i] = v;
        }
        if (br_read(&r, &v, 17) < 0)
            goto fail;
        m->average_maxrgb = v;
        if (br_read(&r, &v, 4) < 0)
            goto fail;
        m->num_distribution_maxrgb_percentiles = (uint8_t)v;
        if (m->num_distribution_maxrgb_percentiles > ST2094_MAX_DISTRIBUTION)
            goto fail;
        for (int i = 0; i < m->num_distribution_maxrgb_percentiles; i++) {
            if (br_read(&r, &v, 7) < 0)
                goto fail;
            m->distribution[i].percentage = (uint8_t)v;
            if (br_read(&r, &v, 17) < 0)
                goto fail;
            m->distribution[i].percentile = v;
        }
        if (br_read(&r, &v, 10) < 0)
            goto fail;
        m->fraction_bright_pixels = (uint16_t)v;
    }

    if (br_read(&r, &v, 1) < 0)
        goto fail;
    m->mastering_display_actual_peak_luminance_flag = (uint8_t)v;
    if (m->mastering_display_actual_peak_luminance_flag) {
        if (decode_peak_table(&r, &m->amd) < 0)
            goto fail;
    }

    for (int win = 0; win < m->num_windows; win++) {
        if (br_read(&r, &v, 1) < 0)
            goto fail;
        m->tone_mapping_flag = (uint8_t)v;
        if (m->tone_mapping_flag) {
            if (br_read(&r, &v, 12) < 0)
                goto fail;
            m->knee_point_x = (uint16_t)v;
            if (br_read(&r, &v, 12) < 0)
                goto fail;
            m->knee_point_y = (uint16_t)v;
            if (br_read(&r, &v, 4) < 0)
                goto fail;
            m->num_bezier_curve_anchors = (uint8_t)v;
            if (m->num_bezier_curve_anchors > ST2094_MAX_BEZIER_ANCHORS)
                goto fail;
            for (int i = 0; i < m->num_bezier_curve_anchors; i++) {
                if (br_read(&r, &v, 10) < 0)
                    goto fail;
                m->bezier_curve_anchors[i] = (uint16_t)v;
            }
        }
    }

    if (br_read(&r, &v, 1) < 0)
        goto fail;
    m->color_saturation_mapping_flag = (uint8_t)v;
    if (m->color_saturation_mapping_flag) {
        if (br_read(&r, &v, 6) < 0)
            goto fail;
        m->color_saturation_weight = (uint8_t)v;
    }

    m->valid = 1;
    return 0;

fail:
    st2094_free(m);
    return -1;
}

/* ---------- JSON conversion ---------- */

int st2094_from_json(const jval *entry, st2094_meta *m)
{
    st2094_init(m);

    uint64_t u;
    if (json_get_uint(entry, "NumberOfWindows", &u, 1) < 0)
        return -1;
    m->num_windows = (uint8_t)u;
    if (m->num_windows != 1) {
        fprintf(stderr,
                "error: NumberOfWindows != 1 (got %u) is not supported\n",
                m->num_windows);
        return -1;
    }

    if (json_get_uint(entry, "TargetedSystemDisplayMaximumLuminance", &u, 1) < 0)
        return -1;
    m->targeted_system_display_maximum_luminance = (uint32_t)u;

    const jval *lp = json_obj_get(entry, "LuminanceParameters");
    if (!lp || lp->type != J_OBJ) {
        fprintf(stderr, "error: missing LuminanceParameters\n");
        return -1;
    }

    if (json_get_uint(lp, "AverageRGB", &u, 1) < 0)
        return -1;
    m->average_maxrgb = (uint32_t)u;

    const jval *maxscl = json_obj_get(lp, "MaxScl");
    if (!maxscl || maxscl->type != J_ARR || json_arr_len(maxscl) != 3)
        return -1;
    for (int i = 0; i < 3; i++) {
        if (!json_arr_int(maxscl, i, &u))
            return -1;
        m->maxscl[i] = (uint32_t)u;
    }

    const jval *ld = json_obj_get(lp, "LuminanceDistributions");
    if (!ld || ld->type != J_OBJ) {
        fprintf(stderr, "error: missing LuminanceParameters.LuminanceDistributions\n");
        return -1;
    }
    const jval *di = json_obj_get(ld, "DistributionIndex");
    const jval *dv = json_obj_get(ld, "DistributionValues");
    if (!di || !dv || di->type != J_ARR || dv->type != J_ARR)
        return -1;
    int ni = json_arr_len(di);
    int nv = json_arr_len(dv);
    if (ni != nv || ni < 1 || ni > ST2094_MAX_DISTRIBUTION)
        return -1;
    m->num_distribution_maxrgb_percentiles = (uint8_t)ni;
    for (int i = 0; i < ni; i++) {
        uint64_t pct, val;
        if (!json_arr_int(di, i, &pct) || !json_arr_int(dv, i, &val))
            return -1;
        m->distribution[i].percentage = (uint8_t)pct;
        m->distribution[i].percentile = (uint32_t)val;
    }

    const jval *bc = json_obj_get(entry, "BezierCurveData");
    if (bc && bc->type == J_OBJ) {
        m->tone_mapping_flag = 1;
        if (json_get_uint(bc, "KneePointX", &u, 1) < 0)
            return -1;
        m->knee_point_x = (uint16_t)u;
        if (json_get_uint(bc, "KneePointY", &u, 1) < 0)
            return -1;
        m->knee_point_y = (uint16_t)u;
        const jval *anchors = json_obj_get(bc, "Anchors");
        if (!anchors || anchors->type != J_ARR)
            return -1;
        int na = json_arr_len(anchors);
        if (na < 1 || na > ST2094_MAX_BEZIER_ANCHORS)
            return -1;
        m->num_bezier_curve_anchors = (uint8_t)na;
        for (int i = 0; i < na; i++) {
            if (!json_arr_int(anchors, i, &u))
                return -1;
            m->bezier_curve_anchors[i] = (uint16_t)u;
        }
    }

    m->valid = 1;
    return 0;
}

static void jbuf_i64(jbuf *b, int64_t v)
{
    jbuf_printf(b, "%lld", (long long)v);
}

void st2094_to_json(const st2094_meta *m, int scene_frame_index, int scene_id,
                    int sequence_frame_index, jbuf *b)
{
    jbuf_puts(b, "{");

    if (m->tone_mapping_flag) {
        jbuf_puts(b, "\"BezierCurveData\":{\"Anchors\":[");
        for (int i = 0; i < m->num_bezier_curve_anchors; i++) {
            if (i)
                jbuf_puts(b, ",");
            jbuf_i64(b, m->bezier_curve_anchors[i]);
        }
        jbuf_puts(b, "],\"KneePointX\":");
        jbuf_i64(b, m->knee_point_x);
        jbuf_puts(b, ",\"KneePointY\":");
        jbuf_i64(b, m->knee_point_y);
        jbuf_puts(b, "},");
    }

    jbuf_puts(b, "\"LuminanceParameters\":{");
    jbuf_puts(b, "\"AverageRGB\":");
    jbuf_i64(b, m->average_maxrgb);
    jbuf_puts(b, ",\"LuminanceDistributions\":{\"DistributionIndex\":[");
    for (int i = 0; i < m->num_distribution_maxrgb_percentiles; i++) {
        if (i)
            jbuf_puts(b, ",");
        jbuf_i64(b, m->distribution[i].percentage);
    }
    jbuf_puts(b, "],\"DistributionValues\":[");
    for (int i = 0; i < m->num_distribution_maxrgb_percentiles; i++) {
        if (i)
            jbuf_puts(b, ",");
        jbuf_i64(b, m->distribution[i].percentile);
    }
    jbuf_puts(b, "]}");
    jbuf_puts(b, ",\"MaxScl\":[");
    jbuf_i64(b, m->maxscl[0]);
    jbuf_puts(b, ",");
    jbuf_i64(b, m->maxscl[1]);
    jbuf_puts(b, ",");
    jbuf_i64(b, m->maxscl[2]);
    jbuf_puts(b, "]}");

    jbuf_puts(b, ",\"NumberOfWindows\":");
    jbuf_i64(b, m->num_windows);
    jbuf_puts(b, ",\"TargetedSystemDisplayMaximumLuminance\":");
    jbuf_i64(b, m->targeted_system_display_maximum_luminance);

    jbuf_puts(b, ",\"SceneFrameIndex\":");
    jbuf_i64(b, scene_frame_index);
    jbuf_puts(b, ",\"SceneId\":");
    jbuf_i64(b, scene_id);
    jbuf_puts(b, ",\"SequenceFrameIndex\":");
    jbuf_i64(b, sequence_frame_index);

    jbuf_puts(b, "}");
}
