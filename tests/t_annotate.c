/*
 * t_annotate.c -- the annotation table: lookup, and its effect on output.
 *
 * asn1c keeps INTEGER named numbers and BIT STRING named bits only in the
 * generated headers. The table supplies them at runtime; without it the encoder
 * emits the numeric forms, which are equally valid X.680.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"
#include "Level.h"
#include "Flags.h"

static const vn_named_value_t level_names[] = {
    {"low", 0}, {"medium", 5}, {"high", 10}
};
static const vn_named_value_t flag_names[] = {
    {"keyCert", 0}, {"crlSign", 1}, {"dataEncipher", 3}
};
static const vn_type_names_t all_types[] = {
    {"Level", level_names, 3, 0},
    {"Flags", flag_names, 3, 1}
};
static const vn_annotations_t annotations = {all_types, 2};

static char *
enc_level(long v, const vn_annotations_t *ann, unsigned flags, char *reason,
          size_t rlen) {
    vn_options_t o;
    memset(&o, 0, sizeof o);
    o.annotations = ann;
    o.flags = flags;
    return vnt_encode(&asn_DEF_Level, &v, &o, reason, rlen);
}

static char *
enc_flags(const unsigned char *buf, size_t size, int unused,
          const vn_annotations_t *ann, char *reason, size_t rlen) {
    BIT_STRING_t bs;
    vn_options_t o;
    memset(&bs, 0, sizeof bs);
    memset(&o, 0, sizeof o);
    bs.buf = (uint8_t *)buf;
    bs.size = size;
    bs.bits_unused = unused;
    o.annotations = ann;
    return vnt_encode(&asn_DEF_Flags, &bs, &o, reason, rlen);
}

int
main(void) {
    char reason[200], *out;

    /* --- lookup ---------------------------------------------------------- */

    VNT_CASE("lookup finds a type");
    VNT_TRUE(vn_annotations_find(&annotations, "Level") == &all_types[0]);
    VNT_TRUE(vn_annotations_find(&annotations, "Flags") == &all_types[1]);

    VNT_CASE("lookup misses are not fatal");
    VNT_TRUE(vn_annotations_find(&annotations, "Nonexistent") == 0);
    VNT_TRUE(vn_annotations_find(0, "Level") == 0);
    VNT_TRUE(vn_annotations_find(&annotations, 0) == 0);
    {
        static const vn_annotations_t empty = {0, 0};
        VNT_TRUE(vn_annotations_find(&empty, "Level") == 0);
    }

    /* --- INTEGER named numbers ------------------------------------------- */

    VNT_CASE("without a table an INTEGER stays numeric");
    out = enc_level(5, 0, 0, reason, sizeof reason);
    VNT_STREQ(out, "5");
    free(out);

    VNT_CASE("with a table it becomes an identifier");
    out = enc_level(5, &annotations, 0, reason, sizeof reason);
    VNT_STREQ(out, "medium");
    free(out);

    VNT_CASE("first and last entries resolve");
    out = enc_level(0, &annotations, 0, reason, sizeof reason);
    VNT_STREQ(out, "low");
    free(out);
    out = enc_level(10, &annotations, 0, reason, sizeof reason);
    VNT_STREQ(out, "high");
    free(out);

    /* An unnamed value is not an error for INTEGER: unlike ENUMERATED, the
     * number is itself valid X.680. */
    VNT_CASE("an unnamed value falls back to the number");
    out = enc_level(7, &annotations, 0, reason, sizeof reason);
    VNT_STREQ(out, "7");
    free(out);

    VNT_CASE("negative values fall back too");
    out = enc_level(-3, &annotations, 0, reason, sizeof reason);
    VNT_STREQ(out, "-3");
    free(out);

    VNT_CASE("VN_F_ENUM_WITH_VALUE annotates a named number in a comment");
    out = enc_level(5, &annotations, VN_F_ENUM_WITH_VALUE, reason,
                    sizeof reason);
    VNT_STREQ(out, "medium -- (5) --");
    free(out);

    /* --- BIT STRING named bits ------------------------------------------- */

    VNT_CASE("without a table a BIT STRING stays a bstring or hstring");
    { const unsigned char b[] = {0xc0}; /* bits 0 and 1 set, 2 bits used */
      out = enc_flags(b, 1, 6, 0, reason, sizeof reason);
      VNT_STREQ(out, "'11'B");
      free(out); }

    VNT_CASE("with a table the set bits are named");
    { const unsigned char b[] = {0xc0};
      out = enc_flags(b, 1, 6, &annotations, reason, sizeof reason);
      VNT_STREQ(out, "{ keyCert, crlSign }");
      free(out); }

    VNT_CASE("a single named bit");
    { const unsigned char b[] = {0x80};
      out = enc_flags(b, 1, 7, &annotations, reason, sizeof reason);
      VNT_STREQ(out, "{ keyCert }");
      free(out); }

    VNT_CASE("no bits set gives an empty list");
    { const unsigned char b[] = {0x00};
      out = enc_flags(b, 1, 6, &annotations, reason, sizeof reason);
      VNT_STREQ(out, "{}");
      free(out); }

    VNT_CASE("a gap in the positions is respected");
    { const unsigned char b[] = {0x90}; /* bits 0 and 3 */
      out = enc_flags(b, 1, 4, &annotations, reason, sizeof reason);
      VNT_STREQ(out, "{ keyCert, dataEncipher }");
      free(out); }

    /*
     * A bit set beyond the named list cannot be written in the named form
     * without losing it, so the encoder falls back to the string form rather
     * than dropping information.
     */
    VNT_CASE("an unnamed set bit forces the string form");
    { const unsigned char b[] = {0x84}; /* bits 0 and 5; 5 has no name */
      out = enc_flags(b, 1, 2, &annotations, reason, sizeof reason);
      VNT_STREQ(out, "'100001'B");
      free(out); }

    return vnt_report("t_annotate");
}
