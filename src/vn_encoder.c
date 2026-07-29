/*
 * vn_encoder.c -- central type dispatch.
 *
 * asn1c 0.9.29 gives every built-in type its own operation table, so a type is
 * identified by comparing td->op against those globals. This is the one place
 * that depends on the asn1c version; see README "ABI pinning".
 *
 * The tables are declared here rather than pulled in from per-type headers
 * because a generated directory only contains the skeletons its schema uses.
 * See vn_optabs.c for how the link survives the missing ones.
 */

#include <string.h>
#include "vn_internal.h"

#define VN_OPTAB(name) extern asn_TYPE_operation_t name
VN_OPTAB(asn_OP_ANY);
VN_OPTAB(asn_OP_BIT_STRING);
VN_OPTAB(asn_OP_BMPString);
VN_OPTAB(asn_OP_BOOLEAN);
VN_OPTAB(asn_OP_CHOICE);
VN_OPTAB(asn_OP_ENUMERATED);
VN_OPTAB(asn_OP_GeneralString);
VN_OPTAB(asn_OP_GeneralizedTime);
VN_OPTAB(asn_OP_GraphicString);
VN_OPTAB(asn_OP_IA5String);
VN_OPTAB(asn_OP_INTEGER);
VN_OPTAB(asn_OP_ISO646String);
VN_OPTAB(asn_OP_NULL);
VN_OPTAB(asn_OP_NativeEnumerated);
VN_OPTAB(asn_OP_NativeInteger);
VN_OPTAB(asn_OP_NativeReal);
VN_OPTAB(asn_OP_NumericString);
VN_OPTAB(asn_OP_OBJECT_IDENTIFIER);
VN_OPTAB(asn_OP_OCTET_STRING);
VN_OPTAB(asn_OP_OPEN_TYPE);
VN_OPTAB(asn_OP_ObjectDescriptor);
VN_OPTAB(asn_OP_PrintableString);
VN_OPTAB(asn_OP_REAL);
VN_OPTAB(asn_OP_RELATIVE_OID);
VN_OPTAB(asn_OP_SEQUENCE);
VN_OPTAB(asn_OP_SEQUENCE_OF);
VN_OPTAB(asn_OP_SET);
VN_OPTAB(asn_OP_SET_OF);
VN_OPTAB(asn_OP_T61String);
VN_OPTAB(asn_OP_TeletexString);
VN_OPTAB(asn_OP_UTCTime);
VN_OPTAB(asn_OP_UTF8String);
VN_OPTAB(asn_OP_UniversalString);
VN_OPTAB(asn_OP_VideotexString);
VN_OPTAB(asn_OP_VisibleString);

static const struct vn_dispatch_s {
    const asn_TYPE_operation_t *op;
    vn_handler_f                handler;
} vn_dispatch[] = {
    { &asn_OP_BOOLEAN, vn_h_boolean },
    { &asn_OP_NULL,    vn_h_null    }
};

int
vn_encode_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    size_t i;

    if(w->failed) return -1;
    if(!td) return vn_fail(w, td, sptr, "internal: null type descriptor");
    if(!td->op)
        return vn_fail(w, td, sptr, "type %s has no operation table",
                       td->name ? td->name : "(unnamed)");

    for(i = 0; i < sizeof vn_dispatch / sizeof vn_dispatch[0]; i++)
        if(vn_dispatch[i].op && vn_dispatch[i].op == td->op)
            return vn_dispatch[i].handler(w, td, sptr, level);

    return vn_fail(w, td, sptr,
                   "no value notation for type %s: unsupported or unknown "
                   "operation table",
                   (td->name && td->name[0]) ? td->name : "(unnamed)");
}

asn_enc_rval_t
vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
          const vn_options_t *opts, asn_app_consume_bytes_f *cb, void *key) {
    asn_enc_rval_t er;
    vn_writer_t w;

    vn_writer_init(&w, opts, cb, key);
    if(vn_encode_value(&w, td, sptr, 0) < 0) {
        er.encoded = -1;
        er.failed_type = w.failed_td ? w.failed_td : td;
        er.structure_ptr = w.failed_sptr ? w.failed_sptr : sptr;
    } else {
        er.encoded = (ssize_t)w.written;
        er.failed_type = 0;
        er.structure_ptr = 0;
    }
    return er;
}

static int
vn_write_stream(const void *buffer, size_t size, void *app_key) {
    FILE *f = (FILE *)app_key;
    return fwrite(buffer, 1, size, f) == size ? 0 : -1;
}

int
vn_fprint(FILE *stream, const asn_TYPE_descriptor_t *td, const void *sptr,
          const vn_options_t *opts) {
    asn_enc_rval_t er = vn_encode(td, sptr, opts, vn_write_stream, stream);
    return er.encoded < 0 ? -1 : 0;
}
