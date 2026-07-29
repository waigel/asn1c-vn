/*
 * vn_writer.c -- output sink, indentation and mode policy.
 *
 * Knows about bytes, indentation and comments; nothing about ASN.1 types.
 * Failures are sticky: once the writer has failed, every call is a no-op that
 * returns -1, so handlers may defer their error checks.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vn_internal.h"

const vn_type_names_t *
vn_annotations_find(const vn_annotations_t *ann, const char *type_name) {
    size_t i;

    if(!ann || !type_name || !type_name[0]) return 0;
    for(i = 0; i < ann->count; i++)
        if(ann->types[i].type_name
           && strcmp(ann->types[i].type_name, type_name) == 0)
            return &ann->types[i];
    return 0;
}

/*
 * Whether a descriptor name is one the schema wrote, which is where a scope
 * path starts over.
 *
 * 12.2 requires a typereference to begin with an upper-case letter and 12.3
 * requires a member identifier to begin with a lower-case one, which separates
 * the two cases: asn1c names an anonymous inline type after the member it hangs
 * off ("inner"), so that name continues the path, while a real type name
 * ("AlgoParameter") begins a fresh one -- and asn1c keys its enum that way too.
 * A list's anonymous element type is called after the built-in instead, and
 * 12.38 reserves those words so no schema type can be confused with them.
 */
static int
vn_scope_restarts_at(const char *name) {
    static const char *const anonymous[] = {"SEQUENCE", "SET", "CHOICE",
                                            "SEQUENCE OF", "SET OF"};
    size_t i;

    if(!name || name[0] < 'A' || name[0] > 'Z') return 0;
    for(i = 0; i < sizeof anonymous / sizeof anonymous[0]; i++)
        if(strcmp(name, anonymous[i]) == 0) return 0;
    return 1;
}

void
vn_member_key(char *dst, size_t dstsz, const char *parent, const char *member) {
    char        tmp[VN_MEMBER_KEY_MAX];
    const char *base;

    if(!dst || dstsz == 0) return;
    base = vn_scope_restarts_at(parent) ? parent : (dst[0] ? dst : parent);
    if(!base || !base[0] || !member || !member[0]) {
        dst[0] = '\0';
        return;
    }
    snprintf(tmp, sizeof tmp, "%s__%s", base, member);
    snprintf(dst, dstsz, "%s", tmp);
}

const vn_type_names_t *
vn_names_for(const vn_annotations_t *ann, const char *scoped_key,
             const asn_TYPE_descriptor_t *td) {
    const vn_type_names_t *n = 0;

    /* The scoped key wins: an inline member shares asn_DEF_NativeInteger with
     * every other plain INTEGER, so the type name would match the wrong thing or
     * nothing at all. */
    if(scoped_key && scoped_key[0]) n = vn_annotations_find(ann, scoped_key);
    if(!n && td) n = vn_annotations_find(ann, td->name);
    return n;
}

void
vn_writer_init(vn_writer_t *w, const vn_options_t *opts,
               asn_app_consume_bytes_f *cb, void *key) {
    memset(w, 0, sizeof(*w));
    w->cb = cb;
    w->key = key;
    w->mode = opts ? opts->mode : VN_MODE_PRETTY;
    w->indent_width = (opts && opts->indent_width > 0) ? opts->indent_width : 4;
    w->line_width = (opts && opts->line_width > 0) ? opts->line_width : 76;
    w->flags = opts ? opts->flags : 0u;
    w->errbuf = opts ? opts->errbuf : 0;
    w->errlen = opts ? opts->errlen : 0;
    w->annotations = opts ? opts->annotations : 0;

    /* Canonical output must not vary with caller preferences, or two callers
     * could produce differing "canonical" text for the same value. */
    if(w->mode == VN_MODE_CANONICAL) {
        w->indent_width = 2;
        w->line_width = 0;
    }
}

int
vn_fail(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
        const char *fmt, ...) {
    if(!w->failed) {
        w->failed = 1;
        w->failed_td = td;
        w->failed_sptr = sptr;
        if(w->errbuf && w->errlen) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(w->errbuf, w->errlen, fmt, ap);
            va_end(ap);
        }
    }
    return -1;
}

int
vn_put(vn_writer_t *w, const char *s, size_t len) {
    if(w->failed) return -1;
    if(len == 0) return 0;
    if(!w->cb) return vn_fail(w, w->failed_td, w->failed_sptr,
                              "no output callback supplied");
    if(w->cb(s, len, w->key) < 0)
        return vn_fail(w, w->failed_td, w->failed_sptr,
                       "output callback failed");
    w->written += len;
    return 0;
}

int
vn_puts(vn_writer_t *w, const char *s) {
    return vn_put(w, s, strlen(s));
}

int
vn_putc(vn_writer_t *w, char c) {
    return vn_put(w, &c, 1);
}

int
vn_printf(vn_writer_t *w, const char *fmt, ...) {
    char scratch[128];
    va_list ap;
    int n;

    if(w->failed) return -1;
    va_start(ap, fmt);
    n = vsnprintf(scratch, sizeof scratch, fmt, ap);
    va_end(ap);
    if(n < 0 || (size_t)n >= sizeof scratch)
        return vn_fail(w, w->failed_td, w->failed_sptr,
                       "internal: formatted value exceeds %u bytes",
                       (unsigned)sizeof scratch);
    return vn_put(w, scratch, (size_t)n);
}

int
vn_break(vn_writer_t *w, int level) {
    /* No terminator is needed: vn_put takes an explicit length. */
    static const char spaces[] = "                ";
    int n = level * w->indent_width;

    if(vn_putc(w, '\n') < 0) return -1;
    while(n > 0) {
        int room = (int)sizeof spaces - 1;
        int chunk = n > room ? room : n;
        if(vn_put(w, spaces, (size_t)chunk) < 0) return -1;
        n -= chunk;
    }
    return 0;
}

int
vn_is_annotated(const vn_writer_t *w) {
    return w->mode == VN_MODE_ANNOTATED;
}

int
vn_comment(vn_writer_t *w, const char *fmt, ...) {
    char scratch[192];
    va_list ap;
    char *p;
    int n;

    if(w->failed) return -1;
    if(!vn_is_annotated(w)) return 0;

    va_start(ap, fmt);
    n = vsnprintf(scratch, sizeof scratch, fmt, ap);
    va_end(ap);
    if(n < 0) return vn_fail(w, 0, 0, "internal: bad comment format");

    /* X.680 11.6: a comment ends at "--", so the text must not contain one. */
    for(p = scratch; p[0] && p[1]; p++)
        if(p[0] == '-' && p[1] == '-') p[0] = '~';

    if(vn_puts(w, "-- ") < 0) return -1;
    if(vn_puts(w, scratch) < 0) return -1;
    return vn_puts(w, " --");
}
