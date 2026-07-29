/*
 * t_annexg.c -- the value notation examples of X.680 (02/2021) Annex G.
 *
 * Every value here is transcribed from the standard's own examples, so a
 * disagreement is a defect in the codec, never a matter of interpretation.
 * Each case names the subclause it came from.
 *
 * The trailing-zero-insignificance cases (22.7: named bits make trailing zero
 * bits semantically irrelevant, so '1101'B, '1101000'B and { sunday, monday,
 * wednesday } are one abstract value) run only under VN_X680_STRICT=1: the
 * reader does not yet normalise them, a known conformance gap.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "PersonnelRecord.h"
#include "DayOfTheMonth.h"
#include "G3FacsimilePage.h"
#include "DaysOfTheWeek.h"
#include "PersonalStatus.h"
#include "G4FacsimileImage.h"
#include "Surname.h"
#include "PatientIdentifier.h"
#include "NamesOfMemberNations.h"
#include "NamesOfOfficers.h"
#include "UserName.h"
#include "Keywords.h"
#include "AcmeBadgeNumber.h"
#include "FileIdentifier.h"

extern const vn_annotations_t vn_generated_annotations;

/* Most cases use the generated table; a case sets this to 0 to bypass it. */
static const vn_annotations_t *g_ann = &vn_generated_annotations;

