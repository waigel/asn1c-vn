/*
 * t_bits_oid.c -- BIT STRING, OBJECT IDENTIFIER and RELATIVE-OID.
 *
 * The bit-count cases matter: X.680 permits both bstring and hstring, but an
 * hstring can only represent a bit count divisible by four, so choosing wrongly
 * would invent or drop padding bits.
 */
#include <stdlib.h>
#include <string.h>
#include <OBJECT_IDENTIFIER.h>
#include "RELATIVE-OID.h"
#include "vntest.h"
#include "Holder.h"

static char *
enc_bits(const unsigned char *b, size_t len, int unused) {
    BIT_STRING_t bs;
    char reason[200];
    memset(&bs, 0, sizeof bs);
    bs.buf = (uint8_t *)b;
    bs.size = len;
    bs.bits_unused = unused;
    return vnt_encode(&asn_DEF_BIT_STRING, &bs, 0, reason, sizeof reason);
}

int
main(void) {
    char reason[200], *out;

    VNT_CASE("empty bit string is an empty bstring");
    out = enc_bits((const unsigned char *)"", 0, 0);
    VNT_STREQ(out, "''B");
    free(out);

    VNT_CASE("8 bits divide by 4, so hstring");
    { const unsigned char b[] = {0xab};
      out = enc_bits(b, 1, 0);
      VNT_STREQ(out, "'AB'H");
      free(out); }

    VNT_CASE("12 bits divide by 4, so hstring drops the padding nibble");
    { const unsigned char b[] = {0xab, 0xc0};
      out = enc_bits(b, 2, 4);
      VNT_STREQ(out, "'ABC'H");
      free(out); }

    VNT_CASE("14 bits do not divide by 4, so bstring");
    { const unsigned char b[] = {0x61, 0xd4}; /* 0110000111010100 */
      out = enc_bits(b, 2, 2);
      VNT_STREQ(out, "'01100001110101'B");
      free(out); }

    VNT_CASE("a single set bit");
    { const unsigned char b[] = {0x80};
      out = enc_bits(b, 1, 7);
      VNT_STREQ(out, "'1'B");
      free(out); }

    VNT_CASE("a single clear bit");
    { const unsigned char b[] = {0x00};
      out = enc_bits(b, 1, 7);
      VNT_STREQ(out, "'0'B");
      free(out); }

    VNT_CASE("16 bits across two octets");
    { const unsigned char b[] = {0xde, 0xad};
      out = enc_bits(b, 2, 0);
      VNT_STREQ(out, "'DEAD'H");
      free(out); }

    VNT_CASE("an invalid bits_unused is rejected");
    {
        BIT_STRING_t bs;
        const unsigned char b[] = {0xff};
        memset(&bs, 0, sizeof bs);
        bs.buf = (uint8_t *)b;
        bs.size = 1;
        bs.bits_unused = 9;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_BIT_STRING, &bs, 0, reason,
                                  sizeof reason));
        VNT_TRUE(strstr(reason, "bits_unused") != 0);
    }

    VNT_CASE("object identifier arcs are space separated, without commas");
    {
        OBJECT_IDENTIFIER_t oid;
        asn_oid_arc_t arcs[4];
        arcs[0] = 2; arcs[1] = 23; arcs[2] = 143; arcs[3] = 1;
        memset(&oid, 0, sizeof oid);
        VNT_TRUE(OBJECT_IDENTIFIER_set_arcs(&oid, arcs, 4) == 0);
        out = vnt_encode(&asn_DEF_OBJECT_IDENTIFIER, &oid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 2 23 143 1 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_OBJECT_IDENTIFIER, &oid);
    }

    VNT_CASE("a two-arc object identifier");
    {
        OBJECT_IDENTIFIER_t oid;
        asn_oid_arc_t arcs[2];
        arcs[0] = 1; arcs[1] = 3;
        memset(&oid, 0, sizeof oid);
        VNT_TRUE(OBJECT_IDENTIFIER_set_arcs(&oid, arcs, 2) == 0);
        out = vnt_encode(&asn_DEF_OBJECT_IDENTIFIER, &oid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 1 3 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_OBJECT_IDENTIFIER, &oid);
    }

    VNT_CASE("a large arc value survives");
    {
        OBJECT_IDENTIFIER_t oid;
        asn_oid_arc_t arcs[3];
        arcs[0] = 1; arcs[1] = 2; arcs[2] = 4294967295u;
        memset(&oid, 0, sizeof oid);
        VNT_TRUE(OBJECT_IDENTIFIER_set_arcs(&oid, arcs, 3) == 0);
        out = vnt_encode(&asn_DEF_OBJECT_IDENTIFIER, &oid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 1 2 4294967295 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_OBJECT_IDENTIFIER, &oid);
    }

    VNT_CASE("relative oid");
    {
        RELATIVE_OID_t roid;
        asn_oid_arc_t arcs[3];
        arcs[0] = 8; arcs[1] = 5; arcs[2] = 100;
        memset(&roid, 0, sizeof roid);
        VNT_TRUE(RELATIVE_OID_set_arcs(&roid, arcs, 3) == 0);
        out = vnt_encode(&asn_DEF_RELATIVE_OID, &roid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 8 5 100 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_RELATIVE_OID, &roid);
    }

    return vnt_report("t_bits_oid");
}
