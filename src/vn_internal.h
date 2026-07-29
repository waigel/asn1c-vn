/*
 * vn_internal.h -- shared by the encoder sources only, not installed.
 */
#ifndef VN_INTERNAL_H
#define VN_INTERNAL_H

#include <sys/types.h>
#include "vn_encoder.h"

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

#endif /* VN_INTERNAL_H */
