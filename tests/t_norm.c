/*
 * t_norm.c -- tests for the XER scanner and the scalar normaliser.
 *
 * Every case here corresponds to a bug found while building the cross-check.
 * These fixtures decide whether real encoder differences are seen at all, so a
 * silent fault in them would be worse than no test.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "xerscan.h"

#define MAXC 8

static void
scan_is(const char *label, const char *xer, const char *expect_joined) {
    char *v[32];
    size_t n = 0, i;
    char err[200];
    char joined[1024];
    size_t used = 0;

    VNT_CASE(label);
    if(!xer_scan_scalars(xer, v, 32, &n, err, sizeof err)) {
        fprintf(stderr, "FAIL [%s]: scan failed: %s\n", label, err);
        vnt_failures++;
        return;
    }
    joined[0] = '\0';
    for(i = 0; i < n; i++) {
        int w = snprintf(joined + used, sizeof joined - used, "%s[%s]",
                         i ? "" : "", v[i]);
        if(w > 0) used += (size_t)w;
    }
    VNT_STREQ(joined, expect_joined);
    for(i = 0; i < n; i++) free(v[i]);
}

static void
vn_norm_is(const char *label, const char *in, const char *expect) {
    char *got;
    VNT_CASE(label);
    got = vn_norm_scalar(in);
    VNT_STREQ(got, expect);
    free(got);
}

/* Does the XER scalar admit `expect` as one of its readings? */
static void
xer_admits(const char *label, const char *in, const char *expect) {
    char *c[MAXC];
    size_t n, i;
    int found = 0;

    VNT_CASE(label);
    n = xer_norm_candidates(in, c, MAXC);
    for(i = 0; i < n; i++)
        if(!strcmp(c[i], expect)) found = 1;
    if(!found) {
        fprintf(stderr, "FAIL [%s]: |%s| yielded %u candidate(s), none |%s|:",
                label, in, (unsigned)n, expect);
        for(i = 0; i < n; i++) fprintf(stderr, " |%s|", c[i]);
        fprintf(stderr, "\n");
        vnt_failures++;
    }
    for(i = 0; i < n; i++) free(c[i]);
}

int
main(void) {
    /* --- the XER scanner ------------------------------------------------- */

    scan_is("text content", "<a>hi</a>", "[hi]");
    scan_is("self-closing tag yields its name", "<flag><true/></flag>",
            "[true]");
    scan_is("empty element yields empty content", "<void></void>", "[]");
    scan_is("a container contributes nothing of its own",
            "<s><a>1</a><b>2</b></s>", "[1][2]");

    /* An empty constructed element is layout, not a value; a single-space string
     * value is a value. Both are whitespace, and only the newline tells them
     * apart. */
    scan_is("empty list contributes nothing", "<numbers>\n    </numbers>", "");
    scan_is("a single-space string value is kept", "<p> </p>", "[ ]");

    /* asn1c wraps long hex across lines and separates pairs with spaces. */
    scan_is("wrapped hex keeps its spaces but loses newlines",
            "<d>\n        FF FF 4C\n        B4\n    </d>", "[        FF FF 4C        B4    ]");

    /* XER is XML, so markup characters arrive escaped. */
    scan_is("xml entities are decoded", "<n>a&amp;b&lt;c&gt;d</n>",
            "[a&b<c>d]");

    /* --- normalising value notation -------------------------------------- */

    vn_norm_is("boolean true", "TRUE", "B:1");
    vn_norm_is("boolean false", "FALSE", "B:0");
    vn_norm_is("null", "NULL", "N:");
    vn_norm_is("integer", "-129", "I:-129");
    vn_norm_is("hstring", "'00AABB'H", "H:00AABB");
    vn_norm_is("empty hstring", "''H", "H:");
    vn_norm_is("bstring not divisible by four", "'01100'B", "Z:01100");
    vn_norm_is("bstring divisible by four becomes hex", "'0110'B", "H:6");
    vn_norm_is("cstring", "\"hi\"", "S:hi");
    vn_norm_is("empty cstring", "\"\"", "S:");
    vn_norm_is("cstring with a doubled quote", "\"a\"\"b\"", "S:a\"b");
    vn_norm_is("cstring of only two quotes", "\"\"\"\"\"\"", "S:\"\"");
    vn_norm_is("arc list", "2 23 143 1", "O:2.23.143.1");

    /* Pretty mode wraps long hex, so whitespace can sit inside the quotes;
     * X.680 does not count it as part of the value. */
    vn_norm_is("wrapped hstring ignores interior whitespace",
               "'00AA\n        BB'H", "H:00AABB");

    /* A string is not an arc list even when it looks like one. This is why the
     * scanner keeps the quotes. */
    vn_norm_is("a quoted digits-and-spaces value stays a string", "\"34 9\"",
               "S:34 9");
    vn_norm_is("a lone space is not an arc list", "\" \"", "S: ");

    /* --- normalising XER, which is ambiguous ----------------------------- */

    xer_admits("xer true", "true", "B:1");
    xer_admits("xer integer", "129", "I:129");
    xer_admits("xer octets", "00AABB", "H:00AABB");
    xer_admits("xer bits divisible by four", "0110", "H:6");
    xer_admits("xer bits not divisible by four", "01100", "Z:01100");
    xer_admits("digits can also be an octet string", "0110", "H:0110");
    xer_admits("hex-looking text can also be a string", "AB", "S:AB");
    xer_admits("digits can also be a string", "129", "S:129");
    xer_admits("xer oid", "2.23.143.1", "O:2.23.143.1");
    xer_admits("space-separated hex packs down", "FF FF 4C", "H:FFFF4C");
    xer_admits("wrapped hex with indentation packs down",
               "        FF FF 4C        B4    ", "H:FFFF4CB4");
    xer_admits("a spacey value can also be a string", "34 9", "S:34 9");

    /* An empty element is any of these. */
    xer_admits("empty is an empty string", "", "S:");
    xer_admits("empty is an empty octet string", "", "H:");
    xer_admits("empty is an empty bit string", "", "Z:");
    xer_admits("empty is a NULL", "", "N:");

    /* Two literal quotes in XER must not be read as an empty string. */
    VNT_CASE("xer text of two quotes is a two-character value");
    {
        char *c[MAXC];
        size_t n = xer_norm_candidates("\"\"", c, MAXC), i;
        int has_two_quote_string = 0;
        for(i = 0; i < n; i++)
            if(!strcmp(c[i], "S:\"\"")) has_two_quote_string = 1;
        VNT_TRUE(has_two_quote_string);
        for(i = 0; i < n; i++) free(c[i]);
    }

    VNT_CASE("an unrecognised value notation shape is rejected, not guessed");
    {
        char *got = vn_norm_scalar("'00GG'H"); /* G is not a hex digit */
        VNT_TRUE(got == 0);
        free(got);
    }

    return vnt_report("t_norm");
}
