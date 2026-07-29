/*
 * asn1vn.c -- read DER from stdin, decode it as the -DPDU root type, and write
 * ASN.1 value notation to stdout.
 *
 * Shaped like asn1c's converter-example, but with vn_encode as the output stage.
 * The root type arrives at compile time via -DPDU=<TypeName>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asn_application.h>
#include "vn_encoder.h"

#define VN_CAT_(a, b) a##b
#define VN_CAT(a, b) VN_CAT_(a, b)
#define VN_PDU_DEF VN_CAT(asn_DEF_, PDU)

extern asn_TYPE_descriptor_t VN_PDU_DEF;

static void
usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [-c|-a] [-l WIDTH] [-i WIDTH] [-L] [-S] < input.der\n"
            "\n"
            "  -c        canonical output: deterministic, for diffing\n"
            "  -a        annotated output: adds X.680 comments\n"
            "  -l WIDTH  wrap hex at WIDTH columns; 0 disables wrapping\n"
            "  -i WIDTH  indent width, default 4\n"
            "  -L        lenient: emit questionable values instead of failing\n"
            "  -S        strict: fail on a bare ANY rather than emitting hex\n",
            argv0);
}

int
main(int argc, char **argv) {
    vn_options_t opts;
    char reason[256] = "";
    unsigned char *buf;
    size_t cap = 1 << 16, len = 0, n;
    void *st = 0;
    asn_dec_rval_t rv;
    int i;

    memset(&opts, 0, sizeof opts);
    opts.mode = VN_MODE_PRETTY;
    opts.errbuf = reason;
    opts.errlen = sizeof reason;

    for(i = 1; i < argc; i++) {
        if(!strcmp(argv[i], "-c")) {
            opts.mode = VN_MODE_CANONICAL;
        } else if(!strcmp(argv[i], "-a")) {
            opts.mode = VN_MODE_ANNOTATED;
        } else if(!strcmp(argv[i], "-L")) {
            opts.flags |= VN_F_LENIENT;
        } else if(!strcmp(argv[i], "-S")) {
            opts.flags |= VN_F_STRICT_ANY;
        } else if(!strcmp(argv[i], "-l") && i + 1 < argc) {
            opts.line_width = atoi(argv[++i]);
            if(opts.line_width == 0) opts.line_width = -1; /* 0 means default */
        } else if(!strcmp(argv[i], "-i") && i + 1 < argc) {
            opts.indent_width = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    /* vn_options_t treats 0 as "use the default", so -l 0 arrives as -1 and is
     * translated to a width that disables wrapping. */
    if(opts.line_width < 0) opts.line_width = 1 << 24;

    buf = (unsigned char *)malloc(cap);
    if(!buf) {
        perror("malloc");
        return 2;
    }
    while((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if(len == cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, cap * 2);
            if(!nb) {
                perror("realloc");
                free(buf);
                return 2;
            }
            buf = nb;
            cap *= 2;
        }
    }
    if(ferror(stdin)) {
        perror("read");
        free(buf);
        return 2;
    }

    rv = asn_decode(0, ATS_BER, &VN_PDU_DEF, &st, buf, len);
    free(buf);
    if(rv.code != RC_OK) {
        fprintf(stderr, "%s: BER/DER decode failed (code %d) after %lu bytes\n",
                argv[0], (int)rv.code, (unsigned long)rv.consumed);
        if(st) ASN_STRUCT_FREE(VN_PDU_DEF, st);
        return 1;
    }

    if(vn_fprint(stdout, &VN_PDU_DEF, st, &opts) < 0) {
        fprintf(stderr, "%s: cannot render value notation: %s\n", argv[0],
                reason[0] ? reason : "unknown error");
        ASN_STRUCT_FREE(VN_PDU_DEF, st);
        return 1;
    }
    fputc('\n', stdout);

    ASN_STRUCT_FREE(VN_PDU_DEF, st);
    return 0;
}
