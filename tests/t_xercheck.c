/*
 * t_xercheck.c -- the semantic oracle.
 *
 * asn1c's XER encoder and this one walk the same structure in descriptor order,
 * so the ordered sequence of scalar values must agree. asn1c's XER shares no code
 * with this encoder, which is what makes it an independent check, and
 * asn_random_fill supplies the values so no fixtures have to be written.
 *
 * What this catches: wrong values, wrong order, missing and extra members.
 * What it does not: the choice between equivalent lexical forms, because XER text
 * carries no type (see xerscan.h). Structure is covered by the well-formedness
 * scan below, and exact forms by the golden files and per-type tests.
 */
#include <stdlib.h>
#include <string.h>
#include <asn_random_fill.h>
#include "vntest.h"
#include "vnscan.h"
#include "xerscan.h"
#include "Top.h"

#define MAX_SCALARS 512
#define MAX_CANDS 8

/*
 * Outcome of one comparison.
 *
 * asn_random_fill in asn1c 0.9.29 happily produces values asn1c's own XER
 * encoder rejects: a CHOICE with present = 0, and OBJECT IDENTIFIERs with fewer
 * than two arcs. Those are not usable test vectors, but they are not wasted
 * either -- the two encoders should agree on whether a value is encodable at
 * all, so a disagreement about validity is itself a finding.
 */
typedef enum {
    CMP_BOTH_OK,      /* both encoded, and the scalars agree */
    CMP_BOTH_REFUSED, /* neither could encode it: agreement */
    CMP_MISMATCH      /* a real disagreement */
} cmp_result_e;

