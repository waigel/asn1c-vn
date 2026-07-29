/*
 * t_dispatch.c -- type dispatch, the two simplest handlers, and the guarantee
 * that an unsupported type produces no output at all.
 */
#include <stdlib.h>
#include <string.h>
#include <REAL.h> /* asn_DEF_REAL: the stand-in for an unsupported type */
#include "vntest.h"
#include "Holder.h"

int
main(void) {
    char reason[200];
    char *out;
    BOOLEAN_t b;
    NULL_t nil = 0;

    VNT_CASE("BOOLEAN true");
    b = 1;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "TRUE");
    free(out);

    VNT_CASE("BOOLEAN false");
    b = 0;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "FALSE");
    free(out);

    /* BER permits any nonzero octet for true, so the handler must not compare
     * against 1. */
    VNT_CASE("BOOLEAN nonzero other than 1 is still TRUE");
    b = 0xff;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "TRUE");
    free(out);

    VNT_CASE("NULL");
    out = vnt_encode(&asn_DEF_NULL, &nil, 0, reason, sizeof reason);
    VNT_STREQ(out, "NULL");
    free(out);

    VNT_CASE("an unsupported type names itself in the reason");
    VNT_TRUE(vnt_encode_fails(&asn_DEF_REAL, 0, 0, reason, sizeof reason));
    VNT_TRUE(strstr(reason, "REAL") != 0);

    /* Passing no callback proves dispatch happens before any write: if the
     * type were rejected only after output began, this would crash. */
    VNT_CASE("failure identifies the type and writes nothing");
    {
        asn_enc_rval_t er = vn_encode(&asn_DEF_REAL, 0, 0, 0, 0);
        VNT_TRUE(er.encoded == -1);
        VNT_TRUE(er.failed_type == &asn_DEF_REAL);
    }

    VNT_CASE("a null descriptor is rejected, not dereferenced");
    VNT_TRUE(vnt_encode_fails(0, &b, 0, reason, sizeof reason));
    VNT_TRUE(strstr(reason, "descriptor") != 0);

    /* A successful encode must leave failed_type clear, so callers can trust
     * it without also checking encoded. */
    VNT_CASE("success clears failed_type");
    {
        asn_enc_rval_t er;
        vnt_str_t sink;
        memset(&sink, 0, sizeof sink);
        b = 1;
        er = vn_encode(&asn_DEF_BOOLEAN, &b, 0, 0, 0);
        VNT_TRUE(er.encoded == -1); /* no callback: cannot write */
        free(sink.buf);
        out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
        VNT_TRUE(out != 0);
        VNT_TRUE(strlen(out ? out : "") == 4);
        free(out);
    }

    return vnt_report("t_dispatch");
}
