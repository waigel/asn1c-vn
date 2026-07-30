/*
 * t_annogen.c -- the vn-annotate generator, checked through its real output.
 *
 * The other annotation test (t_annotate.c) uses hand-written tables; this one
 * links the table vn-annotate produced from tests/schemas/annotate.asn1 and
 * asserts the names in it are X.680 identifiers. asn1c spells them with
 * underscores in the C headers (`Threshold_light_red`), and X.680 12.3 admits
 * letters, digits and hyphens only, so the generator has to map the spelling
 * back rather than copy it.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Threshold.h"
#include "Caps.h"
#include "Pe-Level.h"
#include "Pick.h"
#include "Outer.h"
#include "Count.h"
#include "Wide.h"
#include "Slot.h"
#include "Hyphen-Probe.h"

extern const vn_annotations_t vn_generated_annotations;

static const vn_named_value_t *
nv_find(const vn_type_names_t *t, const char *name) {
    size_t i;
    for(i = 0; i < t->count; i++)
        if(strcmp(t->values[i].name, name) == 0) return &t->values[i];
    return 0;
}

int
main(void) {
    const vn_annotations_t *ann = &vn_generated_annotations;
    const vn_type_names_t *t;
    char reason[200], *out;
    size_t i, j;

    /* --- no C spellings anywhere ----------------------------------------- */

    VNT_CASE("value names never contain an underscore");
    for(i = 0; i < ann->count; i++)
        for(j = 0; j < ann->types[i].count; j++)
            VNT_TRUE(strchr(ann->types[i].values[j].name, '_') == 0);

    VNT_CASE("type keys map former hyphens but keep the __ scope boundary");
    for(i = 0; i < ann->count; i++) {
        const char *p = ann->types[i].type_name;
        for(; *p; p++) {
            if(*p != '_') continue;
            /* A lone underscore was a hyphen and must have been mapped; a
             * double underscore is asn1c's inline-member separator. */
            VNT_TRUE(p[1] == '_');
            if(p[1] == '_') p++;
        }
    }

    /* --- INTEGER named numbers ------------------------------------------- */

    VNT_CASE("hyphenated named numbers are recovered");
    t = vn_annotations_find(ann, "Threshold");
    VNT_TRUE(t != 0);
    if(t) {
        VNT_TRUE(t->is_bit_string == 0);
        VNT_TRUE(nv_find(t, "light-red") && nv_find(t, "light-red")->value == 1);
        VNT_TRUE(nv_find(t, "dark-blue") && nv_find(t, "dark-blue")->value == 2);
        VNT_TRUE(nv_find(t, "auto") && nv_find(t, "auto")->value == 3);
    }

    /* --- BIT STRING named bits ------------------------------------------- */

    VNT_CASE("hyphenated named bits are recovered");
    t = vn_annotations_find(ann, "Caps");
    VNT_TRUE(t != 0);
    if(t) {
        VNT_TRUE(t->is_bit_string == 1);
        VNT_TRUE(nv_find(t, "key-cert") && nv_find(t, "key-cert")->value == 0);
        VNT_TRUE(nv_find(t, "data-encipher")
                 && nv_find(t, "data-encipher")->value == 3);
    }

    /* --- hyphen in the type name itself ----------------------------------- */

    VNT_CASE("a hyphenated type name matches its runtime descriptor name");
    t = vn_annotations_find(ann, asn_DEF_Pe_Level.name);
    VNT_TRUE(t != 0);
    if(t) VNT_TRUE(nv_find(t, "deep-black")
                   && nv_find(t, "deep-black")->value == 9);

    /* --- the inline member's scoped key ------------------------------------ */

    VNT_CASE("an inline member keeps its __ key and maps its value names");
    t = vn_annotations_find(ann, "Box__mode");
    VNT_TRUE(t != 0);
    if(t) VNT_TRUE(nv_find(t, "slow-start") != 0);

    VNT_CASE("an inline CHOICE alternative is scoped the same way");
    t = vn_annotations_find(ann, "Pick__speed");
    VNT_TRUE(t != 0);
    if(t) VNT_TRUE(nv_find(t, "crawl") && nv_find(t, "sprint"));

    /* --- end to end through the encoder ------------------------------------ */

    VNT_CASE("the generated table drives identifier output");
    {
        Threshold_t v = 1;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.annotations = ann;
        out = vnt_encode(&asn_DEF_Threshold, &v, &o, reason, sizeof reason);
        VNT_STREQ(out, "light-red");
        free(out);
    }
    {
        static const unsigned char one_bit[] = {0x80}; /* bit 0 = key-cert */
        Caps_t bs;
        vn_options_t o;
        memset(&bs, 0, sizeof bs);
        memset(&o, 0, sizeof o);
        bs.buf = (uint8_t *)one_bit;
        bs.size = 1;
        bs.bits_unused = 7;
        o.annotations = ann;
        out = vnt_encode(&asn_DEF_Caps, &bs, &o, reason, sizeof reason);
        VNT_STREQ(out, "{ key-cert }");
        free(out);
    }

    /*
     * A CHOICE alternative reaches its scoped entry only if the handler
     * establishes the scope before descending: the alternative's own descriptor
     * is the shared asn_DEF_NativeInteger, whose name is "INTEGER".
     */
    VNT_CASE("an inline CHOICE alternative resolves its named numbers");
    {
        Pick_t       p;
        vn_options_t o;
        memset(&p, 0, sizeof p);
        memset(&o, 0, sizeof o);
        p.present = Pick_PR_speed;
        p.choice.speed = 1;
        o.annotations = ann;
        out = vnt_encode(&asn_DEF_Pick, &p, &o, reason, sizeof reason);
        VNT_STREQ(out, "speed : sprint");
        free(out);
    }

    VNT_CASE("and reads that identifier back");
    {
        void             *st = 0;
        vn_read_options_t ro;
        const char       *text = "speed : sprint";
        memset(&ro, 0, sizeof ro);
        ro.flags = VN_RF_EOF;
        ro.annotations = ann;
        VNT_TRUE(vn_decode(0, &asn_DEF_Pick, &st, &ro, text, strlen(text)).code
                 == RC_OK);
        if(st) {
            VNT_TRUE(((Pick_t *)st)->present == Pick_PR_speed
                     && ((Pick_t *)st)->choice.speed == 1);
            ASN_STRUCT_FREE(asn_DEF_Pick, st);
        }
    }

    /*
     * A nested inline definition. asn1c keys it by the whole path, and the
     * anonymous SEQUENCE between it and Outer contributes only the member name
     * it was reached through, so the scope has to accumulate.
     */
    VNT_CASE("nested inline definitions are keyed by their whole path");
    VNT_TRUE(vn_annotations_find(ann, "Outer__inner__x") != 0);
    VNT_TRUE(vn_annotations_find(ann, "Outer__ring__Member__y") != 0);

    VNT_CASE("and both resolve through the encoder");
    {
        Outer_t         o;
        struct Outer__ring__Member elem;
        struct Outer__ring__Member *elems[1];
        vn_options_t    eo;
        memset(&o, 0, sizeof o);
        memset(&elem, 0, sizeof elem);
        memset(&eo, 0, sizeof eo);
        o.inner.x = 1;
        elem.y = 2;
        elems[0] = &elem;
        o.ring.list.array = (struct Outer__ring__Member **)elems;
        o.ring.list.count = 1;
        o.ring.list.size = 1;
        eo.mode = VN_MODE_CANONICAL;
        eo.annotations = ann;
        out = vnt_encode(&asn_DEF_Outer, &o, &eo, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "alpha") != 0);
        VNT_TRUE(out && strstr(out, "beta") != 0);
        if(out && (!strstr(out, "alpha") || !strstr(out, "beta")))
            fprintf(stderr, "  got: %s\n", out);
        free(out);
    }

    /*
     * The representations asn1c picks other than a plain `long`. Each reaches a
     * different encoder path, and every one of them has to consult the table:
     * otherwise the reader accepts an identifier the writer never emits.
     */
    VNT_CASE("an unsigned INTEGER resolves its named numbers");
    {
        Count_t      v = 255;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.annotations = ann;
        out = vnt_encode(&asn_DEF_Count, &v, &o, reason, sizeof reason);
        VNT_STREQ(out, "all");
        free(out);
    }

    VNT_CASE("a buffer-backed INTEGER resolves its named numbers");
    {
        /* 4294967295 as a minimal two's-complement big-endian magnitude. */
        static const uint8_t brim[] = {0x00, 0xff, 0xff, 0xff, 0xff};
        Wide_t       w;
        vn_options_t o;
        memset(&w, 0, sizeof w);
        memset(&o, 0, sizeof o);
        w.buf = (uint8_t *)brim;
        w.size = sizeof brim;
        o.annotations = ann;
        out = vnt_encode(&asn_DEF_Wide, &w, &o, reason, sizeof reason);
        VNT_STREQ(out, "brim");
        free(out);
    }

    /*
     * An inline BIT STRING member. asn1c writes no base typedef for it, so the
     * generator has to read the representation off the struct member instead;
     * getting it wrong leaves the named bits silently unavailable.
     */
    VNT_CASE("an inline BIT STRING member is typed as named bits");
    t = vn_annotations_find(ann, "Slot__marks");
    VNT_TRUE(t != 0);
    if(t) {
        VNT_TRUE(t->is_bit_string == 1);
        VNT_TRUE(nv_find(t, "first-mark") && nv_find(t, "third-mark"));
    }

    VNT_CASE("and its named bits round trip through both directions");
    {
        static const uint8_t bits[] = {0xa0}; /* bits 0 and 2 */
        static const uint8_t tail[] = {0x01};
        Slot_t       s;
        vn_options_t eo;
        memset(&s, 0, sizeof s);
        memset(&eo, 0, sizeof eo);
        s.marks.buf = (uint8_t *)bits;
        s.marks.size = 1;
        s.marks.bits_unused = 5;
        s.tail.buf = (uint8_t *)tail;
        s.tail.size = 1;
        eo.mode = VN_MODE_CANONICAL;
        eo.annotations = ann;
        out = vnt_encode(&asn_DEF_Slot, &s, &eo, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "{ first-mark, third-mark }") != 0);
        if(out && !strstr(out, "{ first-mark, third-mark }"))
            fprintf(stderr, "  got: %s\n", out);
        if(out) {
            void             *st = 0;
            vn_read_options_t ro;
            memset(&ro, 0, sizeof ro);
            ro.flags = VN_RF_EOF;
            ro.annotations = ann;
            VNT_TRUE(vn_decode(0, &asn_DEF_Slot, &st, &ro, out, strlen(out)).code
                     == RC_OK);
            if(st) ASN_STRUCT_FREE(asn_DEF_Slot, st);
        }
        free(out);
    }

    /*
     * The property all of the above is really about: whatever the writer emits,
     * the reader takes, for every representation. An identifier accepted on
     * input but never produced on output is the asymmetry that hides here.
     */
    VNT_CASE("every representation reads back what it wrote");
    {
        static const uint8_t brim[] = {0x00, 0xff, 0xff, 0xff, 0xff};
        Count_t              c = 255;
        Wide_t               w;
        struct {
            const asn_TYPE_descriptor_t *td;
            const void                  *value;
        } cases[3];
        size_t n;
        Threshold_t th = 2;

        memset(&w, 0, sizeof w);
        w.buf = (uint8_t *)brim;
        w.size = sizeof brim;
        cases[0].td = &asn_DEF_Count;
        cases[0].value = &c;
        cases[1].td = &asn_DEF_Wide;
        cases[1].value = &w;
        cases[2].td = &asn_DEF_Threshold;
        cases[2].value = &th;

        for(n = 0; n < sizeof cases / sizeof cases[0]; n++) {
            vn_options_t      eo;
            vn_read_options_t ro;
            void             *st = 0;
            memset(&eo, 0, sizeof eo);
            memset(&ro, 0, sizeof ro);
            eo.annotations = ann;
            ro.annotations = ann;
            ro.flags = VN_RF_EOF;
            out = vnt_encode(cases[n].td, cases[n].value, &eo, reason,
                             sizeof reason);
            VNT_TRUE(out != 0);
            if(!out) continue;
            VNT_TRUE(vn_decode(0, cases[n].td, &st, &ro, out, strlen(out)).code
                     == RC_OK);
            if(st) {
                VNT_TRUE(cases[n].td->op->compare_struct(cases[n].td, st,
                                                         cases[n].value)
                         == 0);
                ASN_STRUCT_FREE(*cases[n].td, st);
            } else {
                fprintf(stderr, "  [%s] could not read back: %s\n",
                        cases[n].td->name, out);
            }
            free(out);
        }
    }

    /*
     * 11.8: "The NON-BREAKING HYPHEN and the HYPHEN-MINUS should be treated as
     * identical in all names", with the note that My-Type is one name either
     * way. U+2011 is three bytes in UTF-8, so a plain length-and-memcmp match
     * cannot see through it. Every kind of identifier the reader matches is
     * covered here: a member name, a named number, an enumerator and a named
     * bit.
     *
     * The escape is kept in its own literal because "\xe2\x80\x91f" would
     * otherwise scan as one over-long hex escape.
     */
