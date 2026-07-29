/*
 * t_sequence.c -- SEQUENCE and SET: member iteration, omission of absent
 * OPTIONAL members, and comma placement.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Pair.h"
#include "Nested.h"
#include "Empty.h"

int
main(void) {
    char reason[200], *out;

    VNT_CASE("empty sequence");
    {
        Empty_t e;
        memset(&e, 0, sizeof e);
        out = vnt_encode(&asn_DEF_Empty, &e, 0, reason, sizeof reason);
        VNT_STREQ(out, "{ }");
        free(out);
    }

    VNT_CASE("absent OPTIONAL member is omitted, leaving no stray comma");
    {
        Pair_t p;
        memset(&p, 0, sizeof p);
        p.first = 3;
        p.second = 0;
        out = vnt_encode(&asn_DEF_Pair, &p, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    first 3\n}");
        free(out);
    }

    VNT_CASE("present OPTIONAL member is emitted with a separating comma");
    {
        Pair_t p;
        BOOLEAN_t b = 1;
        memset(&p, 0, sizeof p);
        p.first = 3;
        p.second = &b;
        out = vnt_encode(&asn_DEF_Pair, &p, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    first 3,\n    second TRUE\n}");
        free(out);
    }

    VNT_CASE("nesting indents, and scalars stay on their field's line");
    {
        Nested_t n;
        const unsigned char name[] = {0xde, 0xad};
        const unsigned char tag[] = {0x01};
        memset(&n, 0, sizeof n);
        n.name.buf = (uint8_t *)name;
        n.name.size = sizeof name;
        n.inner.id = 5;
        n.inner.tag.buf = (uint8_t *)tag;
        n.inner.tag.size = sizeof tag;
        n.col = 1;
        out = vnt_encode(&asn_DEF_Nested, &n, 0, reason, sizeof reason);
        VNT_STREQ(out,
                  "{\n"
                  "    name 'DEAD'H,\n"
                  "    inner {\n"
                  "        id 5,\n"
                  "        tag '01'H\n"
                  "    },\n"
                  "    col green\n"
                  "}");
        free(out);
    }

    /* A DEFAULT member holding an explicit value must be emitted. asn1c
     * represents it as a pointer, so an unset one is indistinguishable from an
     * absent OPTIONAL and is omitted; see README. */
    VNT_CASE("a DEFAULT member holding a value is emitted");
    {
        Nested_t n;
        const unsigned char tag[] = {0x01};
        long deflt = 7;
        memset(&n, 0, sizeof n);
        n.inner.id = 1;
        n.inner.tag.buf = (uint8_t *)tag;
        n.inner.tag.size = sizeof tag;
        n.col = 0;
        n.deflt = &deflt;
        out = vnt_encode(&asn_DEF_Nested, &n, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "deflt 7") != 0);
        free(out);
    }

    VNT_CASE("an unset DEFAULT member is omitted");
    {
        Nested_t n;
        const unsigned char tag[] = {0x01};
        memset(&n, 0, sizeof n);
        n.inner.id = 1;
        n.inner.tag.buf = (uint8_t *)tag;
        n.inner.tag.size = sizeof tag;
        n.col = 0;
        out = vnt_encode(&asn_DEF_Nested, &n, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "deflt") == 0);
        free(out);
    }

    VNT_CASE("canonical mode uses a two-space indent");
    {
        Pair_t p;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_CANONICAL;
        memset(&p, 0, sizeof p);
        p.first = 1;
        out = vnt_encode(&asn_DEF_Pair, &p, &o, reason, sizeof reason);
        VNT_STREQ(out, "{\n  first 1\n}");
        free(out);
    }

    VNT_CASE("annotated mode names the type and notes absent members");
    {
        Pair_t p;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_ANNOTATED;
        memset(&p, 0, sizeof p);
        p.first = 1;
        out = vnt_encode(&asn_DEF_Pair, &p, &o, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "-- Pair --") != 0);
        VNT_TRUE(out && strstr(out, "second") != 0);
        /* The absence note must not introduce a comma of its own. */
        VNT_TRUE(out && strstr(out, ",\n") == 0);
        free(out);
    }

    return vnt_report("t_sequence");
}
