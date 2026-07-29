/*
 * vn_reader.c -- value notation to structure: dispatch and the leaf types.
 *
 * Ownership discipline, which is where decoders normally leak: everything
 * allocated is reachable from *struct_ptr before any return, so the caller's
 * ASN_STRUCT_FREE collects it. Never hold an allocation only in a local across a
 * return point.
 */

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ANY.h>
#include <BIT_STRING.h>
#include <BOOLEAN.h>
#include <INTEGER.h>
#include <NULL.h>
#include <NativeEnumerated.h>
#include <NativeInteger.h>
#include <OBJECT_IDENTIFIER.h>
#include <OCTET_STRING.h>
#include "RELATIVE-OID.h"
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
VN_OPTAB(asn_OP_NumericString);
VN_OPTAB(asn_OP_OBJECT_IDENTIFIER);
VN_OPTAB(asn_OP_OCTET_STRING);
VN_OPTAB(asn_OP_OPEN_TYPE);
VN_OPTAB(asn_OP_ObjectDescriptor);
VN_OPTAB(asn_OP_PrintableString);
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

/* --- diagnostics ---------------------------------------------------------- */

int
vn_rd_fail(vn_reader_t *r, size_t at, const char *fmt, ...) {
    if(r->errbuf && r->errlen) {
        /* A line and column is far more use than a byte offset in text. */
        size_t i, line = 1, col = 1;
        int    n;
        for(i = 0; i < at && i < r->size; i++) {
            if(r->buf[i] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        n = snprintf(r->errbuf, r->errlen, "line %lu column %lu: ",
                     (unsigned long)line, (unsigned long)col);
        if(n > 0 && (size_t)n < r->errlen) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(r->errbuf + n, r->errlen - (size_t)n, fmt, ap);
            va_end(ap);
        }
    }
    return VR_FAIL;
}

vn_token_e
vn_rd_token(vn_reader_t *r, vn_token_t *tok) {
    return vn_token_next(r->buf, r->size, r->eof, &r->pos, tok);
}

int
vn_rd_more(vn_reader_t *r, size_t from) {
    r->resume = from;
    return VR_MORE;
}

/*
 * Allocate a member if it is not there yet, storing it through sptr first so it
 * is reachable before anything can fail.
 */
void *
vn_rd_alloc(vn_reader_t *r, void **sptr, size_t size) {
    if(*sptr) return *sptr;
    *sptr = calloc(1, size);
    if(!*sptr) vn_rd_fail(r, r->pos, "out of memory");
    return *sptr;
}

/* --- helpers -------------------------------------------------------------- */

/*
 * Convert a token body to a number.
 *
 * A token points into the caller's buffer and is NOT NUL-terminated, so handing
 * tok->body straight to strtol lets it scan past the end of the input. A fuzzer
 * found exactly that as a heap overflow. Copy into a bounded scratch buffer
 * first, and reject anything too long to be a number rather than truncating it.
 */
static int
vn_tok_number(vn_reader_t *r, const vn_token_t *tok, int is_unsigned,
              long *out_signed, unsigned long *out_unsigned) {
    char  scratch[32];
    char *end;

    if(tok->body_len == 0 || tok->body_len >= sizeof scratch)
        return vn_rd_fail(r, (size_t)(tok->start - r->buf),
                          "'%.*s' is not a usable number",
                          (int)(tok->body_len > 24 ? 24 : tok->body_len),
                          tok->body);
    memcpy(scratch, tok->body, tok->body_len);
    scratch[tok->body_len] = '\0';
    errno = 0;
    if(is_unsigned) *out_unsigned = strtoul(scratch, &end, 10);
    else *out_signed = strtol(scratch, &end, 10);
    if(errno != 0 || *end != '\0')
        return vn_rd_fail(r, (size_t)(tok->start - r->buf),
                          "'%s' is out of range", scratch);
    return VR_OK;
}

static int
vn_hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/*
 * Decode an hstring or bstring body into freshly allocated bytes.
 *
 * Whitespace inside the quotes is skipped: X.680 does not count it as part of the
 * value, and the encoder inserts it when wrapping long hex in pretty mode.
 */
static int
vn_rd_bytes(vn_reader_t *r, const vn_token_t *tok, uint8_t **out, size_t *out_len,
            int *bits_unused, size_t *out_digits) {
    size_t   i, ndigits = 0, nbits, nbytes;
    uint8_t *buf;
    int      hex = (tok->kind == VT_HSTRING);

    for(i = 0; i < tok->body_len; i++) {
        char c = tok->body[i];
        if(c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if(hex ? (vn_hexval(c) < 0) : (c != '0' && c != '1'))
            return vn_rd_fail(r, (size_t)(tok->body - r->buf) + i,
                              "'%c' is not a %s digit", c,
                              hex ? "hex" : "binary");
        ndigits++;
    }

    nbits = hex ? ndigits * 4 : ndigits;
    nbytes = (nbits + 7) / 8;
    buf = (uint8_t *)calloc(1, nbytes + 1);
    if(!buf) return vn_rd_fail(r, r->pos, "out of memory");

    {
        size_t d = 0;
        for(i = 0; i < tok->body_len; i++) {
            char c = tok->body[i];
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            if(hex) {
                int v = vn_hexval(c);
                if(d % 2 == 0) buf[d / 2] = (uint8_t)(v << 4);
                else buf[d / 2] |= (uint8_t)v;
            } else if(c == '1') {
                buf[d / 8] |= (uint8_t)(0x80u >> (d % 8));
            }
            d++;
        }
    }

    *out = buf;
    *out_len = nbytes;
    if(bits_unused) *bits_unused = (int)((nbytes * 8) - nbits);
    if(out_digits) *out_digits = ndigits;
    return VR_OK;
}

/* Replace an OCTET_STRING_t body, taking ownership of buf. */
static void
vn_rd_set_octets(OCTET_STRING_t *os, uint8_t *buf, size_t len) {
    if(os->buf) free(os->buf);
    os->buf = buf;
    os->size = len;
}

/* --- primitives ----------------------------------------------------------- */

int
vn_rd_boolean(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t     from = r->pos;
    vn_token_t tok;
    BOOLEAN_t *b;

    (void)td;
    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_IDENT: break;
    default: return vn_rd_fail(r, from, "expected TRUE or FALSE");
    }
    b = (BOOLEAN_t *)vn_rd_alloc(r, sptr, sizeof(*b));
    if(!b) return VR_FAIL;
    if(vn_token_is(&tok, "TRUE")) *b = 1;
    else if(vn_token_is(&tok, "FALSE")) *b = 0;
    else return vn_rd_fail(r, from, "expected TRUE or FALSE");
    return VR_OK;
}

int
vn_rd_null(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t     from = r->pos;
    vn_token_t tok;

    (void)td;
    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_IDENT: break;
    default: return vn_rd_fail(r, from, "expected NULL");
    }
    if(!vn_token_is(&tok, "NULL")) return vn_rd_fail(r, from, "expected NULL");
    if(!vn_rd_alloc(r, sptr, sizeof(NULL_t))) return VR_FAIL;
    return VR_OK;
}

