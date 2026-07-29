/*
 * vn_internal.h -- shared by the encoder sources only, not installed.
 */
#ifndef VN_INTERNAL_H
#define VN_INTERNAL_H

#include <sys/types.h>
#include "vn_encoder.h"

/*
 * Longest scope path the annotation lookup can hold. A path that would exceed
 * it is truncated and simply stops matching, so the numeric form is used --
 * a readability loss, never a wrong identifier.
 */
#define VN_MEMBER_KEY_MAX 192

typedef struct vn_writer_s {
    asn_app_consume_bytes_f *cb;
    void       *key;
    vn_mode_e   mode;
    int         indent_width;
    int         line_width;
    unsigned    flags;
    char       *errbuf;
    size_t      errlen;
    const vn_annotations_t *annotations;
    size_t      written;   /* bytes handed to cb so far */
    int         failed;    /* sticky */
    const asn_TYPE_descriptor_t *failed_td;
    const void                  *failed_sptr;
    /*
     * Scope path of the member being written, e.g. "AlgoParameter__algorithmID".
     *
     * An inline `algorithmID INTEGER { ... }` member has no descriptor of its own
     * -- asn1c points it straight at asn_DEF_NativeInteger, whose name is
     * "INTEGER" -- so its identifiers cannot be found by type name. The path
     * that reaches it is the key that can. asn1c threads opt_mname through its
     * XER codec for the same reason.
     *
     * It is a path rather than one parent plus one member because asn1c names a
     * nested inline definition after the whole descent: Outer__inner__x, and
     * Outer__ring__Member__y for a list element.
     */
    char member_key[VN_MEMBER_KEY_MAX];
} vn_writer_t;

/* --- vn_writer.c ---------------------------------------------------------- */
void vn_writer_init(vn_writer_t *w, const vn_options_t *opts,
                    asn_app_consume_bytes_f *cb, void *key);
int  vn_put(vn_writer_t *w, const char *s, size_t len);
int  vn_puts(vn_writer_t *w, const char *s);
int  vn_putc(vn_writer_t *w, char c);
int  vn_printf(vn_writer_t *w, const char *fmt, ...);
int  vn_break(vn_writer_t *w, int level);              /* newline + indent */
int  vn_comment(vn_writer_t *w, const char *fmt, ...); /* no-op unless ANNOTATED */
int  vn_fail(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             const char *fmt, ...);                    /* always returns -1 */
int  vn_is_annotated(const vn_writer_t *w);

/* Extend the scope path in dst with one member name. `parent` starts the
 * path when dst is still empty; empty result when a part is missing. */
void vn_member_key(char *dst, size_t dstsz, const char *parent,
                   const char *member);
/* Names for td, preferring the scoped key over the type name. */
const vn_type_names_t *vn_names_for(const vn_annotations_t *ann,
                                    const char *scoped_key,
                                    const asn_TYPE_descriptor_t *td);

/* --- vn_encoder.c -------------------------------------------------------- */
int vn_encode_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                    const void *sptr, int level);

typedef int (*vn_handler_f)(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                            const void *sptr, int level);

/* --- vn_primitive.c ------------------------------------------------------ */
int vn_put_hex(vn_writer_t *w, const unsigned char *buf, size_t len, int level);

int vn_h_boolean(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_null(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_integer(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_native_integer(vn_writer_t *, const asn_TYPE_descriptor_t *,
                        const void *, int);
int vn_h_enumerated(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *,
                    int);
int vn_h_native_enumerated(vn_writer_t *, const asn_TYPE_descriptor_t *,
                           const void *, int);
int vn_h_octet_string(vn_writer_t *, const asn_TYPE_descriptor_t *,
                      const void *, int);
int vn_h_bit_string(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *,
                    int);
int vn_h_oid(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_relative_oid(vn_writer_t *, const asn_TYPE_descriptor_t *,
                      const void *, int);
int vn_h_string(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_any(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);

/* --- vn_constructed.c ---------------------------------------------------- */
const void *vn_member_ptr(const asn_TYPE_member_t *elm, const void *sptr);

int vn_h_sequence(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *,
                  int);
int vn_h_set_of(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_choice(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_open_type(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *,
                   int);

/* --- reading -------------------------------------------------------------- */

typedef enum {
    VT_END,        /* no more input in this buffer */
    VT_INCOMPLETE, /* a token started but did not finish in this buffer */
    VT_INVALID,    /* definitely not a token */
    VT_LBRACE,
    VT_RBRACE,
    VT_COMMA,
    VT_COLON,
    VT_NUMBER,
    VT_IDENT,
    VT_CSTRING, /* body excludes the quotes; "" is still doubled */
    VT_HSTRING, /* body excludes the quotes and the H */
    VT_BSTRING
} vn_token_e;

typedef struct vn_token_s {
    vn_token_e  kind;
    const char *start; /* first byte of the token */
    size_t      len;   /* whole token, quotes and suffix included */
    const char *body;  /* content, for the value-bearing kinds */
    size_t      body_len;
} vn_token_t;

/* vn_token.c */
vn_token_e vn_token_next(const char *buf, size_t size, int eof, size_t *pos,
                         vn_token_t *tok);
int        vn_token_is(const vn_token_t *tok, const char *word);

typedef enum {
    VR_OK = 0,
    VR_MORE = 1,  /* need more input; reader->resume says from where */
    VR_FAIL = -1
} vn_rd_result_e;

typedef struct vn_reader_s {
    const char             *buf;
    size_t                  size;
    size_t                  pos;    /* next unread byte */
    size_t                  resume; /* where to re-present from on VR_MORE */
    unsigned                flags;
    const vn_annotations_t *annotations;
    char                   *errbuf;
    size_t                  errlen;
    int                     eof;
    char                    member_key[VN_MEMBER_KEY_MAX]; /* see vn_writer_t */
} vn_reader_t;

/* vn_reader.c */
int  vn_rd_fail(vn_reader_t *r, size_t at, const char *fmt, ...);
int  vn_rd_more(vn_reader_t *r, size_t from);
vn_token_e vn_rd_token(vn_reader_t *r, vn_token_t *tok);
int  vn_rd_value(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr);
void *vn_rd_alloc(vn_reader_t *r, void **sptr, size_t size);

typedef int (*vn_rd_handler_f)(vn_reader_t *r, const asn_TYPE_descriptor_t *td,
                               void **sptr);

int vn_rd_boolean(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_null(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_integer(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_native_integer(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_enumerated(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_native_enumerated(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_octet_string(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_bit_string(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_oid(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_relative_oid(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_string(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_any(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);

/* vn_rd_constructed.c */
int vn_rd_sequence(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_set(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_set_of(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);
int vn_rd_choice(vn_reader_t *, const asn_TYPE_descriptor_t *, void **);

#endif /* VN_INTERNAL_H */
