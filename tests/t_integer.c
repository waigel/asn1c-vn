/*
 * t_integer.c -- INTEGER in both representations, and ENUMERATED.
 *
 * The large-value cases matter because asn1c's own INTEGER printer gives up
 * past intmax_t and emits colon-separated hex, which is not value notation.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"
#include "Colour.h"

/* Decode a DER INTEGER into the buffer-backed representation, then encode it. */
static void
check_int(const char *label, const unsigned char *der, size_t derlen,
          const char *want) {
    INTEGER_t *v = 0;
    asn_dec_rval_t rv;
    char reason[200], *out;

    VNT_CASE(label);
    rv = ber_decode(0, &asn_DEF_INTEGER, (void **)&v, der, derlen);
    VNT_TRUE(rv.code == RC_OK);
    if(rv.code != RC_OK) return;
    out = vnt_encode(&asn_DEF_INTEGER, v, 0, reason, sizeof reason);
    VNT_STREQ(out, want);
    free(out);
    ASN_STRUCT_FREE(asn_DEF_INTEGER, v);
}

int
main(void) {
    char reason[200], *out;
    long small;
    Colour_t col;

    VNT_CASE("native integer zero");
    small = 0;
    out = vnt_encode(&asn_DEF_NativeInteger, &small, 0, reason, sizeof reason);
    VNT_STREQ(out, "0");
    free(out);

    VNT_CASE("native integer negative");
    small = -12345;
    out = vnt_encode(&asn_DEF_NativeInteger, &small, 0, reason, sizeof reason);
    VNT_STREQ(out, "-12345");
    free(out);

    VNT_CASE("native integer at LONG_MIN");
    small = (-9223372036854775807L - 1L);
    out = vnt_encode(&asn_DEF_NativeInteger, &small, 0, reason, sizeof reason);
    VNT_STREQ(out, "-9223372036854775808");
    free(out);

    { const unsigned char d[] = {0x02, 0x01, 0x00};
      check_int("INTEGER 0", d, sizeof d, "0"); }
    { const unsigned char d[] = {0x02, 0x01, 0x7f};
      check_int("INTEGER 127", d, sizeof d, "127"); }
    { const unsigned char d[] = {0x02, 0x01, 0x80};
      check_int("INTEGER -128", d, sizeof d, "-128"); }
    { const unsigned char d[] = {0x02, 0x01, 0xff};
      check_int("INTEGER -1", d, sizeof d, "-1"); }
    { const unsigned char d[] = {0x02, 0x02, 0x01, 0x00};
      check_int("INTEGER 256", d, sizeof d, "256"); }

    /* 2^64, one past what an unsigned 64-bit value holds. */
    { const unsigned char d[] = {0x02, 0x09, 0x01, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00};
      check_int("INTEGER 2^64", d, sizeof d, "18446744073709551616"); }
    { const unsigned char d[] = {0x02, 0x09, 0xff, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00};
      check_int("INTEGER -2^64", d, sizeof d, "-18446744073709551616"); }
    /* 2^128 - 1: all ones, needing a leading zero octet to stay positive. */
    { const unsigned char d[] = {0x02, 0x11, 0x00,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      check_int("INTEGER 2^128-1", d, sizeof d,
                "340282366920938463463374607431768211455"); }
    /* -2^127, the most negative 128-bit two's-complement value. */
    { const unsigned char d[] = {0x02, 0x10, 0x80,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      check_int("INTEGER -2^127", d, sizeof d,
                "-170141183460469231731687303715884105728"); }
    /* A value whose magnitude has interior zero octets, to catch a bignum
     * division that mishandles them. */
    { const unsigned char d[] = {0x02, 0x09, 0x01, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x01};
      check_int("INTEGER 2^64+1", d, sizeof d, "18446744073709551617"); }

    VNT_CASE("enumerated identifier");
    col = 1;
    out = vnt_encode(&asn_DEF_Colour, &col, 0, reason, sizeof reason);
    VNT_STREQ(out, "green");
    free(out);

    VNT_CASE("enumerated first and last members");
    col = 0;
    out = vnt_encode(&asn_DEF_Colour, &col, 0, reason, sizeof reason);
    VNT_STREQ(out, "red");
    free(out);
    col = 2;
    out = vnt_encode(&asn_DEF_Colour, &col, 0, reason, sizeof reason);
    VNT_STREQ(out, "blue");
    free(out);

    /* X.680 EnumeratedValue is an identifier, so the number cannot appear as
     * `blue (2)`; it has to be a comment. */
    VNT_CASE("enumerated value annotation uses a comment");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_ENUM_WITH_VALUE;
        col = 2;
        out = vnt_encode(&asn_DEF_Colour, &col, &o, reason, sizeof reason);
        VNT_STREQ(out, "blue -- (2) --");
        free(out);
    }

    VNT_CASE("unknown enumerated value fails under strict enumeration");
    col = 99;
    VNT_TRUE(vnt_encode_fails(&asn_DEF_Colour, &col, 0, reason, sizeof reason));
    VNT_TRUE(strstr(reason, "99") != 0);

    VNT_CASE("unknown enumerated value becomes a number under VN_F_LENIENT");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_LENIENT;
        col = 99;
        out = vnt_encode(&asn_DEF_Colour, &col, &o, reason, sizeof reason);
        VNT_STREQ(out, "99");
        free(out);
    }

    return vnt_report("t_integer");
}
