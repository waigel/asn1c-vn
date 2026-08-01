/*
 * t_vncorpus.c -- read value notation written by somebody else.
 *
 * Every other test in this suite feeds the reader text this codec produced, so
 * agreement proves only that the two halves share their assumptions. A corpus
 * from a foreign producer is the one thing that cannot: TCA ships its reference
 * ProfileElements as value notation, and those files were written against the
 * standard rather than against us.
 *
 * Two properties per file:
 *
 *   1. it reads at all;
 *   2. text -> DER and text -> structure -> our text -> DER agree byte for byte,
 *      which is what makes our rendering of a foreign value trustworthy.
 *
 * Files are named on the command line. The corpus is not in this repository --
 * it belongs to whoever published it -- so this runs from a directory you point
 * it at, in the way check-roundtrip takes DERDIR.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"

#ifdef VC_EXTERNAL
#include <constr_TYPE.h>
#else
#include "Nested.h"
#endif

#ifndef VC_PDU
#define VC_PDU Nested
#endif
#define VC_CAT_(a, b) a##b
#define VC_CAT(a, b) VC_CAT_(a, b)
#define asn_DEF_VC_PDU VC_CAT(asn_DEF_, VC_PDU)

#ifdef VC_EXTERNAL
extern asn_TYPE_descriptor_t asn_DEF_VC_PDU;
#endif

extern const vn_annotations_t vn_generated_annotations;

static int
collect(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

/*
 * A value assignment -- `name Type ::= value` -- is module syntax rather than
 * value syntax, so the codec does not know it; skipping the header is the
 * caller's job, exactly as in asn1vn.
 */
static size_t
skip_filler(const char *b, size_t len, size_t p) {
    for(;;) {
        while(p < len && (b[p] == ' ' || b[p] == '\t' || b[p] == '\n'
                          || b[p] == '\r'))
            p++;
        if(p + 1 < len && b[p] == '-' && b[p + 1] == '-') {
            p += 2;
            while(p < len) {
                if(b[p] == '\n') { p++; break; }
                if(p + 1 < len && b[p] == '-' && b[p + 1] == '-') { p += 2; break; }
                p++;
            }
            continue;
        }
        if(p + 1 < len && b[p] == '/' && b[p + 1] == '*') {
            int depth = 1;
            p += 2;
            while(p < len && depth) {
                if(p + 1 < len && b[p] == '/' && b[p + 1] == '*') { depth++; p += 2; }
                else if(p + 1 < len && b[p] == '*' && b[p + 1] == '/') { depth--; p += 2; }
                else p++;
            }
            continue;
        }
        return p;
    }
}

static size_t
take_ident(const char *b, size_t len, size_t p) {
    size_t s = p;
    while(p < len
          && ((b[p] >= 'a' && b[p] <= 'z') || (b[p] >= 'A' && b[p] <= 'Z')
              || (b[p] >= '0' && b[p] <= '9') || b[p] == '-' || b[p] == '_'))
        p++;
    return p > s ? p : s;
}

static size_t
skip_assignment_header(const char *b, size_t len, size_t p) {
    size_t q, at = skip_filler(b, len, p);

    q = take_ident(b, len, at);
    if(q == at) return p;
    at = skip_filler(b, len, q);
    q = take_ident(b, len, at);
    if(q == at) return p;
    at = skip_filler(b, len, q);
    if(at + 2 < len && b[at] == ':' && b[at + 1] == ':' && b[at + 2] == '=')
        return at + 3;
    return p;
}

static char *
slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long  size;

    if(!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0) { fclose(f); return 0; }
    buf = (char *)malloc((size_t)size + 1);
    if(!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);
    buf[size] = '\0';
    *len = (size_t)size;
    return buf;
}