static int
xer_consume(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

/*
 * An empty-bodied value cannot be aligned between the two dialects, so both
 * sides drop them before comparing.
 *
 * XER writes an empty SEQUENCE OF, an empty string, an empty OCTET STRING and a
 * NULL all as an empty element, and nothing in the text distinguishes them.
 * Value notation, by contrast, writes `{ }`, `""`, `''H` and `NULL` -- the empty
 * list yielding no scalar at all while the others yield one. Keeping them would
 * make the scalar counts disagree for reasons that have nothing to do with
 * correctness.
 *
 * What this gives up: this layer no longer checks the *position* of empty values
 * or of NULL. Both are pinned instead by t_sequence, t_collection, t_octet,
 * t_strings and the golden files, which assert exact output.
 */
/*
 * The two dialects spell "empty" differently, and the predicates must not be
 * conflated. On the XER side only genuinely empty content is empty; the text
 * `""` there is a string whose value is two quote characters. On the value
 * notation side `""` is the empty cstring, because the scanner keeps the quotes.
 */
static int
is_empty_vn(const char *s) {
    if(!s || !*s) return 1;
    if(!strcmp(s, "\"\"")) return 1; /* the empty cstring */
    if(!strcmp(s, "NULL")) return 1;
    if(!strcmp(s, "''H")) return 1;  /* the empty OCTET STRING */
    if(!strcmp(s, "''B")) return 1;  /* the empty BIT STRING */
    return 0;
}

static int
is_empty_xer(const char *s) {
    return !s || !*s;
}

static size_t
drop_empty(char **v, size_t n, int (*is_empty)(const char *)) {
    size_t i, keep = 0;
    for(i = 0; i < n; i++) {
        if(is_empty(v[i])) {
            free(v[i]);
            continue;
        }
        v[keep++] = v[i];
    }
    return keep;
}

static int
scalars_agree(const char *vn_scalar, const char *xer_scalar) {
    char *want = vn_norm_scalar(vn_scalar);
    char *cands[MAX_CANDS];
    size_t ncands, i;
    int agree = 0;

    if(!want) return 0;
    ncands = xer_norm_candidates(xer_scalar, cands, MAX_CANDS);
    for(i = 0; i < ncands; i++)
        if(!strcmp(cands[i], want)) agree = 1;
    for(i = 0; i < ncands; i++) free(cands[i]);
    free(want);
    return agree && ncands > 0;
}

static cmp_result_e
compare_one(const void *sptr, int iteration) {
    vnt_str_t xer;
    char *vn = 0;
    char *xs[MAX_SCALARS], *vs[MAX_SCALARS];
    size_t xn = 0, vcount = 0, i;
    char reason[256], err[256];
    int xer_ok, ok = 1;

    memset(&xer, 0, sizeof xer);
    xer_ok = xer_encode(&asn_DEF_Top, sptr, XER_F_BASIC, xer_consume, &xer)
                 .encoded
             >= 0;
    vn = vnt_encode(&asn_DEF_Top, sptr, 0, reason, sizeof reason);

    if(!xer_ok) {
        /* asn1c cannot encode this value, so it is no basis for comparison.
         * Both encoders should agree it is unencodable. */
        cmp_result_e r = CMP_BOTH_REFUSED;
        if(vn) {
            fprintf(stderr,
                    "FAIL iter %d: asn1c's XER refused this value but ours "
                    "accepted it:\n%s\n",
                    iteration, vn);
            vnt_failures++;
            r = CMP_MISMATCH;
        }
        free(xer.buf);
        free(vn);
        return r;
    }

    if(!vn) {
        fprintf(stderr,
                "FAIL iter %d: asn1c's XER encoded this value but ours failed: "
                "%s\n--- XER ---\n%s\n",
                iteration, reason, xer.buf ? xer.buf : "");
        vnt_failures++;
        free(xer.buf);
        return CMP_MISMATCH;
    }

    if(!vn_scan_wellformed(vn, err, sizeof err)) {
        fprintf(stderr, "FAIL iter %d: value notation is malformed: %s\n%s\n",
                iteration, err, vn);
        vnt_failures++;
        ok = 0;
    }

    if(!xer_scan_scalars(xer.buf ? xer.buf : "", xs, MAX_SCALARS, &xn, err,
                         sizeof err)) {
        fprintf(stderr, "FAIL iter %d: XER scan failed: %s\n", iteration, err);
        vnt_failures++;
        ok = 0;
        xn = 0;
    } else if(!vn_scan_scalars(vn, vs, MAX_SCALARS, &vcount, err, sizeof err)) {
        fprintf(stderr, "FAIL iter %d: VN scan failed: %s\n", iteration, err);
        vnt_failures++;
        ok = 0;
        vcount = 0;
    } else if((xn = drop_empty(xs, xn, is_empty_xer)),
              (vcount = drop_empty(vs, vcount, is_empty_vn)), xn != vcount) {
        fprintf(stderr,
                "FAIL iter %d: %u XER scalars but %u VN scalars\n"
                "--- XER ---\n%s\n--- VN ---\n%s\n",
                iteration, (unsigned)xn, (unsigned)vcount, xer.buf, vn);
        vnt_failures++;
        ok = 0;
    } else {
        for(i = 0; i < xn; i++) {
            if(!scalars_agree(vs[i], xs[i])) {
                fprintf(stderr,
                        "FAIL iter %d: scalar %u differs: XER |%s| vs VN |%s|\n",
                        iteration, (unsigned)i, xs[i], vs[i]);
                vnt_failures++;
                ok = 0;
            }
        }
    }

    for(i = 0; i < xn; i++) free(xs[i]);
    for(i = 0; i < vcount; i++) free(vs[i]);
    free(xer.buf);
    free(vn);
    return ok ? CMP_BOTH_OK : CMP_MISMATCH;
}

int
main(int argc, char **argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 400;
    int i, built = 0, compared = 0, refused = 0;

    VNT_CASE("random values agree with asn1c's XER");
    for(i = 0; i < rounds; i++) {
        void *st = 0;
        /* Vary the budget so both tiny and deeply nested values occur. */
        size_t budget = 16 + (size_t)(i % 112);
        if(asn_random_fill(&asn_DEF_Top, &st, budget) != ARFILL_OK) continue;
        built++;
        switch(compare_one(st, i)) {
        case CMP_BOTH_OK:      compared++; break;
        case CMP_BOTH_REFUSED: refused++;  break;
        case CMP_MISMATCH:                 break;
        }
        ASN_STRUCT_FREE(asn_DEF_Top, st);
    }

    printf("t_xercheck: %d built, %d compared, %d refused by both\n", built,
           compared, refused);

    /* Guard against a vacuous pass: if nothing was ever actually compared, the
     * suite would be green while testing nothing. */
    VNT_CASE("enough values were genuinely compared");
    VNT_TRUE(compared >= rounds / 10);
    if(compared < rounds / 10)
        fprintf(stderr,
                "only %d of %d rounds reached a real comparison; "
                "asn_random_fill may have regressed\n",
                compared, rounds);

    return vnt_report("t_xercheck");
}
