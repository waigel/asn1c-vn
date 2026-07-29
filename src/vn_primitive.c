/*
 * vn_primitive.c -- value notation for leaf types.
 *
 * Handlers here know the syntax of a single ASN.1 type and nothing about where
 * the bytes go; that is the writer's business.
 */

#include <string.h>
#include <BOOLEAN.h>
#include <INTEGER.h>
#include <NULL.h>
#include <NativeEnumerated.h>
#include <NativeInteger.h>
#include <BIT_STRING.h>
#include <OBJECT_IDENTIFIER.h>
#include <OCTET_STRING.h>
#include "RELATIVE-OID.h" /* asn1c spells this filename with a hyphen */
#include "vn_internal.h"

/*
 * Widest INTEGER magnitude we convert, and the decimal digits it can need.
 * 160 octets is 1280 bits, whose decimal form is at most 386 digits.
 */
#define VN_INT_MAX_OCTETS 160
#define VN_INT_MAX_DIGITS 400

int
vn_h_boolean(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             int level) {
    const BOOLEAN_t *b = (const BOOLEAN_t *)sptr;
    (void)td;
    (void)level;
    /* BER allows any nonzero octet to mean true, so test for nonzero rather
     * than comparing against 1. */
    return vn_puts(w, *b ? "TRUE" : "FALSE");
}

int
vn_h_null(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
          int level) {
    (void)td;
    (void)sptr;
    (void)level;
    return vn_puts(w, "NULL");
}

/*
 * Emit bytes as an X.680 hstring: 'AABB'H, uppercase.
 *
 * When line_width is set, wrap on an even digit boundary so a byte is never
 * split across lines. Shared with BIT STRING and bare ANY.
 */
int
vn_put_hex(vn_writer_t *w, const unsigned char *buf, size_t len, int level) {
    static const char hexdigits[] = "0123456789ABCDEF";
    size_t i, on_line = 0;
    int budget = w->line_width - (level + 1) * w->indent_width;

    /* Keep a usable budget even at deep nesting levels. */
    if(budget < 8) budget = 8;

    if(vn_putc(w, '\'') < 0) return -1;
    for(i = 0; i < len; i++) {
        if(w->line_width > 0 && on_line + 2 > (size_t)budget) {
            if(vn_break(w, level + 1) < 0) return -1;
            on_line = 0;
        }
        if(vn_putc(w, hexdigits[buf[i] >> 4]) < 0) return -1;
        if(vn_putc(w, hexdigits[buf[i] & 0x0f]) < 0) return -1;
        on_line += 2;
    }
    return vn_puts(w, "'H");
}

int
vn_h_octet_string(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                  const void *sptr, int level) {
    const OCTET_STRING_t *os = (const OCTET_STRING_t *)sptr;
    (void)td;
    return vn_put_hex(w, os->buf, os->buf ? os->size : 0, level);
}

/*
 * BIT STRING. X.680 allows bstring and hstring; an hstring carries exactly four
 * bits per digit, so it is only usable when the bit count divides by four.
 * Choosing bstring otherwise means no padding bit is ever invented or lost.
 * Named bit lists are not retained by asn1c at runtime, so `{ keyCert }` form
 * is out of reach; see README.
 */
int
vn_h_bit_string(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    static const char hexdigits[] = "0123456789ABCDEF";
    const BIT_STRING_t *bs = (const BIT_STRING_t *)sptr;
    size_t nbits, i;

    (void)level;
    if(bs->bits_unused < 0 || bs->bits_unused > 7)
        return vn_fail(w, td, sptr,
                       "BIT STRING has bits_unused = %d, which must be 0..7",
                       bs->bits_unused);
    nbits = (bs->buf && bs->size > 0)
                ? bs->size * 8 - (size_t)bs->bits_unused
                : 0;

    if(nbits > 0 && nbits % 4 == 0) {
        size_t ndigits = nbits / 4;
        if(vn_putc(w, '\'') < 0) return -1;
        for(i = 0; i < ndigits; i++) {
            unsigned nib = bs->buf[i / 2];
            nib = (i % 2 == 0) ? (nib >> 4) : (nib & 0x0fu);
            if(vn_putc(w, hexdigits[nib]) < 0) return -1;
        }
        return vn_puts(w, "'H");
    }

    if(vn_putc(w, '\'') < 0) return -1;
    for(i = 0; i < nbits; i++) {
        unsigned bit = (bs->buf[i / 8] >> (7 - i % 8)) & 1u;
        if(vn_putc(w, bit ? '1' : '0') < 0) return -1;
    }
    return vn_puts(w, "'B");
}

/* OBJECT IDENTIFIER and RELATIVE-OID share the form { arc arc arc }. */
static int
vn_put_arcs(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            ssize_t (*get_arcs)(const void *, asn_oid_arc_t *, size_t)) {
    asn_oid_arc_t arcs[32];
    const size_t slots = sizeof arcs / sizeof arcs[0];
    ssize_t count, i;

    count = get_arcs(sptr, arcs, slots);
    if(count < 0)
        return vn_fail(w, td, sptr, "cannot read the arcs of %s",
                       td->name ? td->name : "(unnamed)");
    /* get_arcs reports the true arc count even when it exceeds the slots. */
    if((size_t)count > slots)
        return vn_fail(w, td, sptr,
                       "%s has %d arcs, more than the %u this encoder holds",
                       td->name ? td->name : "(unnamed)", (int)count,
                       (unsigned)slots);

    if(vn_putc(w, '{') < 0) return -1;
    for(i = 0; i < count; i++)
        if(vn_printf(w, " %lu", (unsigned long)arcs[i]) < 0) return -1;
    return vn_puts(w, " }");
}