/* Reads one file; returns 1 when both properties hold. */
static int
check_file(const char *path, const char *label) {
    size_t            len, off;
    char             *text = slurp(path, &len);
    vn_read_options_t ro;
    char              reason[400];
    int               ok = 1, values = 0;

    if(!text) {
        fprintf(stderr, "FAIL [%s]: cannot read the file\n", label);
        vnt_failures++;
        return 0;
    }
    memset(&ro, 0, sizeof ro);
    ro.flags = VN_RF_EOF;
    ro.annotations = &vn_generated_annotations;
    ro.errbuf = reason;
    ro.errlen = sizeof reason;

    for(off = 0;;) {
        void          *st = 0;
        vnt_str_t      der1, vn, der2;
        asn_dec_rval_t dv;

        off = skip_filler(text, len, off);
        if(off >= len) break;
        off = skip_assignment_header(text, len, off);
        off = skip_filler(text, len, off);
        if(off >= len) break;

        memset(&der1, 0, sizeof der1);
        memset(&vn, 0, sizeof vn);
        memset(&der2, 0, sizeof der2);
        reason[0] = '\0';

        dv = vn_decode(0, &asn_DEF_VC_PDU, &st, &ro, text + off, len - off);
        if(dv.code != RC_OK) {
            fprintf(stderr, "FAIL [%s]: %s\n", label,
                    reason[0] ? reason : "cannot read");
            vnt_failures++;
            if(st) ASN_STRUCT_FREE(asn_DEF_VC_PDU, st);
            ok = 0;
            break;
        }
        values++;

        /* Their text -> DER. */
        if(der_encode(&asn_DEF_VC_PDU, st, collect, &der1).encoded < 0) {
            fprintf(stderr, "FAIL [%s]: cannot encode as DER\n", label);
            vnt_failures++;
            ok = 0;
        } else {
            /* Their text -> our text -> DER, which must be the same DER. */
            vn_options_t eo;
            memset(&eo, 0, sizeof eo);
            eo.annotations = &vn_generated_annotations;
            if(vn_encode(&asn_DEF_VC_PDU, st, &eo, collect, &vn).encoded < 0) {
                fprintf(stderr, "FAIL [%s]: cannot render\n", label);
                vnt_failures++;
                ok = 0;
            } else {
                void *st2 = 0;
                reason[0] = '\0';
                if(vn_decode(0, &asn_DEF_VC_PDU, &st2, &ro, vn.buf, vn.len).code
                   != RC_OK) {
                    fprintf(stderr, "FAIL [%s]: cannot read back our own: %s\n",
                            label, reason);
                    vnt_failures++;
                    ok = 0;
                } else if(der_encode(&asn_DEF_VC_PDU, st2, collect, &der2)
                              .encoded
                          < 0) {
                    fprintf(stderr, "FAIL [%s]: cannot re-encode\n", label);
                    vnt_failures++;
                    ok = 0;
                } else if(der1.len != der2.len
                          || memcmp(der1.buf, der2.buf, der1.len) != 0) {
                    fprintf(stderr,
                            "FAIL [%s]: our rendering encodes differently "
                            "(%lu vs %lu bytes)\n",
                            label, (unsigned long)der1.len,
                            (unsigned long)der2.len);
                    vnt_failures++;
                    ok = 0;
                }
                if(st2) ASN_STRUCT_FREE(asn_DEF_VC_PDU, st2);
            }
        }

        ASN_STRUCT_FREE(asn_DEF_VC_PDU, st);
        free(der1.buf);
        free(der2.buf);
        free(vn.buf);
        if(!ok) break;
        off += dv.consumed;
    }

    if(ok && values == 0) {
        fprintf(stderr, "FAIL [%s]: no value found\n", label);
        vnt_failures++;
        ok = 0;
    }
    free(text);
    return ok;
}

int
main(int argc, char **argv) {
    int i, good = 0;

    if(argc < 2) {
        fprintf(stderr, "usage: %s <file.asn1> [more...]\n", argv[0]);
        return 2;
    }
    for(i = 1; i < argc; i++) {
        const char *base = strrchr(argv[i], '/');
        base = base ? base + 1 : argv[i];
        VNT_CASE(base);
        if(check_file(argv[i], base)) good++;
    }
    printf("t_vncorpus: %d of %d file(s) read and re-encoded identically\n",
           good, argc - 1);
    return vnt_report("t_vncorpus");
}
