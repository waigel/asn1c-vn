/*
 * vn_encoder.h -- ASN.1 value notation encoder for vlm/asn1c.
 *
 * Serialises a decoded asn1c structure as the textual value notation defined
 * by ITU-T X.680 (02/2021) = ISO/IEC 8824-1:2021. Output only; reading value
 * notation back into a structure is out of scope.
 */
#ifndef VN_ENCODER_H
#define VN_ENCODER_H

#include <stdio.h>
#include <stddef.h>
#include <asn_application.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output flavour. All three emit valid X.680 value notation. */
typedef enum {
    VN_MODE_PRETTY = 0, /* for reading: 4-space indent, hex wrapped */
    VN_MODE_CANONICAL,  /* for diffing: fixed 2-space indent, no wrapping */
    VN_MODE_ANNOTATED   /* pretty plus X.680 comments */
} vn_mode_e;

#define VN_F_LENIENT         0x01u /* emit questionable values instead of failing */
#define VN_F_ENUM_WITH_VALUE 0x02u /* `green -- (1) --` instead of `green` */
#define VN_F_STRICT_ANY      0x04u /* fail on bare ANY instead of emitting hex */

/*
 * Optional annotation table: the identifiers asn1c does not keep at runtime.
 *
 * asn1c retains ENUMERATED identifiers but not INTEGER named numbers or
 * BIT STRING named bit lists, and for INTEGER it must not -- X.693 8.3.4
 * prohibits the identifier form in XER. The names do survive as C enums in the
 * generated headers, and tools/vn-annotate.c turns those into a table.
 *
 * Supplying one lets `keyReference pukAppl1` be written instead of
 * `keyReference 1`. Leaving it NULL keeps the numeric forms, which are equally
 * valid X.680.
 */
typedef struct vn_named_value_s {
    const char *name;
    long        value; /* for a bit list, the bit position */
} vn_named_value_t;

typedef struct vn_type_names_s {
    const char             *type_name; /* matches asn_TYPE_descriptor_t.name */
    const vn_named_value_t *values;
    size_t                  count;
    int                     is_bit_string; /* named bits, not named numbers */
} vn_type_names_t;

typedef struct vn_annotations_s {
    const vn_type_names_t *types;
    size_t                 count;
} vn_annotations_t;

/* Look up a type's names, or NULL. Exposed because the reader needs it too. */
const vn_type_names_t *vn_annotations_find(const vn_annotations_t *ann,
                                           const char *type_name);

typedef struct vn_options_s {
    vn_mode_e mode;
    int       indent_width; /* 0 = mode default (4); ignored when CANONICAL */
    int       line_width;   /* 0 = mode default (76); ignored when CANONICAL */
    unsigned  flags;
    char     *errbuf;       /* optional; receives a human-readable reason */
    size_t    errlen;
    const vn_annotations_t *annotations; /* optional; see above */
} vn_options_t;

/*
 * Serialise *sptr, an instance of *td, as ASN.1 value notation.
 * Same shape as xer_encode(): bytes leave through the consume callback so the
 * destination (file, buffer, socket) is the caller's business.
 * opts may be NULL, meaning pretty defaults.
 */
asn_enc_rval_t vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                         const vn_options_t *opts,
                         asn_app_consume_bytes_f *cb, void *key);

/* Convenience: write straight to a stream. 0 on success, -1 on failure. */
int vn_fprint(FILE *stream, const asn_TYPE_descriptor_t *td, const void *sptr,
              const vn_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* VN_ENCODER_H */