/* Decimal to an arbitrary-width big-endian two's-complement integer. */
static int
vn_rd_big_integer(vn_reader_t *r, const vn_token_t *tok, INTEGER_t *st) {
    const char *p = tok->body;
    size_t      len = tok->body_len, i;
    int         negative = 0;
    uint8_t     mag[160];
    size_t      used = 1;
    uint8_t    *out;
    size_t      out_len;

    memset(mag, 0, sizeof mag);
    if(len && *p == '-') {
        negative = 1;
        p++;
        len--;
    }
    for(i = 0; i < len; i++) {
        unsigned carry = (unsigned)(p[i] - '0');
        size_t   j;
        for(j = 0; j < used; j++) {
            unsigned v = (unsigned)mag[used - 1 - j] * 10u + carry;
            mag[used - 1 - j] = (uint8_t)(v & 0xff);
            carry = v >> 8;
        }
        while(carry) {
            if(used >= sizeof mag)
                return vn_rd_fail(r, (size_t)(tok->start - r->buf),
                                  "integer exceeds %u octets",
                                  (unsigned)sizeof mag);
            memmove(mag + 1, mag, used);
            mag[0] = (uint8_t)(carry & 0xff);
            carry >>= 8;
            used++;
        }
    }

    /* A leading zero octet keeps a positive value from reading as negative. */
    out_len = used + ((mag[0] & 0x80) ? 1 : 0);
    out = (uint8_t *)calloc(1, out_len + 1);
    if(!out) return vn_rd_fail(r, r->pos, "out of memory");
    memcpy(out + (out_len - used), mag, used);

    if(negative) {
        size_t j = out_len;
        for(i = 0; i < out_len; i++) out[i] = (uint8_t)~out[i];
        while(j-- > 0)
            if(++out[j] != 0) break;
    }

    if(st->buf) free(st->buf);
    st->buf = out;
    st->size = out_len;
    return VR_OK;
}

