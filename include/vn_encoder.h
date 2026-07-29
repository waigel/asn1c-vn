/*
 * vn_encoder.h -- ASN.1 value notation codec for vlm/asn1c.
 *
 * Converts between a decoded asn1c structure and the textual value notation
 * defined by ITU-T X.680 (02/2021) = ISO/IEC 8824-1:2021: vn_encode() writes it,
 * vn_decode() reads it back.
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

/* --- reading ------------------------------------------------------------- */

#define VN_RF_LENIENT 0x01u /* accept questionable input instead of failing */
/*
 * The buffer holds all the input there will be.
 *
 * Without this, a bare token at the end of the buffer is ambiguous: `TRUE` could
 * be complete or could be the start of a longer identifier arriving in the next
 * chunk, so the reader must ask for more and would never finish. Callers that
 * hold the whole text -- the normal case -- set this; one feeding a stream sets
 * it only on the final presentation.
 */
#define VN_RF_EOF     0x02u

typedef struct vn_read_options_s {
    unsigned                flags;
    const vn_annotations_t *annotations; /* needed for identifier input */
    char                   *errbuf;      /* optional; reason plus position */
    size_t                  errlen;
} vn_read_options_t;

/*
 * Parse one ASN.1 value in value notation into *struct_ptr, an instance of *td.
 *
 * Shaped like asn1c's xer_type_decoder_f, minus its opt_mname: value notation has
 * no element wrapper around a value. Exactly one value is consumed; anything
 * after it is the caller's business, and rval.consumed says where it ended.
 *
 * Restartability follows asn1c's own contract, which is only partial:
 *   - Constructed types resume byte-exactly, keeping progress in the target
 *     structure's _asn_ctx.
 *   - A primitive has nowhere to keep state, so on RC_WMORE it reports the
 *     position at which its value began; the caller re-presents from there.
 *   - A single token must be complete within one presentation. A 10 KB hstring
 *     needs a 10 KB buffer. asn1c's XER decoder behaves the same way.
 *
 * *struct_ptr belongs to the caller after RC_WMORE and after RC_FAIL alike;
 * release it with ASN_STRUCT_FREE.
 */
asn_dec_rval_t vn_decode(const asn_codec_ctx_t *opt_codec_ctx,
                         const asn_TYPE_descriptor_t *td, void **struct_ptr,
                         const vn_read_options_t *opts, const void *buf,
                         size_t size);

#ifdef __cplusplus
}
#endif

#endif /* VN_ENCODER_H */
