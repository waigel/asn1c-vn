/*
 * t_scan.c -- the scanner is a test fixture, so it gets its own tests. If it
 * were wrong, it would hide encoder bugs rather than catch them.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vnscan.h"

static void
ok(const char *text) {
    char err[200];
    VNT_CASE(text);
    if(!vn_scan_wellformed(text, err, sizeof err)) {
        fprintf(stderr, "FAIL: expected well-formed: %s\n  in: %s\n", err, text);
        vnt_failures++;
    }
}

static void
bad(const char *text) {
    char err[200];
    VNT_CASE(text);
    if(vn_scan_wellformed(text, err, sizeof err)) {
        fprintf(stderr, "FAIL: expected malformed but was accepted: %s\n", text);
        vnt_failures++;
    }
}

int
main(void) {
    ok("TRUE");
    ok("NULL");
    ok("-12345");
    ok("{ }");
    ok("{ a 1, b TRUE }");
    ok("{\n    a 1,\n    b { c '00FF'H }\n}");
    ok("alt : { x 1 }");
    ok("alt : TRUE");
    ok("{ 1, 2, 3 }");
    ok("{ 42 }");
    ok("\"a string with , and { and -- inside\"");
    ok("'0110'B");
    ok("''H");
    ok("{ 2 23 143 1 }");
    ok("green -- (1) --");
    ok("{ a 1 -- note -- , b 2 }");
    ok("{ pick b : TRUE, n 1 }");
    ok("\"an embedded \"\" quote\"");

    bad("{ a 1,");         /* unclosed brace */
    bad("{ a 1 }}");       /* one close too many */
    bad("{ a 1,, b 2 }");  /* two commas */
    bad("{ a 1, }");       /* trailing comma */
    bad("{ , a 1 }");      /* leading comma */
    bad("\"unterminated");
    bad("'00FF");          /* unterminated hstring */
    bad("'00FF'");         /* missing the H or B suffix */
    bad("alt :");          /* nothing after the colon */
    bad("{ alt : }");      /* still nothing after the colon */
    bad("");               /* no content at all */
    bad("a 1, b 2");       /* comma outside braces */

    VNT_CASE("scalar extraction skips field names");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[200];
        int rc = vn_scan_scalars("{ a 1, b TRUE, c { d '0A'H, e \"x\" } }", vals,
                                 8, &n, err, sizeof err);
        VNT_TRUE(rc == 1);
        VNT_TRUE(n == 4);
        if(n == 4) {
            VNT_STREQ(vals[0], "1");
            VNT_STREQ(vals[1], "TRUE");
            VNT_STREQ(vals[2], "'0A'H");
            VNT_STREQ(vals[3], "\"x\""); /* cstrings keep their quotes */
        }
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("an enum identifier is a value, not a field name");
    {
        char *vals[4];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ col green }", vals, 4, &n, err,
                                 sizeof err) == 1);
        VNT_TRUE(n == 1);
        if(n == 1) VNT_STREQ(vals[0], "green");
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("a choice alternative name is not a value");
    {
        char *vals[4];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("pick : TRUE", vals, 4, &n, err,
                                 sizeof err) == 1);
        VNT_TRUE(n == 1);
        if(n == 1) VNT_STREQ(vals[0], "TRUE");
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("an oid arc list becomes one scalar");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ oid { 2 23 143 1 } }", vals, 8, &n, err,
                                 sizeof err) == 1);
        VNT_TRUE(n == 1);
        if(n == 1) VNT_STREQ(vals[0], "2 23 143 1");
        for(i = 0; i < n; i++) free(vals[i]);
    }

    /* A comma-separated integer list is a SEQUENCE OF, not an arc list. */
    VNT_CASE("a comma-separated integer list stays separate scalars");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ 1, 2, 3 }", vals, 8, &n, err,
                                 sizeof err) == 1);
        VNT_TRUE(n == 3);
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("a one-element integer list is one scalar, not an arc list");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ 42 }", vals, 8, &n, err, sizeof err) == 1);
        VNT_TRUE(n == 1);
        if(n == 1) VNT_STREQ(vals[0], "42");
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("comments contribute no scalars");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ a 1 -- b 2 -- }", vals, 8, &n, err,
                                 sizeof err) == 1);
        VNT_TRUE(n == 1);
        for(i = 0; i < n; i++) free(vals[i]);
    }

    VNT_CASE("running out of room is reported");
    {
        char *vals[2];
        size_t n = 0;
        char err[200];
        VNT_TRUE(vn_scan_scalars("{ 1, 2, 3, 4 }", vals, 2, &n, err,
                                 sizeof err) == 0);
        VNT_TRUE(err[0] != '\0');
    }

    return vnt_report("t_scan");
}
