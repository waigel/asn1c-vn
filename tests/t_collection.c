/*
 * t_collection.c -- SEQUENCE OF, SET OF and CHOICE.
 *
 * List elements carry no field name in value notation; CHOICE uses the
 * `alternative : value` form.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Numbers.h"
#include "Inners.h"
#include "Bag.h"
#include "Choice.h"

int
main(void) {
    char reason[200], *out;

    VNT_CASE("empty list");
    {
        Numbers_t n;
        memset(&n, 0, sizeof n);
        out = vnt_encode(&asn_DEF_Numbers, &n, 0, reason, sizeof reason);
        VNT_STREQ(out, "{ }");
        free(out);
    }

    VNT_CASE("list of scalars carries no member names");
    {
        Numbers_t n;
        long a = 1, b = 2, c = 3;
        long *items[3];
        memset(&n, 0, sizeof n);
        items[0] = &a;
        items[1] = &b;
        items[2] = &c;
        n.list.array = items;
        n.list.count = 3;
        n.list.size = 3;
        out = vnt_encode(&asn_DEF_Numbers, &n, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    1,\n    2,\n    3\n}");
        free(out);
    }

    VNT_CASE("single-element list");
    {
        Numbers_t n;
        long a = 42;
        long *items[1];
        memset(&n, 0, sizeof n);
        items[0] = &a;
        n.list.array = items;
        n.list.count = 1;
        n.list.size = 1;
        out = vnt_encode(&asn_DEF_Numbers, &n, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    42\n}");
        free(out);
    }

    VNT_CASE("SET OF renders like SEQUENCE OF");
    {
        Bag_t bag;
        long a = 5, b = 6;
        long *items[2];
        memset(&bag, 0, sizeof bag);
        items[0] = &a;
        items[1] = &b;
        bag.list.array = items;
        bag.list.count = 2;
        bag.list.size = 2;
        out = vnt_encode(&asn_DEF_Bag, &bag, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    5,\n    6\n}");
        free(out);
    }

    VNT_CASE("list of sequences nests");
    {
        Inners_t l;
        Inner_t i0;
        const unsigned char t0[] = {0xaa};
        Inner_t *items[1];
        memset(&l, 0, sizeof l);
        memset(&i0, 0, sizeof i0);
        i0.id = 9;
        i0.tag.buf = (uint8_t *)t0;
        i0.tag.size = sizeof t0;
        items[0] = &i0;
        l.list.array = items;
        l.list.count = 1;
        l.list.size = 1;
        out = vnt_encode(&asn_DEF_Inners, &l, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    {\n        id 9,\n        tag 'AA'H\n    }\n}");
        free(out);
    }

    VNT_CASE("a null element pointer is reported, not dereferenced");
    {
        Numbers_t n;
        long *items[1];
        memset(&n, 0, sizeof n);
        items[0] = 0;
        n.list.array = items;
        n.list.count = 1;
        n.list.size = 1;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Numbers, &n, 0, reason,
                                  sizeof reason));
        VNT_TRUE(strstr(reason, "null") != 0);
    }

    VNT_CASE("choice uses `alternative : value`");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_flag;
        c.choice.flag = 1;
        out = vnt_encode(&asn_DEF_Choice, &c, 0, reason, sizeof reason);
        VNT_STREQ(out, "flag : TRUE");
        free(out);
    }

    VNT_CASE("choice of the first alternative");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_nothing;
        out = vnt_encode(&asn_DEF_Choice, &c, 0, reason, sizeof reason);
        VNT_STREQ(out, "nothing : NULL");
        free(out);
    }

    VNT_CASE("choice of a constructed alternative");
    {
        Choice_t c;
        const unsigned char t[] = {0x01, 0x02};
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_inner;
        c.choice.inner.id = 4;
        c.choice.inner.tag.buf = (uint8_t *)t;
        c.choice.inner.tag.size = sizeof t;
        out = vnt_encode(&asn_DEF_Choice, &c, 0, reason, sizeof reason);
        VNT_STREQ(out, "inner : {\n    id 4,\n    tag '0102'H\n}");
        free(out);
    }

    VNT_CASE("unset choice fails rather than guessing an alternative");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_NOTHING;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Choice, &c, 0, reason,
                                  sizeof reason));
        VNT_TRUE(strstr(reason, "no alternative") != 0);
    }

    VNT_CASE("an out-of-range present index fails");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = (enum Choice_PR)99;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Choice, &c, 0, reason,
                                  sizeof reason));
    }

    return vnt_report("t_collection");
}
