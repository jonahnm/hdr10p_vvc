#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- JSON DOM ---------------- */

static jval *j_new(j_type t)
{
    jval *v = calloc(1, sizeof(jval));
    if (v)
        v->type = t;
    return v;
}

void json_free(jval *v)
{
    if (!v)
        return;
    for (int i = 0; i < v->n; i++) {
        json_free(v->elems[i]);
        free(v->keys ? v->keys[i] : NULL);
    }
    free(v->elems);
    free(v->keys);
    if (v->type == J_STR)
        free(v->u.s);
    free(v);
}

const char *json_type_name(j_type t)
{
    switch (t) {
    case J_NULL:   return "null";
    case J_BOOL:   return "boolean";
    case J_INT:    return "integer";
    case J_DOUBLE: return "number";
    case J_STR:    return "string";
    case J_ARR:    return "array";
    case J_OBJ:    return "object";
    }
    return "unknown";
}

static int j_push_child(jval *v, jval *child, char *key)
{
    if (v->n == v->cap) {
        int ncap = v->cap ? v->cap * 2 : 8;
        jval **ne = realloc(v->elems, sizeof(jval *) * ncap);
        if (!ne)
            return -1;
        v->elems = ne;
        if (key) {
            char **nk = realloc(v->keys, sizeof(char *) * ncap);
            if (!nk)
                return -1;
            v->keys = nk;
        }
        v->cap = ncap;
    }
    v->elems[v->n] = child;
    if (key)
        v->keys[v->n] = key;
    v->n++;
    return 0;
}

typedef struct {
    const char *p;
    const char *start;
    const char *end;
    int         err;
} jparser;

static void jp_ws(jparser *jp)
{
    while (jp->p < jp->end &&
           (*jp->p == ' ' || *jp->p == '\t' || *jp->p == '\n' || *jp->p == '\r'))
        jp->p++;
}

static jval *jp_value(jparser *jp);

