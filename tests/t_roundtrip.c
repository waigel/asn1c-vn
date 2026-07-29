/*
 * t_roundtrip.c -- the acceptance criterion for the codec.
 *
 * DER -> value notation -> DER must be byte-identical. Unlike a comparison
 * against a foreign reference, this needs no external file and no assumption
 * about which schema version produced it.
 *
 * Run with DER file arguments to exercise real data; with none it uses the
 * built-in cases.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"

/*
 * The built-in cases need the generated struct types; a run against another
 * schema needs only the descriptor, since everything else goes through void *.
 */
#ifdef RT_EXTERNAL
#include <constr_TYPE.h>
#else
#include "Nested.h"
#include "Wrapper.h"
#include "Numbers.h"
#include "Choice.h"
#endif

/* The root type for file-driven runs, chosen at compile time. */
#ifndef RT_PDU
#define RT_PDU Nested
#endif
#define RT_CAT_(a, b) a##b
#define RT_CAT(a, b) RT_CAT_(a, b)
#define asn_DEF_RT_PDU RT_CAT(asn_DEF_, RT_PDU)

#ifdef RT_EXTERNAL
extern asn_TYPE_descriptor_t asn_DEF_RT_PDU;
#endif

/* --- helpers -------------------------------------------------------------- */

static int
collect(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

/*
 * Encode to VN, read it back, re-encode to DER and compare with the DER we
 * started from. Returns 1 on success.
 */
static int
roundtrip(const asn_TYPE_descriptor_t *td, const void *original_der,
          size_t der_len, const char *label) {
    void            *st = 0;
    vnt_str_t        vn, der2;
    vn_read_options_t ro;
    char             reason[400];
    asn_dec_rval_t   dv;
    asn_enc_rval_t   ev;
    int              ok = 0;

    memset(&vn, 0, sizeof vn);
    memset(&der2, 0, sizeof der2);
    memset(&ro, 0, sizeof ro);
    ro.errbuf = reason;
    ro.errlen = sizeof reason;
    ro.flags = VN_RF_EOF;
    reason[0] = '\0';

    /* DER -> structure -> VN */
    dv = ber_decode(0, td, &st, original_der, der_len);
    if(dv.code != RC_OK) {
        fprintf(stderr, "FAIL [%s]: input DER does not decode\n", label);
        goto done;
    }
    ev = vn_encode(td, st, 0, collect, &vn);
    if(ev.encoded < 0) {
        fprintf(stderr, "FAIL [%s]: cannot render value notation\n", label);
        goto done;
    }
    ASN_STRUCT_FREE(*td, st);
    st = 0;

    /* VN -> structure */
    dv = vn_decode(0, td, &st, &ro, vn.buf, vn.len);
    if(dv.code != RC_OK) {
        fprintf(stderr, "FAIL [%s]: cannot read back: %s\n--- VN ---\n%s\n",
                label, reason[0] ? reason : "(no reason)", vn.buf ? vn.buf : "");
        goto done;
    }
    if(dv.consumed != vn.len) {
        fprintf(stderr,
                "FAIL [%s]: consumed %lu of %lu bytes; trailing content?\n",
                label, (unsigned long)dv.consumed, (unsigned long)vn.len);
        goto done;
    }

    /* structure -> DER */
    ev = der_encode(td, st, collect, &der2);
    if(ev.encoded < 0) {
        fprintf(stderr, "FAIL [%s]: cannot re-encode as DER\n", label);
        goto done;
    }

    if(der2.len != der_len || memcmp(der2.buf, original_der, der_len) != 0) {
        size_t i, upto = der2.len < der_len ? der2.len : der_len;
        fprintf(stderr, "FAIL [%s]: DER differs (%lu vs %lu bytes)\n", label,
                (unsigned long)der_len, (unsigned long)der2.len);
        for(i = 0; i < upto; i++)
            if(((const unsigned char *)original_der)[i]
               != (unsigned char)der2.buf[i]) {
                fprintf(stderr, "  first difference at byte %lu: %02X vs %02X\n",
                        (unsigned long)i,
                        ((const unsigned char *)original_der)[i],
                        (unsigned char)der2.buf[i]);
                break;
            }
        fprintf(stderr, "--- VN ---\n%s\n", vn.buf ? vn.buf : "");
        goto done;
    }

    ok = 1;

done:
    if(!ok) vnt_failures++;
    if(st) ASN_STRUCT_FREE(*td, st);
    free(vn.buf);
    free(der2.buf);
    return ok;
}

/* Build a value, DER-encode it, then round-trip that DER.
 * Only the built-in cases construct values; a run against another schema drives
 * everything from files. */
#ifndef RT_EXTERNAL
static void
roundtrip_value(const asn_TYPE_descriptor_t *td, const void *value,
                const char *label) {
    vnt_str_t der;
    asn_enc_rval_t ev;

    VNT_CASE(label);
    memset(&der, 0, sizeof der);
    ev = der_encode(td, value, collect, &der);
    if(ev.encoded < 0) {
        fprintf(stderr, "FAIL [%s]: cannot DER-encode the fixture\n", label);
        vnt_failures++;
        free(der.buf);
        return;
    }
    roundtrip(td, der.buf, der.len, label);
    free(der.buf);
}
#endif /* !RT_EXTERNAL */

/* --- built-in cases ------------------------------------------------------- */

#ifndef RT_EXTERNAL
static void
builtin_cases(void) {
    {
        Nested_t n;
        const unsigned char name[] = {0xde, 0xad, 0xbe, 0xef};
        const unsigned char tag[] = {0x01, 0x02};
        long opt = 42;
        memset(&n, 0, sizeof n);
        n.name.buf = (uint8_t *)name;
        n.name.size = sizeof name;
        n.inner.id = 5;
        n.inner.tag.buf = (uint8_t *)tag;
        n.inner.tag.size = sizeof tag;
        n.col = 2;
        n.opt = &opt;
        roundtrip_value(&asn_DEF_Nested, &n, "nested sequence");
    }

    {
        /* No optional members: exercises omission and default handling. */
        Nested_t n;
        const unsigned char tag[] = {0x07};
        memset(&n, 0, sizeof n);
        n.inner.id = -1;
        n.inner.tag.buf = (uint8_t *)tag;
        n.inner.tag.size = sizeof tag;
        n.col = 0;
        roundtrip_value(&asn_DEF_Nested, &n, "nested with absent optionals");
    }

    {
        Numbers_t list;
        long a = 1, b = -2, c = 32768;
        long *items[3];
        memset(&list, 0, sizeof list);
        items[0] = &a;
        items[1] = &b;
        items[2] = &c;
        list.list.array = items;
        list.list.count = 3;
        list.list.size = 3;
        roundtrip_value(&asn_DEF_Numbers, &list, "list of integers");
    }

    {
        Numbers_t empty;
        memset(&empty, 0, sizeof empty);
        roundtrip_value(&asn_DEF_Numbers, &empty, "empty list");
    }

    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_flag;
        c.choice.flag = 1;
        roundtrip_value(&asn_DEF_Choice, &c, "choice of a boolean");
    }

    {
        Choice_t c;
        const unsigned char tag[] = {0xff, 0x00};
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_inner;
        c.choice.inner.id = 127;
        c.choice.inner.tag.buf = (uint8_t *)tag;
        c.choice.inner.tag.size = sizeof tag;
        roundtrip_value(&asn_DEF_Choice, &c, "choice of a sequence");
    }

    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_nothing;
        roundtrip_value(&asn_DEF_Choice, &c, "choice of NULL");
    }
}

