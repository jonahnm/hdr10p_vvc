#ifndef HDR10P_VVC_JSON_H
#define HDR10P_VVC_JSON_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal JSON DOM. Enough to read the HDR10+ SceneInfo files produced by
 * hdr10plus_tool / Samsung tools and to emit JSON on extraction.
 */

typedef enum {
    J_NULL,
    J_BOOL,
    J_INT,   /* integer-valued number, stored exactly in i */
    J_DOUBLE,/* non-integer number, stored in d */
    J_STR,
    J_ARR,
    J_OBJ,
} j_type;

typedef struct jval jval;

struct jval {
    j_type type;
    union {
        int      b;
        int64_t  i;
        double   d;
        char    *s;
    } u;
    jval   **elems; /* array elements or object values */
    char   **keys;  /* object keys (parallel to elems) */
    int      n;     /* number of children */
    int      cap;
};

jval *json_parse_file(const char *path);
jval *json_parse_str(const char *text, size_t len);
void  json_free(jval *v);

const char *json_type_name(j_type t);

/* Object helpers. Returns NULL when not present or type mismatch. */
const jval *json_obj_get(const jval *obj, const char *key);
int  json_get_int(const jval *obj, const char *key, int64_t *out, int required);
int  json_get_uint(const jval *obj, const char *key, uint64_t *out, int required);
/* Returns 1 if the array element at index i is a JSON int >= 0, 0 otherwise. */
int  json_arr_int(const jval *arr, int i, uint64_t *out);
int  json_arr_len(const jval *arr);

/* Simple growing string builder for emitting JSON. */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} jbuf;

void  jbuf_init(jbuf *b);
void  jbuf_free(jbuf *b);
void  jbuf_puts(jbuf *b, const char *s);
void  jbuf_printf(jbuf *b, const char *fmt, ...);
const char *jbuf_cstr(jbuf *b);
int   jbuf_write_file(jbuf *b, const char *path);

#endif /* HDR10P_VVC_JSON_H */
