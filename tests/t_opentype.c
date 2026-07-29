/*
 * t_opentype.c -- bare ANY versus a table-constrained open type.
 *
 * A bare ANY carries no type information at runtime, so hex is the only
 * possible rendering and is this encoder's single deliberate departure from
 * X.680. A table-constrained open type is not affected: asn1c resolves it to a
 * real descriptor, so it renders as ordinary value notation.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Msg.h"
#include "Opaque.h"

int
main(void) {
    char reason[240], *out;

    VNT_CASE("bare ANY is emitted as hex");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x04, 0x03, 0x01, 0x02, 0x03};
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        out = vnt_encode(&asn_DEF_Opaque, &o, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "'0403010203'H") != 0);
        free(out);
    }

    VNT_CASE("an empty bare ANY is still valid output");
    {
        Opaque_t o;
        memset(&o, 0, sizeof o);
        o.id = 1;
        out = vnt_encode(&asn_DEF_Opaque, &o, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "''H") != 0);
        free(out);
    }

    VNT_CASE("bare ANY fails under VN_F_STRICT_ANY");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x05, 0x00};
        vn_options_t opt;
        memset(&opt, 0, sizeof opt);
        opt.flags = VN_F_STRICT_ANY;
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Opaque, &o, &opt, reason,
                                  sizeof reason));
        VNT_TRUE(strstr(reason, "ANY") != 0);
    }

    VNT_CASE("annotated mode marks bare ANY as not being value notation");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x05, 0x00};
        vn_options_t opt;
        memset(&opt, 0, sizeof opt);
        opt.mode = VN_MODE_ANNOTATED;
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        out = vnt_encode(&asn_DEF_Opaque, &o, &opt, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "ANY") != 0);
        free(out);
    }

    VNT_CASE("open type renders its resolved alternative as a string");
    {
        Msg_t m;
        memset(&m, 0, sizeof m);
        m.id = 1;
        m.body.present = Msg__body_PR_Text;
        m.body.choice.Text.buf = (uint8_t *)"hi";
        m.body.choice.Text.size = 2;
        out = vnt_encode(&asn_DEF_Msg, &m, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "\"hi\"") != 0);
        /* Real value notation, not a hex fallback. */
        VNT_TRUE(out && strchr(out, '\'') == 0);
        VNT_TRUE(out && strstr(out, "Text :") != 0);
        free(out);
    }

    VNT_CASE("open type renders a boolean alternative");
    {
        Msg_t m;
        memset(&m, 0, sizeof m);
        m.id = 2;
        m.body.present = Msg__body_PR_Flag;
        m.body.choice.Flag = 1;
        out = vnt_encode(&asn_DEF_Msg, &m, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "TRUE") != 0);
        free(out);
    }

    VNT_CASE("an unresolved open type fails rather than emitting hex");
    {
        Msg_t m;
        memset(&m, 0, sizeof m);
        m.id = 1;
        m.body.present = Msg__body_PR_NOTHING;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Msg, &m, 0, reason, sizeof reason));
    }

    return vnt_report("t_opentype");
}