int
vn_rd_integer(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t     from = r->pos;
    vn_token_t tok;
    INTEGER_t *st;
    long       value;
    char       scratch[32];

    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_NUMBER: break;
    case VT_IDENT: {
        /* A named number, if the annotation table supplies one. */
        const vn_type_names_t *names =
            vn_names_for(r->annotations, r->member_key, td);
        size_t i;
        if(names && !names->is_bit_string) {
            for(i = 0; i < names->count; i++) {
                if(tok.body_len != strlen(names->values[i].name)) continue;
                if(memcmp(tok.body, names->values[i].name, tok.body_len)) continue;
                st = (INTEGER_t *)vn_rd_alloc(r, sptr, sizeof(*st));
                if(!st) return VR_FAIL;
                if(asn_long2INTEGER(st, names->values[i].value))
                    return vn_rd_fail(r, from, "cannot store %s",
                                      names->values[i].name);
                return VR_OK;
            }
        }
        return vn_rd_fail(r, from,
                          "'%.*s' is not a known identifier for %s; supply an "
                          "annotation table or use the number",
                          (int)tok.body_len, tok.body,
                          td->name ? td->name : "INTEGER");
    }
    default: return vn_rd_fail(r, from, "expected an integer");
    }

    st = (INTEGER_t *)vn_rd_alloc(r, sptr, sizeof(*st));
    if(!st) return VR_FAIL;

    /* A value that fits a long takes the simple path; anything wider goes
     * through the decimal-to-binary conversion below. */
    if(tok.body_len < 20) {
        char *end;
        memcpy(scratch, tok.body, tok.body_len);
        scratch[tok.body_len] = '\0';
        errno = 0;
        value = strtol(scratch, &end, 10);
        if(errno == 0 && *end == '\0') {
            if(asn_long2INTEGER(st, value))
                return vn_rd_fail(r, from, "cannot store the integer");
            return VR_OK;
        }
    }
    return vn_rd_big_integer(r, &tok, st);
}

int
vn_rd_native_integer(vn_reader_t *r, const asn_TYPE_descriptor_t *td,
                     void **sptr) {
    INTEGER_t  tmp;
    void      *tmp_ptr = &tmp;
    long      *native;
    int        rc;

    memset(&tmp, 0, sizeof tmp);
    rc = vn_rd_integer(r, td, &tmp_ptr);
    if(rc != VR_OK) {
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_INTEGER, &tmp);
        return rc;
    }
    native = (long *)vn_rd_alloc(r, sptr, sizeof(*native));
    if(!native) {
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_INTEGER, &tmp);
        return VR_FAIL;
    }
    {
        const asn_INTEGER_specifics_t *specs =
            (const asn_INTEGER_specifics_t *)td->specifics;
        int bad = (specs && specs->field_unsigned)
                      ? asn_INTEGER2ulong(&tmp, (unsigned long *)native)
                      : asn_INTEGER2long(&tmp, native);
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_INTEGER, &tmp);
        if(bad) return vn_rd_fail(r, r->pos, "integer does not fit a long");
    }
    return VR_OK;
}

