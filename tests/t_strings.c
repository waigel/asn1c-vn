/*
 * t_strings.c -- restricted string types and the time types, all rendered as
 * X.680 cstrings.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "AllStrings.h"

static char *
enc(const asn_TYPE_descriptor_t *td, const char *s, size_t len,
    const vn_options_t *o, char *reason, size_t rlen) {
    OCTET_STRING_t os;
    memset(&os, 0, sizeof os);
    os.buf = (uint8_t *)s;
    os.size = len;
    return vnt_encode(td, &os, o, reason, rlen);
}

int
main(void) {
    char reason[240], *out;

    VNT_CASE("empty string");
    out = enc(&asn_DEF_UTF8String, "", 0, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\"");
    free(out);

    VNT_CASE("plain ascii");
    out = enc(&asn_DEF_IA5String, "hello", 5, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"hello\"");
    free(out);

    /* X.680 11.14: a quotation mark inside a cstring is written twice. */
    VNT_CASE("an embedded quote is doubled");
    out = enc(&asn_DEF_UTF8String, "a\"b", 3, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"a\"\"b\"");
    free(out);

    VNT_CASE("only the quote is special; a backslash is literal");
    out = enc(&asn_DEF_UTF8String, "a\\b", 3, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"a\\b\"");
    free(out);

    VNT_CASE("a string that is only quotes");
    out = enc(&asn_DEF_UTF8String, "\"\"", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\"\"\"\"\"");
    free(out);

    VNT_CASE("utf-8 bytes pass through unchanged");
    out = enc(&asn_DEF_UTF8String, "\xc3\xa4", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\xc3\xa4\"");
    free(out);

    VNT_CASE("every restricted string type is handled");
    {
        const asn_TYPE_descriptor_t *tds[] = {
            &asn_DEF_UTF8String,   &asn_DEF_IA5String,
            &asn_DEF_PrintableString, &asn_DEF_NumericString,
            &asn_DEF_VisibleString, &asn_DEF_GeneralString,
            &asn_DEF_GraphicString, &asn_DEF_TeletexString,
            &asn_DEF_VideotexString, &asn_DEF_ObjectDescriptor
        };
        size_t i;
        for(i = 0; i < sizeof tds / sizeof tds[0]; i++) {
            out = enc(tds[i], "ab", 2, 0, reason, sizeof reason);
            if(!out)
                fprintf(stderr, "  %s failed: %s\n",
                        tds[i]->name ? tds[i]->name : "?", reason);
            VNT_STREQ(out, "\"ab\"");
            free(out);
        }
    }

    /* A cstring cannot carry control characters; X.680 has a separate
     * character-defs form for those, which this encoder does not implement. */
    VNT_CASE("a control character fails by default");
    VNT_TRUE(enc(&asn_DEF_IA5String, "a\nb", 3, 0, reason, sizeof reason) == 0);
    VNT_TRUE(strstr(reason, "control") != 0);

    VNT_CASE("DEL is also rejected");
    VNT_TRUE(enc(&asn_DEF_IA5String, "a\x7f", 2, 0, reason, sizeof reason) == 0);

    VNT_CASE("a control character passes under VN_F_LENIENT");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_LENIENT;
        out = enc(&asn_DEF_IA5String, "a\nb", 3, &o, reason, sizeof reason);
        VNT_STREQ(out, "\"a\nb\"");
        free(out);
    }

    /* U+00E4 is 00 E4 in UTF-16BE and C3 A4 in UTF-8. */
    VNT_CASE("BMPString is transcoded from UTF-16BE to UTF-8");
    out = enc(&asn_DEF_BMPString, "\x00\xe4", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\xc3\xa4\"");
    free(out);

    VNT_CASE("BMPString ascii");
    out = enc(&asn_DEF_BMPString, "\x00h\x00i", 4, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"hi\"");
    free(out);

    /* U+1F600 is 00 01 F6 00 in UTF-32BE and F0 9F 98 80 in UTF-8. */
    VNT_CASE("UniversalString is transcoded from UTF-32BE to UTF-8");
    out = enc(&asn_DEF_UniversalString, "\x00\x01\xf6\x00", 4, 0, reason,
              sizeof reason);
    VNT_STREQ(out, "\"\xf0\x9f\x98\x80\"");
    free(out);

    VNT_CASE("an odd-length BMPString is rejected");
    VNT_TRUE(enc(&asn_DEF_BMPString, "\x00", 1, 0, reason, sizeof reason) == 0);
    VNT_TRUE(strstr(reason, "BMPString") != 0);

    VNT_CASE("a misaligned UniversalString is rejected");
    VNT_TRUE(enc(&asn_DEF_UniversalString, "\x00\x00\x00", 3, 0, reason,
                 sizeof reason) == 0);

    VNT_CASE("an unpaired surrogate in BMPString is rejected");
    VNT_TRUE(enc(&asn_DEF_BMPString, "\xd8\x00", 2, 0, reason,
                 sizeof reason) == 0);
    VNT_TRUE(strstr(reason, "surrogate") != 0);

    VNT_CASE("a quote inside BMPString is doubled");
    out = enc(&asn_DEF_BMPString, "\x00\x22", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\"\"\"");
    free(out);

    VNT_CASE("GeneralizedTime is a cstring of the raw bytes");
    out = enc(&asn_DEF_GeneralizedTime, "20260729124800Z", 15, 0, reason,
              sizeof reason);
    VNT_STREQ(out, "\"20260729124800Z\"");
    free(out);

    VNT_CASE("UTCTime is a cstring of the raw bytes");
    out = enc(&asn_DEF_UTCTime, "260729124800Z", 13, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"260729124800Z\"");
    free(out);

    return vnt_report("t_strings");
}
