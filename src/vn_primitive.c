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
#include <OCTET_STRING.h>
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