/* Shared by ENUMERATED and NativeEnumerated: identifier to value. */
static int
vn_rd_enum_value(vn_reader_t *r, const asn_TYPE_descriptor_t *td, long *out) {
    const asn_INTEGER_specifics_t *specs =
        (const asn_INTEGER_specifics_t *)td->specifics;
    size_t     from = r->pos;
    vn_token_t tok;
    int        i;

    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_IDENT: break;
    case VT_NUMBER:
        /* X.680 admits only the identifier for ENUMERATED. */
        if(!(r->flags & VN_RF_LENIENT))
            return vn_rd_fail(r, from,
                              "ENUMERATED %s needs an identifier, not a number",
                              td->name ? td->name : "");
        return vn_tok_number(r, &tok, 0, out, 0);
    default:
        return vn_rd_fail(r, from, "expected an enumeration identifier");
    }

    if(specs) {
        for(i = 0; i < specs->map_count; i++) {
            const asn_INTEGER_enum_map_t *e = &specs->value2enum[i];
            if(!e->enum_name) continue;
            if(strlen(e->enum_name) != tok.body_len) continue;
            if(memcmp(e->enum_name, tok.body, tok.body_len)) continue;
            *out = e->nat_value;
            return VR_OK;
        }
    }
    return vn_rd_fail(r, from, "'%.*s' is not a value of %s",
                      (int)tok.body_len, tok.body, td->name ? td->name : "");
}

int
vn_rd_enumerated(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    long       value = 0;
    int        rc = vn_rd_enum_value(r, td, &value);
    INTEGER_t *st;

    if(rc != VR_OK) return rc;
    st = (INTEGER_t *)vn_rd_alloc(r, sptr, sizeof(*st));
    if(!st) return VR_FAIL;
    if(asn_long2INTEGER(st, value))
        return vn_rd_fail(r, r->pos, "cannot store the enumeration");
    return VR_OK;
}

int
vn_rd_native_enumerated(vn_reader_t *r, const asn_TYPE_descriptor_t *td,
                        void **sptr) {
    long  value = 0;
    int   rc = vn_rd_enum_value(r, td, &value);
    long *native;

    if(rc != VR_OK) return rc;
    native = (long *)vn_rd_alloc(r, sptr, sizeof(*native));
    if(!native) return VR_FAIL;
    *native = value;
    return VR_OK;
}

int
vn_rd_octet_string(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t          from = r->pos;
    vn_token_t      tok;
    OCTET_STRING_t *os;
    uint8_t        *bytes = 0;
    size_t          len = 0;
    int             rc;

    (void)td;
    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_HSTRING:
    case VT_BSTRING: break;
    default: return vn_rd_fail(r, from, "expected '..'H or '..'B");
    }
    {
        size_t digits = 0;
        rc = vn_rd_bytes(r, &tok, &bytes, &len, 0, &digits);
        if(rc != VR_OK) return rc;
        if(tok.kind == VT_HSTRING && (digits % 2)) {
            free(bytes);
            return vn_rd_fail(
                r, from, "an OCTET STRING needs an even number of hex digits");
        }
    }
    os = (OCTET_STRING_t *)vn_rd_alloc(r, sptr, sizeof(*os));
    if(!os) {
        free(bytes);
        return VR_FAIL;
    }
    vn_rd_set_octets(os, bytes, len);
    return VR_OK;
}

