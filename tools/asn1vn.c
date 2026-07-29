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
    size_t cap = 1 << 16, len = 0, n, offset, count = 0;
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

    /*
     * Decode every PDU in the input, not just the first.
     *
     * A single DER value is the common case, but some formats concatenate them:
     * an SGP.22 profile package is a sequence of ProfileElement TLVs one after
     * another, so stopping after one would silently ignore almost the whole
     * file. Each value is emitted in turn; with more than one, the output is a
     * sequence of X.680 values rather than a single value.
     */
    for(offset = 0; offset < len; count++) {
        st = 0;
        rv = asn_decode(0, ATS_BER, &VN_PDU_DEF, &st, buf + offset,
                        len - offset);
        if(rv.code != RC_OK) {
            fprintf(stderr,
                    "%s: BER/DER decode failed (code %d) at byte %lu of %lu",
                    argv[0], (int)rv.code, (unsigned long)(offset + rv.consumed),
                    (unsigned long)len);
            if(count) fprintf(stderr, ", after %lu value(s)", (unsigned long)count);
            fputc('\n', stderr);
            if(st) ASN_STRUCT_FREE(VN_PDU_DEF, st);
            free(buf);
            return 1;
        }
        if(rv.consumed == 0) { /* no progress: refuse to loop forever */
            fprintf(stderr, "%s: decoder consumed no input at byte %lu\n",
                    argv[0], (unsigned long)offset);
            if(st) ASN_STRUCT_FREE(VN_PDU_DEF, st);
            free(buf);
            return 1;
        }

        if(count && fputc('\n', stdout) == EOF) {
            perror("write");
            ASN_STRUCT_FREE(VN_PDU_DEF, st);
            free(buf);
            return 1;
        }
        if(opts.mode == VN_MODE_ANNOTATED
           && printf("-- value %lu at byte %lu --\n", (unsigned long)count + 1,
                     (unsigned long)offset)
                  < 0) {
            perror("write");
            ASN_STRUCT_FREE(VN_PDU_DEF, st);
            free(buf);
            return 1;
        }
        if(vn_fprint(stdout, &VN_PDU_DEF, st, &opts) < 0) {
            fprintf(stderr, "%s: cannot render value notation: %s\n", argv[0],
                    reason[0] ? reason : "unknown error");
            ASN_STRUCT_FREE(VN_PDU_DEF, st);
            free(buf);
            return 1;
        }
        fputc('\n', stdout);

        ASN_STRUCT_FREE(VN_PDU_DEF, st);
        offset += rv.consumed;
    }

    free(buf);
    if(count == 0) {
        fprintf(stderr, "%s: input is empty\n", argv[0]);
        return 1;
    }
    if(fflush(stdout) != 0) {
        perror("write");
        return 1;
    }
    return 0;
}
