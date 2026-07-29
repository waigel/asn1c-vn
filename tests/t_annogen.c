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

    return vnt_report("t_annogen");
}