int
vn_rd_bit_string(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t        from = r->pos;
    vn_token_t    tok;
    BIT_STRING_t *bs;
    uint8_t      *bytes = 0;
    size_t        len = 0;
    int           unused = 0, rc;

    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_HSTRING:
    case VT_BSTRING: break;
    case VT_LBRACE: {
        /* A named bit list: { keyCert, crlSign } */
        const vn_type_names_t *names =
            vn_names_for(r->annotations, r->member_key, td);
        size_t  highest = 0;
        uint8_t tmp[64];

        if(!names || !names->is_bit_string)
            return vn_rd_fail(r, from,
                              "a named bit list needs an annotation table for %s",
                              td->name ? td->name : "BIT STRING");
        memset(tmp, 0, sizeof tmp);
        for(;;) {
            size_t     item = r->pos;
            vn_token_e k = vn_rd_token(r, &tok);
            size_t     i;
            if(k == VT_INCOMPLETE || k == VT_END) return vn_rd_more(r, from);
            if(k == VT_RBRACE) break;
            if(k == VT_COMMA) continue;
            if(k != VT_IDENT)
                return vn_rd_fail(r, item, "expected a bit name or }");
            for(i = 0; i < names->count; i++) {
                size_t pos_bit;
                if(strlen(names->values[i].name) != tok.body_len) continue;
                if(memcmp(names->values[i].name, tok.body, tok.body_len)) continue;
                pos_bit = (size_t)names->values[i].value;
                if(pos_bit / 8 >= sizeof tmp)
                    return vn_rd_fail(r, item, "bit position %lu is too large",
                                      (unsigned long)pos_bit);
                tmp[pos_bit / 8] |= (uint8_t)(0x80u >> (pos_bit % 8));
                if(pos_bit + 1 > highest) highest = pos_bit + 1;
                break;
            }
            if(i == names->count)
                return vn_rd_fail(r, item, "'%.*s' is not a bit of %s",
                                  (int)tok.body_len, tok.body,
                                  td->name ? td->name : "");
        }
        len = (highest + 7) / 8;
        unused = (int)(len * 8 - highest);
        bytes = (uint8_t *)calloc(1, len + 1);
        if(!bytes) return vn_rd_fail(r, from, "out of memory");
        memcpy(bytes, tmp, len);
        bs = (BIT_STRING_t *)vn_rd_alloc(r, sptr, sizeof(*bs));
        if(!bs) {
            free(bytes);
            return VR_FAIL;
        }
        vn_rd_set_octets((OCTET_STRING_t *)bs, bytes, len);
        bs->bits_unused = unused;
        return VR_OK;
    }
    default: return vn_rd_fail(r, from, "expected '..'B, '..'H or a bit list");
    }

    rc = vn_rd_bytes(r, &tok, &bytes, &len, &unused, 0);
    if(rc != VR_OK) return rc;
    bs = (BIT_STRING_t *)vn_rd_alloc(r, sptr, sizeof(*bs));
    if(!bs) {
        free(bytes);
        return VR_FAIL;
    }
    vn_rd_set_octets((OCTET_STRING_t *)bs, bytes, len);
    bs->bits_unused = unused;
    return VR_OK;
}

static int
vn_rd_arcs(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr,
           int relative) {
    size_t        from = r->pos;
    vn_token_t    tok;
    asn_oid_arc_t arcs[32];
    size_t        count = 0;

    if(vn_rd_token(r, &tok) != VT_LBRACE) {
        if(tok.kind == VT_INCOMPLETE || tok.kind == VT_END)
            return vn_rd_more(r, from);
        return vn_rd_fail(r, from, "expected { to start an object identifier");
    }
    for(;;) {
        size_t     item = r->pos;
        vn_token_e k = vn_rd_token(r, &tok);
        if(k == VT_INCOMPLETE || k == VT_END) return vn_rd_more(r, from);
        if(k == VT_RBRACE) break;
        if(k != VT_NUMBER) return vn_rd_fail(r, item, "expected an arc or }");
        if(count >= sizeof arcs / sizeof arcs[0])
            return vn_rd_fail(r, item, "more than %u arcs",
                              (unsigned)(sizeof arcs / sizeof arcs[0]));
        {
            unsigned long arc = 0;
            int           rc = vn_tok_number(r, &tok, 1, 0, &arc);
            if(rc != VR_OK) return rc;
            arcs[count++] = (asn_oid_arc_t)arc;
        }
    }
    if(!vn_rd_alloc(r, sptr, relative ? sizeof(RELATIVE_OID_t)
                                      : sizeof(OBJECT_IDENTIFIER_t)))
        return VR_FAIL;
    if(relative) {
        if(RELATIVE_OID_set_arcs((RELATIVE_OID_t *)*sptr, arcs, count))
            return vn_rd_fail(r, from, "cannot store the relative oid");
    } else {
        if(count < 2)
            return vn_rd_fail(r, from,
                              "an OBJECT IDENTIFIER needs at least two arcs");
        if(OBJECT_IDENTIFIER_set_arcs((OBJECT_IDENTIFIER_t *)*sptr, arcs, count))
            return vn_rd_fail(r, from, "cannot store the object identifier");
    }
    (void)td;
    return VR_OK;
}