#define NBH "\xe2\x80\x91"
    VNT_CASE("a non-breaking hyphen names the same thing as a hyphen");
    {
        static const char plain[] =
            "{ first-field one-value, second-field plain-red,"
            "  third-field { low-bit } }";
        static const char nbh[] =
            "{ first" NBH "field one" NBH "value,"
            "  second" NBH "field plain" NBH "red,"
            "  third" NBH "field { low" NBH "bit } }";
        void             *a = 0, *b = 0;
        vn_read_options_t ro;
        char              reason2[200];

        memset(&ro, 0, sizeof ro);
        ro.flags = VN_RF_EOF;
        ro.annotations = ann;
        ro.errbuf = reason2;
        ro.errlen = sizeof reason2;
        reason2[0] = '\0';

        VNT_TRUE(vn_decode(0, &asn_DEF_Hyphen_Probe, &a, &ro, plain,
                           sizeof plain - 1)
                     .code
                 == RC_OK);
        if(vn_decode(0, &asn_DEF_Hyphen_Probe, &b, &ro, nbh, sizeof nbh - 1)
               .code
           != RC_OK) {
            fprintf(stderr, "  non-breaking hyphen rejected: %s\n", reason2);
            vnt_failures++;
        }
        if(a && b)
            VNT_TRUE(asn_DEF_Hyphen_Probe.op->compare_struct(
                         &asn_DEF_Hyphen_Probe, a, b)
                     == 0);
        if(a) ASN_STRUCT_FREE(asn_DEF_Hyphen_Probe, a);
        if(b) ASN_STRUCT_FREE(asn_DEF_Hyphen_Probe, b);
    }

    /*
     * The same input one byte at a time. A three-byte U+2011 straddling the
     * buffer edge must read as "the token has not finished" rather than as its
     * end, which is the case a whole-buffer test can never reach.
     */
    VNT_CASE("a non-breaking hyphen split across presentations still parses");
    {
        static const char nbh[] =
            "{ first" NBH "field one" NBH "value,"
            "  second" NBH "field plain" NBH "red,"
            "  third" NBH "field { low" NBH "bit } }";
        const size_t      len = sizeof nbh - 1;
        void             *st = 0;
        vn_read_options_t ro;
        size_t            fed;
        int               guard, done = 0;

        memset(&ro, 0, sizeof ro);
        ro.annotations = ann;

        for(guard = 0, fed = 0; guard < 100000; guard++) {
            asn_dec_rval_t dv;
            if(fed < len) fed++;
            ro.flags = (fed >= len) ? VN_RF_EOF : 0u;
            dv = vn_decode(0, &asn_DEF_Hyphen_Probe, &st, &ro, nbh, fed);
            if(dv.code == RC_OK) {
                done = 1;
                break;
            }
            if(dv.code == RC_FAIL || fed >= len) break;
            if(st) {
                ASN_STRUCT_FREE(asn_DEF_Hyphen_Probe, st);
                st = 0;
            }
        }
        VNT_TRUE(done);
        if(st) ASN_STRUCT_FREE(asn_DEF_Hyphen_Probe, st);
    }
#undef NBH

    return vnt_report("t_annogen");
}