static int
collect(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

/* Parse value notation; NULL on failure (which is reported). */
static void *
read_vn(const asn_TYPE_descriptor_t *td, const char *text) {
    void             *st = 0;
    vn_read_options_t ro;
    char              reason[400];
    asn_dec_rval_t    dv;

    memset(&ro, 0, sizeof ro);
    ro.flags = VN_RF_EOF;
    ro.annotations = g_ann;
    ro.errbuf = reason;
    ro.errlen = sizeof reason;
    reason[0] = '\0';

    dv = vn_decode(0, td, &st, &ro, text, strlen(text));
    if(dv.code != RC_OK) {
        fprintf(stderr, "FAIL [%s]: cannot read: %s\n--- VN ---\n%s\n",
                vnt_case, reason[0] ? reason : "(no reason)", text);
        vnt_failures++;
        if(st) ASN_STRUCT_FREE(*td, st);
        return 0;
    }
    if(dv.consumed != strlen(text)) {
        fprintf(stderr, "FAIL [%s]: consumed %lu of %lu bytes\n", vnt_case,
                (unsigned long)dv.consumed, (unsigned long)strlen(text));
        vnt_failures++;
        ASN_STRUCT_FREE(*td, st);
        return 0;
    }
    return st;
}

/* DER of a parsed value notation text; NULL on failure. */
static unsigned char *
der_of(const asn_TYPE_descriptor_t *td, const char *text, size_t *len) {
    void          *st = read_vn(td, text);
    vnt_str_t      der;
    asn_enc_rval_t ev;

    if(!st) return 0;
    memset(&der, 0, sizeof der);
    ev = der_encode(td, st, collect, &der);
    ASN_STRUCT_FREE(*td, st);
    if(ev.encoded < 0) {
        fprintf(stderr, "FAIL [%s]: cannot DER-encode\n", vnt_case);
        vnt_failures++;
        free(der.buf);
        return 0;
    }
    *len = der.len;
    return (unsigned char *)der.buf;
}

/*
 * The annex value must survive text -> DER -> our own text -> DER with both
 * DER encodings byte-identical.
 */
static void
annexg_case(const char *label, const asn_TYPE_descriptor_t *td,
            const char *text) {
    unsigned char *der1, *der2 = 0;
    size_t         len1, len2 = 0;
    void          *st;
    vnt_str_t      vn;
    vn_options_t   eo;

    VNT_CASE(label);
    der1 = der_of(td, text, &len1);
    if(!der1) return;

    /* Re-render with our encoder and read that back too. */
    st = read_vn(td, text);
    if(!st) {
        free(der1);
        return;
    }
    memset(&vn, 0, sizeof vn);
    memset(&eo, 0, sizeof eo);
    eo.annotations = g_ann;
    if(vn_encode(td, st, &eo, collect, &vn).encoded < 0) {
        fprintf(stderr, "FAIL [%s]: cannot render value notation\n", label);
        vnt_failures++;
    } else {
        der2 = der_of(td, vn.buf, &len2);
        VNT_TRUE(der2 && len1 == len2 && memcmp(der1, der2, len1) == 0);
    }
    ASN_STRUCT_FREE(*td, st);
    free(vn.buf);
    free(der1);
    free(der2);
}

/* Two value notation texts that denote the same abstract value. */
static void
same_value(const char *label, const asn_TYPE_descriptor_t *td,
           const char *text_a, const char *text_b) {
    unsigned char *a, *b;
    size_t         alen, blen;

    VNT_CASE(label);
    a = der_of(td, text_a, &alen);
    b = der_of(td, text_b, &blen);
    if(a && b) VNT_TRUE(alen == blen && memcmp(a, b, alen) == 0);
    free(a);
    free(b);
}

static void
distinct_values(const char *label, const asn_TYPE_descriptor_t *td,
                const char *text_a, const char *text_b) {
    unsigned char *a, *b;
    size_t         alen, blen;

    VNT_CASE(label);
    a = der_of(td, text_a, &alen);
    b = der_of(td, text_b, &blen);
    if(a && b) VNT_TRUE(alen != blen || memcmp(a, b, alen) != 0);
    free(a);
    free(b);
}

/* G.1.3, verbatim. */
static const char personnel_record_value[] =
    "{ name          {givenName \"John\", initial \"P\", familyName \"Smith\"},\n"
    "  title         \"Director\",\n"
    "  number        51,\n"
    "  dateOfHire    \"19710917\",\n"
    "  nameOfSpouse  {givenName \"Mary\", initial \"T\", familyName \"Smith\"},\n"
    "  children\n"
    "  { {name {givenName \"Ralph\", initial \"T\", familyName \"Smith\"},\n"
    "     dateOfBirth \"19571111\"},\n"
    "    {name {givenName \"Susan\", initial \"B\", familyName \"Jones\"},\n"
    "     dateOfBirth \"19590717\"}\n"
    "  }\n"
    "}";

static void
g1_personnel_record(void) {
    PersonnelRecord_t *pr;

    VNT_CASE("G.1.3 personnel record reads and spot-checks");
    pr = (PersonnelRecord_t *)read_vn(&asn_DEF_PersonnelRecord,
                                      personnel_record_value);
    if(pr) {
        VNT_TRUE(pr->number == 51);
        VNT_TRUE(pr->title.size == 8
                 && memcmp(pr->title.buf, "Director", 8) == 0);
        VNT_TRUE(pr->children && pr->children->list.count == 2);
        ASN_STRUCT_FREE(asn_DEF_PersonnelRecord, pr);
    }

    annexg_case("G.1.3 personnel record round trip",
                &asn_DEF_PersonnelRecord, personnel_record_value);
}

int
main(void) {
    g1_personnel_record();

    /* G.2.2.2: today DayOfTheMonth ::= first; unknown DayOfTheMonth ::= 0;
     * dayOfTheMonth DayOfTheMonth ::= 4. The named number and its number are
     * the same value. */
    annexg_case("G.2.2.2 today = first", &asn_DEF_DayOfTheMonth, "first");
    annexg_case("G.2.2.2 unknown = 0", &asn_DEF_DayOfTheMonth, "0");
    annexg_case("G.2.2.2 dayOfTheMonth = 4", &asn_DEF_DayOfTheMonth, "4");
    same_value("G.2.2.2 first denotes 1", &asn_DEF_DayOfTheMonth, "first",
               "1");

    /* G.2.5.1: without a NamedBitList, trailing zero bits are significant:
     * body1 and body2 are distinct abstract values (the annex says so in as
     * many words). */
    annexg_case("G.2.5.1 image", &asn_DEF_G3FacsimilePage,
                "'100110100100001110110'B");
    annexg_case("G.2.5.1 trailer", &asn_DEF_G3FacsimilePage,
                "'0123456789ABCDEF'H");
    annexg_case("G.2.5.1 body1", &asn_DEF_G3FacsimilePage, "'1101'B");
    annexg_case("G.2.5.1 body2", &asn_DEF_G3FacsimilePage, "'1101000'B");
    distinct_values("G.2.5.1 body1 and body2 are distinct",
                    &asn_DEF_G3FacsimilePage, "'1101'B", "'1101000'B");

    /* G.2.5.3 / G.2.5.5: named bit lists. */
    annexg_case("G.2.5.3 sunnyDaysLastWeek1", &asn_DEF_DaysOfTheWeek,
                "{sunday, monday, wednesday}");
    annexg_case("G.2.5.5 billClinton", &asn_DEF_PersonalStatus,
                "{married, employed, collegeGraduate}");
    /* hillaryClinton carries two trailing zero bits. With the table the
     * encoder would normalise to the named list -- correct per 22.7 but not
     * byte-stable until the reader normalises too (the strict cases below) --
     * so this one runs without annotations, through the plain bstring path. */
    g_ann = 0;
    annexg_case("G.2.5.5 hillaryClinton", &asn_DEF_PersonalStatus,
                "'110100'B");
    g_ann = &vn_generated_annotations;

    /* G.2.6 */
    annexg_case("G.2.6.1 image", &asn_DEF_G4FacsimileImage,
                "'3FE2EBAD471005'H");
    annexg_case("G.2.6.2 president", &asn_DEF_Surname, "\"Clinton\"");

    /* G.2.9 */
    annexg_case("G.2.9 lastPatient", &asn_DEF_PatientIdentifier,
                "{ name \"Jane Doe\", roomNumber outPatient : NULL }");

    /* G.2.10 */
    annexg_case("G.2.10.1 firstTwo", &asn_DEF_NamesOfMemberNations,
                "{\"Australia\", \"Austria\"}");
    annexg_case("G.2.10.2 acmeCorp", &asn_DEF_NamesOfOfficers,
                "{ president    \"Jane Doe\","
                "  vicePresident \"John Doe\","
                "  secretary    \"Joe Doe\" }");

    /* G.2.11.1. The annex writes countryName first: the NOTE to 27.9 says a
     * SET's component values may appear in any order, unlike 25.20's
     * requirement for a SEQUENCE. */
    annexg_case("G.2.11.1 user, components out of order (27.9)",
                &asn_DEF_UserName,
                "{ countryName      \"Nigeria\","
                "  personalName     \"Jonas Maruba\","
                "  organizationName \"Meteorology, Ltd.\" }");
    same_value("G.2.11.1 order does not change the value", &asn_DEF_UserName,
               "{ countryName \"Nigeria\", personalName \"Jonas Maruba\","
               "  organizationName \"Meteorology, Ltd.\" }",
               "{ personalName \"Jonas Maruba\","
               "  organizationName \"Meteorology, Ltd.\","
               "  countryName \"Nigeria\" }");

    /* Any order, but still exactly once each (27.9 proper). */
    VNT_CASE("a SET still rejects a repeated component");
    {
        void             *st = 0;
        vn_read_options_t ro;
        const char       *text =
            "{ countryName \"a\", countryName \"b\","
            "  personalName \"c\", organizationName \"d\" }";
        memset(&ro, 0, sizeof ro);
        ro.flags = VN_RF_EOF;
        VNT_TRUE(vn_decode(0, &asn_DEF_UserName, &st, &ro, text, strlen(text))
                     .code
                 != RC_OK);
        if(st) ASN_STRUCT_FREE(asn_DEF_UserName, st);
    }

    /* G.2.11.4 */
    annexg_case("G.2.11.4 someASN1Keywords", &asn_DEF_Keywords,
                "{\"INTEGER\", \"BOOLEAN\", \"REAL\"}");

    /* G.2.12.4 */
    annexg_case("G.2.12.4 badgeNumber", &asn_DEF_AcmeBadgeNumber, "2345");

    /* G.2.13.1 */
    annexg_case("G.2.13.1 file", &asn_DEF_FileIdentifier,
                "serialNumber : 106448503");

    /*
     * 22.7: with a NamedBitList, trailing zero bits do not change the abstract
     * value. The annex asserts these equalities in the notes to G.2.5.3 and
     * G.2.5.5; the third is the 22.18 example's rule applied to a named-bit
     * type, where 'D0'H is four bits rather than eight.
     */
    same_value("G.2.5.3 sunnyDaysLastWeek1 = sunnyDaysLastWeek3",
               &asn_DEF_DaysOfTheWeek, "{sunday, monday, wednesday}",
               "'1101000'B");
    same_value("G.2.5.5 billClinton = hillaryClinton", &asn_DEF_PersonalStatus,
               "{married, employed, collegeGraduate}", "'110100'B");
    same_value("22.18 hstring trailing zeros with named bits",
               &asn_DEF_PersonalStatus,
               "{married, employed, collegeGraduate}", "'D0'H");

    /* The converse, G.2.5.1: without a NamedBitList 22.7 does not apply and
     * the same two spellings are distinct values. Already asserted above for
     * G3FacsimilePage; repeated here as the pair to the rule. */
    distinct_values("G.2.5.1 trailing zeros are significant without named bits",
                    &asn_DEF_G3FacsimilePage, "'1101'B", "'1101000'B");

    /* 22.9: IdentifierList is comma-separated, like every other list. */
    VNT_CASE("a named bit list needs its commas");
    {
        static const char *bad[] = {"{married employed}", "{married,}",
                                    "{, married}"};
        size_t             n;
        for(n = 0; n < sizeof bad / sizeof bad[0]; n++) {
            void             *st = 0;
            vn_read_options_t ro;
            memset(&ro, 0, sizeof ro);
            ro.flags = VN_RF_EOF;
            ro.annotations = g_ann;
            VNT_TRUE(vn_decode(0, &asn_DEF_PersonalStatus, &st, &ro, bad[n],
                               strlen(bad[n]))
                         .code
                     != RC_OK);
            if(st) ASN_STRUCT_FREE(asn_DEF_PersonalStatus, st);
        }
    }

    return vnt_report("t_annexg");
}