int
vn_rd_oid(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    return vn_rd_arcs(r, td, sptr, 0);
}

int
vn_rd_relative_oid(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    return vn_rd_arcs(r, td, sptr, 1);
}

/* One UTF-8 code point; returns the bytes consumed, or 0 on a bad sequence. */
static size_t
vn_utf8_next(const char *p, size_t len, unsigned long *cp) {
    unsigned char c = (unsigned char)p[0];
    size_t        need;

    if(c < 0x80) { *cp = c; return 1; }
    if((c & 0xe0) == 0xc0) { *cp = c & 0x1fu; need = 1; }
    else if((c & 0xf0) == 0xe0) { *cp = c & 0x0fu; need = 2; }
    else if((c & 0xf8) == 0xf0) { *cp = c & 0x07u; need = 3; }
    else return 0;
    if(len < need + 1) return 0;
    {
        size_t i;
        for(i = 1; i <= need; i++) {
            unsigned char cc = (unsigned char)p[i];
            if((cc & 0xc0) != 0x80) return 0;
            *cp = (*cp << 6) | (cc & 0x3fu);
        }
    }
    return need + 1;
}

int
vn_rd_string(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    const asn_OCTET_STRING_specifics_t *specs =
        (const asn_OCTET_STRING_specifics_t *)td->specifics;
    enum asn_OS_Subvariant sub = specs ? specs->subvariant : ASN_OSUBV_STR;
    size_t                 from = r->pos;
    vn_token_t             tok;
    OCTET_STRING_t        *os;
    uint8_t               *out;
    size_t                 i, n = 0;

    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_CSTRING: break;
    default: return vn_rd_fail(r, from, "expected a \"string\"");
    }

    /* Worst case: every code point becomes four octets. */
    out = (uint8_t *)calloc(1, tok.body_len * 4 + 4);
    if(!out) return vn_rd_fail(r, from, "out of memory");

    for(i = 0; i < tok.body_len;) {
        unsigned long cp;
        size_t        used;

        if(tok.body[i] == '"' && i + 1 < tok.body_len && tok.body[i + 1] == '"') {
            cp = '"';
            used = 2;
        } else if(sub == ASN_OSUBV_U16 || sub == ASN_OSUBV_U32) {
            used = vn_utf8_next(tok.body + i, tok.body_len - i, &cp);
            if(!used) {
                free(out);
                return vn_rd_fail(r, (size_t)(tok.body - r->buf) + i,
                                  "not valid UTF-8");
            }
        } else {
            out[n++] = (uint8_t)tok.body[i];
            i++;
            continue;
        }
        i += used;

        if(sub == ASN_OSUBV_U16) {
            if(cp > 0xffff) {
                free(out);
                return vn_rd_fail(r, from,
                                  "U+%lX does not fit a BMPString", cp);
            }
            out[n++] = (uint8_t)(cp >> 8);
            out[n++] = (uint8_t)(cp & 0xff);
        } else if(sub == ASN_OSUBV_U32) {
            out[n++] = (uint8_t)(cp >> 24);
            out[n++] = (uint8_t)((cp >> 16) & 0xff);
            out[n++] = (uint8_t)((cp >> 8) & 0xff);
            out[n++] = (uint8_t)(cp & 0xff);
        } else {
            out[n++] = (uint8_t)cp;
        }
    }

    os = (OCTET_STRING_t *)vn_rd_alloc(r, sptr, sizeof(*os));
    if(!os) {
        free(out);
        return VR_FAIL;
    }
    vn_rd_set_octets(os, out, n);
    return VR_OK;
}

int
vn_rd_any(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    /* Hex, mirroring the encoder's one documented deviation from X.680. */
    return vn_rd_octet_string(r, td, sptr);
}

/* --- dispatch ------------------------------------------------------------- */