static char *jp_string_raw(jparser *jp)
{
    if (jp->p >= jp->end || *jp->p != '"') {
        jp->err = 1;
        return NULL;
    }
    jp->p++; /* opening quote */
    char *out = NULL;
    size_t len = 0, cap = 0;
    while (jp->p < jp->end) {
        char c = *jp->p;
        if (c == '"') {
            jp->p++;
            if (!out)
                out = calloc(1, 1);
            out[len] = '\0';
            return out;
        }
        if ((unsigned char)c < 0x20) {
            jp->err = 1;
            break;
        }
        if (len + 8 >= cap) {
            size_t ncap = cap ? cap * 2 : 16;
            char *nb = realloc(out, ncap);
            if (!nb) {
                jp->err = 1;
                break;
            }
            out = nb;
            cap = ncap;
        }
        if (c == '\\') {
            jp->p++;
            if (jp->p >= jp->end) {
                jp->err = 1;
                break;
            }
            char e = *jp->p;
            switch (e) {
            case '"':  out[len++] = '"';  break;
            case '\\': out[len++] = '\\'; break;
            case '/':  out[len++] = '/';  break;
            case 'b':  out[len++] = '\b'; break;
            case 'f':  out[len++] = '\f'; break;
            case 'n':  out[len++] = '\n'; break;
            case 'r':  out[len++] = '\r'; break;
            case 't':  out[len++] = '\t'; break;
            case 'u': {
                if (jp->end - jp->p < 5) {
                    jp->err = 1;
                    break;
                }
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = jp->p[i + 1];
                    cp <<= 4;
                    if (h >= '0' && h <= '9')       cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f')  cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')  cp |= (unsigned)(h - 'A' + 10);
                    else { jp->err = 1; cp = 0; break; }
                }
                jp->p += 4;
                if (cp < 0x80) {
                    out[len++] = (char)cp;
                } else if (cp < 0x800) {
                    out[len++] = (char)(0xC0 | (cp >> 6));
                    out[len++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[len++] = (char)(0xE0 | (cp >> 12));
                    out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[len++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                jp->err = 1;
                break;
            }
            if (jp->err)
                break;
        } else {
            out[len++] = c;
        }
        jp->p++;
    }
    free(out);
    return NULL;
}

static jval *jp_object(jparser *jp)
{
    jp->p++; /* '{' */
    jval *obj = j_new(J_OBJ);
    if (!obj) { jp->err = 1; return NULL; }
    jp_ws(jp);
    if (jp->p < jp->end && *jp->p == '}') {
        jp->p++;
        return obj;
    }
    for (;;) {
        jp_ws(jp);
        char *key = jp_string_raw(jp);
        if (!key) { jp->err = 1; break; }
        jp_ws(jp);
        if (jp->p >= jp->end || *jp->p != ':') { jp->err = 1; free(key); break; }
        jp->p++;
        jp_ws(jp);
        jval *val = jp_value(jp);
        if (!val) { jp->err = 1; free(key); break; }
        if (j_push_child(obj, val, key) < 0) { jp->err = 1; break; }
        jp_ws(jp);
        if (jp->p >= jp->end) { jp->err = 1; break; }
        if (*jp->p == ',') { jp->p++; continue; }
        if (*jp->p == '}') { jp->p++; return obj; }
        jp->err = 1;
        break;
    }
    json_free(obj);
    return NULL;
}

static jval *jp_array(jparser *jp)
{
    jp->p++; /* '[' */
    jval *arr = j_new(J_ARR);
    if (!arr) { jp->err = 1; return NULL; }
    jp_ws(jp);
    if (jp->p < jp->end && *jp->p == ']') {
        jp->p++;
        return arr;
    }
    for (;;) {
        jp_ws(jp);
        jval *val = jp_value(jp);
        if (!val) { jp->err = 1; break; }
        if (j_push_child(arr, val, NULL) < 0) { jp->err = 1; break; }
        jp_ws(jp);
        if (jp->p >= jp->end) { jp->err = 1; break; }
        if (*jp->p == ',') { jp->p++; continue; }
        if (*jp->p == ']') { jp->p++; return arr; }
        jp->err = 1;
        break;
    }
    json_free(arr);
    return NULL;
}

static jval *jp_value(jparser *jp)
{
    jp_ws(jp);
    if (jp->p >= jp->end) {
        jp->err = 1;
        return NULL;
    }
    char c = *jp->p;
    if (c == '{')
        return jp_object(jp);
    if (c == '[')
        return jp_array(jp);
    if (c == '"') {
        char *s = jp_string_raw(jp);
        if (!s) {
            jp->err = 1;
            return NULL;
        }
        jval *v = j_new(J_STR);
        if (!v) { free(s); jp->err = 1; return NULL; }
        v->u.s = s;
        return v;
    }
    if (c == 't' && jp->end - jp->p >= 4 && !memcmp(jp->p, "true", 4)) {
        jp->p += 4;
        jval *v = j_new(J_BOOL);
        if (v) v->u.b = 1;
        return v;
    }
    if (c == 'f' && jp->end - jp->p >= 5 && !memcmp(jp->p, "false", 5)) {
        jp->p += 5;
        jval *v = j_new(J_BOOL);
        if (v) v->u.b = 0;
        return v;
    }
    if (c == 'n' && jp->end - jp->p >= 4 && !memcmp(jp->p, "null", 4)) {
        jp->p += 4;
        return j_new(J_NULL);
    }
    /* number */
    {
        const char *start = jp->p;
        if (c == '-')
            jp->p++;
        while (jp->p < jp->end &&
               ((*jp->p >= '0' && *jp->p <= '9') || *jp->p == '.' ||
                *jp->p == 'e' || *jp->p == 'E' || *jp->p == '+' || *jp->p == '-'))
            jp->p++;
        if (jp->p == start) {
            jp->err = 1;
            return NULL;
        }
        char tmp[64];
        size_t n = (size_t)(jp->p - start);
        if (n >= sizeof(tmp)) {
            jp->err = 1;
            return NULL;
        }
        memcpy(tmp, start, n);
        tmp[n] = '\0';
        jval *v = NULL;
        if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E')) {
            char *endp = NULL;
            long long ll = strtoll(tmp, &endp, 10);
            if (endp && *endp == '\0') {
                v = j_new(J_INT);
                if (v) v->u.i = (int64_t)ll;
            }
        }
        if (!v) {
            char *endp = NULL;
            double d = strtod(tmp, &endp);
            if (!endp || *endp != '\0') {
                jp->err = 1;
                return NULL;
            }
            v = j_new(J_DOUBLE);
            if (v) v->u.d = d;
        }
        return v;
    }
}

jval *json_parse_str(const char *text, size_t len)
{
    jparser jp = { text, text, text + len, 0 };
    jval *v = jp_value(&jp);
    if (!v || jp.err) {
        json_free(v);
        return NULL;
    }
    return v;
}

jval *json_parse_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    jval *v = json_parse_str(buf, rd);
    free(buf);
    return v;
}

