/*
 * t_golden.c -- pins the exact output of all three modes.
 *
 * A missing golden file is written out and still counted as a failure. A golden
 * file no human has checked against X.680 is worthless: it would freeze whatever
 * the encoder happened to produce and call it correct.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vnscan.h"
#include "Nested.h"

/* Static storage keeps the value construction free of allocation bookkeeping. */
static const Nested_t *
make_nested(void) {
    static Nested_t n;
    static const unsigned char name[] = {0xde, 0xad, 0xbe, 0xef};
    static const unsigned char tag[] = {0x01, 0x02};
    static long opt = 42;

    memset(&n, 0, sizeof n);
    n.name.buf = (uint8_t *)name;
    n.name.size = sizeof name;
    n.inner.id = 5;
    n.inner.tag.buf = (uint8_t *)tag;
    n.inner.tag.size = sizeof tag;
    n.col = 2;
    n.opt = &opt;
    return &n;
}

static void
check_mode(const char *label, vn_mode_e mode, const char *path,
           const asn_TYPE_descriptor_t *td, const void *sptr) {
    vn_options_t o;
    char reason[240], err[240];
    char *out, *want;
    long len;
    FILE *f;

    VNT_CASE(label);
    memset(&o, 0, sizeof o);
    o.mode = mode;
    out = vnt_encode(td, sptr, &o, reason, sizeof reason);
    if(!out) {
        fprintf(stderr, "FAIL [%s]: encode failed: %s\n", label, reason);
        vnt_failures++;
        return;
    }

    /* Whatever the mode, the output must be well-formed value notation. */
    if(!vn_scan_wellformed(out, err, sizeof err)) {
        fprintf(stderr, "FAIL [%s]: output is malformed: %s\n%s\n", label, err,
                out);
        vnt_failures++;
    }

    f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr,
                "NOTE [%s]: writing new golden file %s -- review it against "
                "X.680 before trusting it\n",
                label, path);
        f = fopen(path, "wb");
        if(f) {
            fwrite(out, 1, strlen(out), f);
            fclose(f);
        }
        free(out);
        vnt_failures++; /* a self-written golden must never count as a pass */
        return;
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    want = (char *)malloc((size_t)len + 1);
    if(want) {
        size_t got = fread(want, 1, (size_t)len, f);
        want[got] = '\0';
    }
    fclose(f);

    VNT_STREQ(out, want);
    free(out);
    free(want);
}

int
main(void) {
    const Nested_t *n = make_nested();

    check_mode("nested pretty", VN_MODE_PRETTY,
               "tests/golden/nested.pretty.vn", &asn_DEF_Nested, n);
    check_mode("nested canonical", VN_MODE_CANONICAL,
               "tests/golden/nested.canonical.vn", &asn_DEF_Nested, n);
    check_mode("nested annotated", VN_MODE_ANNOTATED,
               "tests/golden/nested.annotated.vn", &asn_DEF_Nested, n);

    return vnt_report("t_golden");
}