#endif /* !RT_EXTERNAL */

#ifndef RT_EXTERNAL
/*
 * Restartability: presenting the same text one byte at a time must reach the
 * same result as presenting it whole. This is the only thing that exercises the
 * RC_WMORE paths at all.
 */
static void
incremental_case(void) {
    Nested_t             n;
    const unsigned char  tag[] = {0x01};
    vnt_str_t            vn;
    void                *whole = 0, *piecewise = 0;
    vn_read_options_t    ro;
    char                 reason[400];
    size_t               fed;
    int                  guard;

    VNT_CASE("one byte at a time reaches the same structure");
    memset(&n, 0, sizeof n);
    memset(&vn, 0, sizeof vn);
    memset(&ro, 0, sizeof ro);
    ro.errbuf = reason;
    ro.errlen = sizeof reason;
    ro.flags = VN_RF_EOF;
    n.inner.id = 9;
    n.inner.tag.buf = (uint8_t *)tag;
    n.inner.tag.size = sizeof tag;
    n.col = 1;

    if(vn_encode(&asn_DEF_Nested, &n, 0, collect, &vn).encoded < 0) {
        fprintf(stderr, "FAIL: cannot render the fixture\n");
        vnt_failures++;
        free(vn.buf);
        return;
    }

    ro.flags = VN_RF_EOF;
    if(vn_decode(0, &asn_DEF_Nested, &whole, &ro, vn.buf, vn.len).code != RC_OK) {
        fprintf(stderr, "FAIL: whole-buffer read failed: %s\n", reason);
        vnt_failures++;
        goto out;
    }

    /*
     * Grow the visible prefix one byte at a time. On RC_WMORE the decoder tells
     * us where to resume; a primitive asks to be re-presented from its start, so
     * the offset can move backwards relative to the parse position.
     */
    fed = 0;
    for(guard = 0; guard < 100000; guard++) {
        asn_dec_rval_t dv;
        if(fed < vn.len) fed++;
        /* Only the final presentation may claim to be the end of the input;
         * that is exactly what VN_RF_EOF means. */
        ro.flags = (fed >= vn.len) ? VN_RF_EOF : 0u;
        dv = vn_decode(0, &asn_DEF_Nested, &piecewise, &ro, vn.buf, fed);
        if(dv.code == RC_OK) break;
        if(dv.code == RC_FAIL) {
            fprintf(stderr, "FAIL: incremental read failed at %lu bytes: %s\n",
                    (unsigned long)fed, reason);
            vnt_failures++;
            goto out;
        }
        if(fed >= vn.len) {
            fprintf(stderr, "FAIL: still wants more input with all %lu bytes\n",
                    (unsigned long)vn.len);
            vnt_failures++;
            goto out;
        }
        /* Not finished: discard and retry with more, which is what a caller
         * that cannot keep partial state would do. */
        if(piecewise) {
            ASN_STRUCT_FREE(asn_DEF_Nested, piecewise);
            piecewise = 0;
        }
    }

    VNT_TRUE(piecewise != 0);
    if(piecewise && whole)
        VNT_TRUE(asn_DEF_Nested.op->compare_struct(&asn_DEF_Nested, whole,
                                                   piecewise)
                 == 0);

out:
    if(whole) ASN_STRUCT_FREE(asn_DEF_Nested, whole);
    if(piecewise) ASN_STRUCT_FREE(asn_DEF_Nested, piecewise);
    free(vn.buf);
}

