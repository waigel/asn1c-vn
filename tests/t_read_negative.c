/*
 * t_read_negative.c -- the reader's error paths.
 *
 * The round-trip test proves valid input works. These prove invalid input is
 * refused rather than quietly mangled, which is the harder half: the reader is
 * the first part of this project to take input it did not produce itself.
 *
 * Every case also runs under the sanitizers in CI, so a failure path that leaks
 * or reads out of bounds shows up here rather than in production.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Nested.h"
#include "Numbers.h"
#include "Choice.h"
#include "Level.h"

static void
rejects(const asn_TYPE_descriptor_t *td, const char *text, const char *expect,
        const char *label) {
    void             *st = 0;
    vn_read_options_t ro;
    char              reason[400];
    asn_dec_rval_t    dv;

    VNT_CASE(label);
    memset(&ro, 0, sizeof ro);
    ro.flags = VN_RF_EOF;
    ro.errbuf = reason;
    ro.errlen = sizeof reason;
    reason[0] = '\0';

    dv = vn_decode(0, td, &st, &ro, text, strlen(text));
    if(dv.code == RC_OK) {
        fprintf(stderr, "FAIL [%s]: accepted |%s|\n", label, text);
        vnt_failures++;
    } else if(expect && !strstr(reason, expect)) {
        fprintf(stderr, "FAIL [%s]: reason lacks '%s': %s\n", label, expect,
                reason[0] ? reason : "(none)");
        vnt_failures++;
    } else if(!reason[0] && dv.code == RC_FAIL) {
        fprintf(stderr, "FAIL [%s]: rejected without a reason\n", label);
        vnt_failures++;
    }
    /* The caller owns the partial tree even after a failure. */
    if(st) ASN_STRUCT_FREE(*td, st);
}

static void
accepts(const asn_TYPE_descriptor_t *td, const char *text, unsigned flags,
        const char *label) {
    void             *st = 0;
    vn_read_options_t ro;
    char              reason[400];
    asn_dec_rval_t    dv;

    VNT_CASE(label);
    memset(&ro, 0, sizeof ro);
    ro.flags = VN_RF_EOF | flags;
    ro.errbuf = reason;
    ro.errlen = sizeof reason;
    reason[0] = '\0';

    dv = vn_decode(0, td, &st, &ro, text, strlen(text));
    if(dv.code != RC_OK) {
        fprintf(stderr, "FAIL [%s]: rejected |%s|: %s\n", label, text,
                reason[0] ? reason : "(no reason)");
        vnt_failures++;
    }
    if(st) ASN_STRUCT_FREE(*td, st);
}