static const struct vn_rd_dispatch_s {
    const asn_TYPE_operation_t *op;
    vn_rd_handler_f             handler;
} vn_rd_dispatch[] = {
    {&asn_OP_BOOLEAN, vn_rd_boolean},
    {&asn_OP_NULL, vn_rd_null},
    {&asn_OP_INTEGER, vn_rd_integer},
    {&asn_OP_NativeInteger, vn_rd_native_integer},
    {&asn_OP_ENUMERATED, vn_rd_enumerated},
    {&asn_OP_NativeEnumerated, vn_rd_native_enumerated},
    {&asn_OP_OCTET_STRING, vn_rd_octet_string},
    {&asn_OP_BIT_STRING, vn_rd_bit_string},
    {&asn_OP_OBJECT_IDENTIFIER, vn_rd_oid},
    {&asn_OP_RELATIVE_OID, vn_rd_relative_oid},
    {&asn_OP_SEQUENCE, vn_rd_sequence},
    {&asn_OP_SET, vn_rd_sequence},
    {&asn_OP_SEQUENCE_OF, vn_rd_set_of},
    {&asn_OP_SET_OF, vn_rd_set_of},
    {&asn_OP_CHOICE, vn_rd_choice},
    {&asn_OP_OPEN_TYPE, vn_rd_choice},
    {&asn_OP_UTF8String, vn_rd_string},
    {&asn_OP_IA5String, vn_rd_string},
    {&asn_OP_PrintableString, vn_rd_string},
    {&asn_OP_NumericString, vn_rd_string},
    {&asn_OP_VisibleString, vn_rd_string},
    {&asn_OP_ISO646String, vn_rd_string},
    {&asn_OP_GeneralString, vn_rd_string},
    {&asn_OP_GraphicString, vn_rd_string},
    {&asn_OP_TeletexString, vn_rd_string},
    {&asn_OP_T61String, vn_rd_string},
    {&asn_OP_VideotexString, vn_rd_string},
    {&asn_OP_BMPString, vn_rd_string},
    {&asn_OP_UniversalString, vn_rd_string},
    {&asn_OP_ObjectDescriptor, vn_rd_string},
    {&asn_OP_GeneralizedTime, vn_rd_string},
    {&asn_OP_UTCTime, vn_rd_string},
    {&asn_OP_ANY, vn_rd_any}
};

int
vn_rd_value(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    size_t i;

    if(!td) return vn_rd_fail(r, r->pos, "internal: null type descriptor");
    if(!td->op)
        return vn_rd_fail(r, r->pos, "type %s has no operation table",
                          td->name ? td->name : "(unnamed)");
    for(i = 0; i < sizeof vn_rd_dispatch / sizeof vn_rd_dispatch[0]; i++)
        if(vn_rd_dispatch[i].op && vn_rd_dispatch[i].op == td->op)
            return vn_rd_dispatch[i].handler(r, td, sptr);
    return vn_rd_fail(r, r->pos,
                      "cannot read type %s from value notation: unsupported or "
                      "unknown operation table",
                      (td->name && td->name[0]) ? td->name : "(unnamed)");
}

asn_dec_rval_t
vn_decode(const asn_codec_ctx_t *opt_codec_ctx, const asn_TYPE_descriptor_t *td,
          void **struct_ptr, const vn_read_options_t *opts, const void *buf,
          size_t size) {
    asn_dec_rval_t rval;
    vn_reader_t    r;
    int            rc;

    (void)opt_codec_ctx;
    memset(&r, 0, sizeof r);
    r.buf = (const char *)buf;
    r.size = size;
    if(opts) {
        r.flags = opts->flags;
        r.annotations = opts->annotations;
        r.errbuf = opts->errbuf;
        r.errlen = opts->errlen;
        r.eof = (opts->flags & VN_RF_EOF) != 0;
        if(r.errbuf && r.errlen) r.errbuf[0] = '\0';
    }

    rc = vn_rd_value(&r, td, struct_ptr);
    switch(rc) {
    case VR_OK:
        rval.code = RC_OK;
        rval.consumed = r.pos;
        break;
    case VR_MORE:
        rval.code = RC_WMORE;
        rval.consumed = r.resume;
        break;
    default:
        rval.code = RC_FAIL;
        rval.consumed = r.pos;
        break;
    }
    return rval;
}