const jval *json_obj_get(const jval *obj, const char *key)
{
    if (!obj || obj->type != J_OBJ)
        return NULL;
    for (int i = 0; i < obj->n; i++) {
        if (obj->keys[i] && !strcmp(obj->keys[i], key))
            return obj->elems[i];
    }
    return NULL;
}

int json_get_int(const jval *obj, const char *key, int64_t *out, int required)
{
    const jval *v = json_obj_get(obj, key);
    if (!v) {
        if (required)
            return -1;
        *out = 0;
        return 0;
    }
    if (v->type == J_INT) {
        *out = v->u.i;
        return 0;
    }
    if (v->type == J_DOUBLE) {
        *out = (int64_t)v->u.d;
        return 0;
    }
    return required ? -1 : 0;
}

int json_get_uint(const jval *obj, const char *key, uint64_t *out, int required)
{
    int64_t v = 0;
    if (json_get_int(obj, key, &v, required) < 0)
        return -1;
    if (v < 0)
        return required ? -1 : 0;
    *out = (uint64_t)v;
    return 0;
}

int json_arr_len(const jval *arr)
{
    if (!arr || arr->type != J_ARR)
        return 0;
    return arr->n;
}

int json_arr_int(const jval *arr, int i, uint64_t *out)
{
    if (!arr || arr->type != J_ARR || i < 0 || i >= arr->n)
        return 0;
    const jval *v = arr->elems[i];
    int64_t x;
    if (v->type == J_INT) {
        x = v->u.i;
    } else if (v->type == J_DOUBLE) {
        x = (int64_t)v->u.d;
    } else {
        return 0;
    }
    if (x < 0)
        return 0;
    *out = (uint64_t)x;
    return 1;
}

/* ---------------- JSON writer ---------------- */

void jbuf_init(jbuf *b)
{
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

void jbuf_free(jbuf *b)
{
    free(b->buf);
    b->buf = NULL;
    b->len = b->cap = 0;
}

static void jbuf_reserve(jbuf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap)
        return;
    size_t ncap = b->cap ? b->cap : 128;
    while (ncap < b->len + extra + 1)
        ncap *= 2;
    char *nb = realloc(b->buf, ncap);
    if (!nb)
        return;
    b->buf = nb;
    b->cap = ncap;
}

void jbuf_puts(jbuf *b, const char *s)
{
    size_t n = strlen(s);
    jbuf_reserve(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    if (b->buf)
        b->buf[b->len] = '\0';
}

void jbuf_printf(jbuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    jbuf_reserve(b, (size_t)n);
    vsnprintf(b->buf + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

const char *jbuf_cstr(jbuf *b)
{
    if (!b->buf) {
        b->buf = calloc(1, 1);
        b->cap = 1;
    }
    b->buf[b->len] = '\0';
    return b->buf;
}

int jbuf_write_file(jbuf *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t n = b->len;
    if (fwrite(b->buf ? b->buf : (const char *)"", 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
        return -1;
    return 0;
}
