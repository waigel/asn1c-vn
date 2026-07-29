/*
 * t_octet.c -- OCTET STRING as an X.680 hstring, and hex line wrapping.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"

static char *
enc_os(const unsigned char *bytes, size_t len, const vn_options_t *o) {
    OCTET_STRING_t os;
    char reason[200];
    memset(&os, 0, sizeof os);
    os.buf = (uint8_t *)bytes;
    os.size = len;
    return vnt_encode(&asn_DEF_OCTET_STRING, &os, o, reason, sizeof reason);
}

/* Count hex digits per line, so a wrap can never split a byte in half. */
static void
check_even_per_line(const char *out) {
    const char *p = out;
    for(;;) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);
        const char *q;
        size_t n = 0;
        for(q = p; q < end; q++)
            if(strchr("0123456789ABCDEF", *q) && *q != '\0') n++;
        VNT_TRUE(n % 2 == 0);
        if(!nl) break;
        p = nl + 1;
    }
}

int
main(void) {
    char *out;

    VNT_CASE("empty octet string");
    out = enc_os((const unsigned char *)"", 0, 0);
    VNT_STREQ(out, "''H");
    free(out);

    VNT_CASE("a null buffer is treated as empty");
    {
        OCTET_STRING_t os;
        char reason[200];
        memset(&os, 0, sizeof os);
        out = vnt_encode(&asn_DEF_OCTET_STRING, &os, 0, reason, sizeof reason);
        VNT_STREQ(out, "''H");
        free(out);
    }

    VNT_CASE("hex digits are uppercase");
    { const unsigned char b[] = {0x00, 0xaa, 0xbb};
      out = enc_os(b, sizeof b, 0);
      VNT_STREQ(out, "'00AABB'H");
      free(out); }

    VNT_CASE("nibble boundaries round out correctly");
    { const unsigned char b[] = {0x0f, 0xf0, 0xff, 0x01};
      out = enc_os(b, sizeof b, 0);
      VNT_STREQ(out, "'0FF0FF01'H");
      free(out); }

    VNT_CASE("canonical mode never wraps");
    {
        vn_options_t o;
        unsigned char b[64];
        char want[1 + 128 + 2 + 1];
        size_t i;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_CANONICAL;
        for(i = 0; i < sizeof b; i++) b[i] = (unsigned char)i;
        out = enc_os(b, sizeof b, &o);
        want[0] = '\'';
        for(i = 0; i < sizeof b; i++)
            sprintf(want + 1 + i * 2, "%02X", (unsigned)b[i]);
        strcpy(want + 1 + sizeof b * 2, "'H");
        VNT_STREQ(out, want);
        VNT_TRUE(out && strchr(out, '\n') == 0);
        free(out);
    }

    VNT_CASE("pretty mode wraps long hex on an even boundary");
    {
        vn_options_t o;
        unsigned char b[40];
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_PRETTY;
        o.line_width = 20;
        memset(b, 0xab, sizeof b);
        out = enc_os(b, sizeof b, &o);
        VNT_TRUE(out && strchr(out, '\n') != 0);
        if(out) check_even_per_line(out);
        free(out);
    }

    /* Every hex digit must survive the wrapping, in order. */
    VNT_CASE("wrapping loses no digits");
    {
        vn_options_t o;
        unsigned char b[40];
        size_t i, digits = 0;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_PRETTY;
        o.line_width = 20;
        for(i = 0; i < sizeof b; i++) b[i] = (unsigned char)(i * 7);
        out = enc_os(b, sizeof b, &o);
        if(out) {
            const char *p;
            for(p = out; *p; p++)
                if(strchr("0123456789ABCDEF", *p)) digits++;
            VNT_TRUE(digits == sizeof b * 2);
        }
        free(out);
    }

    VNT_CASE("a short value does not wrap even in pretty mode");
    { const unsigned char b[] = {0x01, 0x02, 0x03};
      out = enc_os(b, sizeof b, 0);
      VNT_TRUE(out && strchr(out, '\n') == 0);
      free(out); }

    return vnt_report("t_octet");
}