static ssize_t
vn_oid_arcs(const void *sptr, asn_oid_arc_t *arcs, size_t slots) {
    return OBJECT_IDENTIFIER_get_arcs((const OBJECT_IDENTIFIER_t *)sptr, arcs,
                                      slots);
}

static ssize_t
vn_roid_arcs(const void *sptr, asn_oid_arc_t *arcs, size_t slots) {
    return RELATIVE_OID_get_arcs((const RELATIVE_OID_t *)sptr, arcs, slots);
}

int
vn_h_oid(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
         int level) {
    (void)level;
    return vn_put_arcs(w, td, sptr, vn_oid_arcs);
}

int
vn_h_relative_oid(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                  const void *sptr, int level) {
    (void)level;
    return vn_put_arcs(w, td, sptr, vn_roid_arcs);
}

/*
 * Render an arbitrary-width two's-complement big-endian integer as decimal by
 * repeated division by ten.
 *
 * This exists because asn1c itself stops at intmax_t and falls back to
 * colon-separated hex (INTEGER.c:179-198), which is not value notation.
 */
static int
vn_int_big_decimal(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                   const INTEGER_t *st) {
    unsigned char mag[VN_INT_MAX_OCTETS];
    char digits[VN_INT_MAX_DIGITS];
    size_t len = st->size, ndigits = 0, first;
    int negative;

    if(len == 0) return vn_puts(w, "0");
    if(len > sizeof mag)
        return vn_fail(w, td, st,
                       "INTEGER of %u octets exceeds this encoder's %u octet "
                       "limit", (unsigned)len, (unsigned)sizeof mag);

    negative = (st->buf[0] & 0x80) != 0;
    memcpy(mag, st->buf, len);
    if(negative) { /* negate in place to get the magnitude */
        size_t i = len;
        while(i-- > 0) mag[i] = (unsigned char)~mag[i];
        for(i = len; i-- > 0;)
            if(++mag[i] != 0) break;
    }

    for(first = 0; first + 1 < len && mag[first] == 0; first++)
        ;

    do { /* long division of mag[first..len) by 10, collecting remainders */
        unsigned rem = 0;
        size_t i;
        for(i = first; i < len; i++) {
            unsigned cur = (rem << 8) | mag[i];
            mag[i] = (unsigned char)(cur / 10);
            rem = cur % 10;
        }
        if(ndigits >= sizeof digits)
            return vn_fail(w, td, st, "internal: decimal buffer overflow");
        digits[ndigits++] = (char)('0' + rem);
        while(first < len && mag[first] == 0) first++;
    } while(first < len);

    if(negative && vn_putc(w, '-') < 0) return -1;
    while(ndigits-- > 0)
        if(vn_putc(w, digits[ndigits]) < 0) return -1;
    return 0;
}

static int
vn_int_decimal(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
               const INTEGER_t *st) {
    intmax_t v;
    if(!st->buf || st->size == 0) return vn_puts(w, "0");
    if(asn_INTEGER2imax(st, &v) == 0) return vn_printf(w, "%jd", v);
    return vn_int_big_decimal(w, td, st);
}

int
vn_h_integer(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             int level) {
    (void)level;
    return vn_int_decimal(w, td, (const INTEGER_t *)sptr);
}

int
vn_h_native_integer(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                    const void *sptr, int level) {
    const asn_INTEGER_specifics_t *specs =
        (const asn_INTEGER_specifics_t *)td->specifics;
    (void)level;
    if(specs && specs->field_unsigned)
        return vn_printf(w, "%lu", *(const unsigned long *)sptr);
    return vn_printf(w, "%ld", *(const long *)sptr);
}

/* Shared by ENUMERATED and NativeEnumerated once the value is a long. */
static int
vn_enum_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
              const void *sptr, long value) {
    const asn_INTEGER_specifics_t *specs =
        (const asn_INTEGER_specifics_t *)td->specifics;
    const asn_INTEGER_enum_map_t *item =
        specs ? INTEGER_map_value2enum(specs, value) : 0;

    if(item && item->enum_name) {
        if(vn_puts(w, item->enum_name) < 0) return -1;
        if(w->flags & VN_F_ENUM_WITH_VALUE) {
            /* X.680 EnumeratedValue is an identifier, so the number has to
             * travel in a comment; `green (1)` would not be value notation. */
            if(vn_putc(w, ' ') < 0) return -1;
            if(vn_is_annotated(w)) return vn_comment(w, "(%ld)", value);
            return vn_printf(w, "-- (%ld) --", value);
        }
        return 0;
    }
    if(w->flags & VN_F_LENIENT) return vn_printf(w, "%ld", value);
    return vn_fail(w, td, sptr,
                   "ENUMERATED %s has no identifier for value %ld",
                   td->name ? td->name : "(unnamed)", value);
}

int
vn_h_native_enumerated(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                       const void *sptr, int level) {
    (void)level;
    return vn_enum_value(w, td, sptr, *(const long *)sptr);
}

int
vn_h_enumerated(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    long value;
    (void)level;
    if(asn_INTEGER2long((const INTEGER_t *)sptr, &value) != 0)
        return vn_fail(w, td, sptr,
                       "ENUMERATED %s value does not fit in a long",
                       td->name ? td->name : "(unnamed)");
    return vn_enum_value(w, td, sptr, value);
}