#endif /* !RT_EXTERNAL */

/* --- real files ----------------------------------------------------------- */

static int
roundtrip_file(const char *path, int *values) {
    FILE          *f = fopen(path, "rb");
    unsigned char *buf;
    long           size;
    size_t         offset;
    int            ok = 1;

    if(!f) {
        fprintf(stderr, "FAIL: cannot open %s\n", path);
        vnt_failures++;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)(size > 0 ? size : 1));
    if(!buf || size <= 0 || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        fprintf(stderr, "FAIL: cannot read %s\n", path);
        vnt_failures++;
        return 0;
    }
    fclose(f);

    for(offset = 0; offset < (size_t)size;) {
        void          *st = 0;
        asn_dec_rval_t dv =
            asn_decode(0, ATS_BER, &asn_DEF_RT_PDU, &st, buf + offset,
                       (size_t)size - offset);
        char label[64];
        if(dv.code != RC_OK || dv.consumed == 0) {
            fprintf(stderr, "FAIL: %s: decode failed at byte %lu\n", path,
                    (unsigned long)offset);
            vnt_failures++;
            if(st) ASN_STRUCT_FREE(asn_DEF_RT_PDU, st);
            ok = 0;
            break;
        }
        ASN_STRUCT_FREE(asn_DEF_RT_PDU, st);
        snprintf(label, sizeof label, "value at byte %lu",
                 (unsigned long)offset);
        VNT_CASE(label);
        if(!roundtrip(&asn_DEF_RT_PDU, buf + offset, dv.consumed, label)) ok = 0;
        (*values)++;
        offset += dv.consumed;
    }

    free(buf);
    return ok;
}

int
main(int argc, char **argv) {
    if(argc > 1) {
        int i, values = 0;
        for(i = 1; i < argc; i++) roundtrip_file(argv[i], &values);
        printf("t_roundtrip: %d value(s) from %d file(s)\n", values, argc - 1);
        VNT_CASE("the files yielded values");
        VNT_TRUE(values > 0);
        return vnt_report("t_roundtrip(files)");
    }

#ifdef RT_EXTERNAL
    fprintf(stderr, "usage: %s <file.der> [more...]\n", argv[0]);
    return 2;
#else
    builtin_cases();
    incremental_case();
    return vnt_report("t_roundtrip");
#endif
}