int
main(void) {
    /* --- structure ------------------------------------------------------- */

    rejects(&asn_DEF_Nested, "{ nosuch 1 }", "no member", "unknown member name");

    rejects(&asn_DEF_Nested,
            "{ name '00'H, name '01'H, inner { id 1, tag '00'H }, col red }",
            "twice", "duplicate member");

    /* X.680 requires the components in declaration order. */
    rejects(&asn_DEF_Nested,
            "{ col red, name '00'H, inner { id 1, tag '00'H } }", "out of order",
            "members out of order");

    rejects(&asn_DEF_Nested, "{ inner { id 1, tag '00'H }, col red }",
            "missing mandatory", "missing mandatory inline member");

    rejects(&asn_DEF_Nested, "{ name '00'H, col red }", "missing mandatory",
            "missing mandatory constructed member");

    rejects(&asn_DEF_Nested, "{ , name '00'H }", "comma",
            "comma before the first member");

    rejects(&asn_DEF_Numbers, "{ 1, }", 0, "trailing comma in a list");

    rejects(&asn_DEF_Numbers, "{ 1 2 }", 0, "missing comma in a list");

    /* --- CHOICE ---------------------------------------------------------- */

    rejects(&asn_DEF_Choice, "nosuch : TRUE", "no alternative",
            "unknown alternative");
    rejects(&asn_DEF_Choice, "flag TRUE", "expected :", "missing colon");
    rejects(&asn_DEF_Choice, "flag :", 0, "nothing after the colon");

    /* --- scalars --------------------------------------------------------- */

    rejects(&asn_DEF_Numbers, "{ '00GG'H }", 0, "bad hex digit");
    rejects(&asn_DEF_Numbers, "{ '012'B }", 0, "bad binary digit");
    rejects(&asn_DEF_Nested, "{ name '000'H, inner { id 1, tag '00'H }, col red }",
            "even number", "odd hex digit count for an OCTET STRING");

    rejects(&asn_DEF_Choice, "flag : MAYBE", "TRUE or FALSE", "bad boolean");
    rejects(&asn_DEF_Choice, "nothing : NIL", "expected NULL", "bad null");

    /* ENUMERATED admits only the identifier, per X.680. */
    rejects(&asn_DEF_Nested, "{ name '00'H, inner { id 1, tag '00'H }, col 0 }",
            "needs an identifier", "number for an ENUMERATED");
    accepts(&asn_DEF_Nested, "{ name '00'H, inner { id 1, tag '00'H }, col 0 }",
            VN_RF_LENIENT, "a number for ENUMERATED passes under VN_RF_LENIENT");
    rejects(&asn_DEF_Nested,
            "{ name '00'H, inner { id 1, tag '00'H }, col purple }",
            "not a value of", "unknown enumeration identifier");

    /* Without an annotation table an INTEGER identifier cannot be resolved. */
    rejects(&asn_DEF_Level, "medium", "not a known identifier",
            "identifier for an INTEGER without a table");

    /* --- truncation ------------------------------------------------------ */

    rejects(&asn_DEF_Nested, "{ name '00'H, inner { id 1", 0,
            "truncated input with EOF set");
    rejects(&asn_DEF_Numbers, "{ '00AA", 0, "unterminated hstring");
    rejects(&asn_DEF_Nested, "{ name \"unterminated", 0, "unterminated cstring");
    rejects(&asn_DEF_Nested, "", 0, "empty input");

    /* --- without EOF, truncation asks for more rather than failing -------- */

    VNT_CASE("truncated input without EOF asks for more");
    {
        void             *st = 0;
        vn_read_options_t ro;
        const char       *text = "{ name '00'H, inner { id 1";
        asn_dec_rval_t    dv;
        memset(&ro, 0, sizeof ro);
        dv = vn_decode(0, &asn_DEF_Nested, &st, &ro, text, strlen(text));
        VNT_TRUE(dv.code == RC_WMORE);
        if(st) ASN_STRUCT_FREE(asn_DEF_Nested, st);
    }

    /* --- the codec reads one value and stops ----------------------------- */

    VNT_CASE("content after the value is left to the caller");
    {
        void             *st = 0;
        vn_read_options_t ro;
        const char       *text = "{ 1, 2 }  trailing junk here";
        asn_dec_rval_t    dv;
        memset(&ro, 0, sizeof ro);
        ro.flags = VN_RF_EOF;
        dv = vn_decode(0, &asn_DEF_Numbers, &st, &ro, text, strlen(text));
        VNT_TRUE(dv.code == RC_OK);
        VNT_TRUE(dv.consumed == 8); /* just "{ 1, 2 }" */
        if(st) ASN_STRUCT_FREE(asn_DEF_Numbers, st);
    }

    /* --- comments and whitespace are skipped ----------------------------- */

    accepts(&asn_DEF_Nested,
            "{ -- a comment -- name '00'H,\n  inner { id 1, tag '00'H },\n"
            "  col red -- trailing comment\n}",
            0, "comments and newlines are skipped");

    /*
     * 12.6.2 gives the comment two forms, and 12.6.4 is the one the reader was
     * missing: a multi-line comment opened with slash-star. It has been in
     * X.680 since the 2002 edition, and TCA's own reference ProfileElements use
     * it, so a reader without it rejects real files.
     */
    accepts(&asn_DEF_Nested,
            "{ /* a block comment */ name '00'H,\n"
            "  inner { id 1, tag '00'H },\n"
            "  /* spanning\n     several lines */ col red\n}",
            0, "a multi-line comment is skipped");

    /* 12.6.4: an opener found before the closer nests, and the comment ends
     * only once every one of them has been matched. */
    accepts(&asn_DEF_Nested,
            "{ /* outer /* inner */ still the comment */ name '00'H,\n"
            "  inner { id 1, tag '00'H }, col red }",
            0, "multi-line comments nest");

    /* 12.6.3: inside a one-line comment the block delimiters mean nothing. */
    accepts(&asn_DEF_Nested,
            "{ -- a /* b */ c\n name '00'H, inner { id 1, tag '00'H },"
            " col red }",
            0, "block delimiters inside a one-line comment are ordinary text");

    /* 12.6.4: and the other way round, a double hyphen inside a block one. */
    accepts(&asn_DEF_Nested,
            "{ /* a -- b */ name '00'H, inner { id 1, tag '00'H }, col red }",
            0, "a double hyphen inside a multi-line comment is ordinary text");

    rejects(&asn_DEF_Nested,
            "{ /* never closed name '00'H, inner { id 1, tag '00'H } }", 0,
            "an unterminated multi-line comment");

    return vnt_report("t_read_negative");
}
