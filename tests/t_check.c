/*
 * t_check.c -- vn_check_constraints over the type tree.
 *
 * The point of having our own walk rather than calling asn1c's
 * asn_check_constraints is that asn1c's stops at the first member of a SEQUENCE
 * or SET that carries no constraint of its own (constr_SEQUENCE.c, fixed by
 * contrib/asn1c-B-constraint-loop.patch). Every fixture here puts the
 * constrained member behind an unconstrained one, so a checker with that fault
 * reports them all clean.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Record.h"
#include "Records.h"
#include "Pick.h"
#include "Loose.h"

/* Build a Record on the stack; buffers belong to the caller. */
static void
mk_record(Record_t *r, const uint8_t *code, size_t code_len,
          const char *label) {
    memset(r, 0, sizeof *r);
    r->tag = 1;
    r->code.buf = (uint8_t *)code;
    r->code.size = code_len;
    if(label) {
        static UTF8String_t held;
        memset(&held, 0, sizeof held);
        held.buf = (uint8_t *)label;
        held.size = strlen(label);
        r->label = &held;
    }
}

static void
accepts(const asn_TYPE_descriptor_t *td, const void *sptr, const char *what) {
    char   err[256];
    size_t elen = sizeof err;
    VNT_CASE(what);
    err[0] = '\0';
    if(vn_check_constraints(td, sptr, err, &elen) != 0) {
        fprintf(stderr, "FAIL [%s]: rejected: %s\n", what, err);
        vnt_failures++;
    }
}

/*
 * `naming` is the member the message must point at. A diagnostic that says only
 * "OCTET STRING: constraint failed" leaves the caller to guess which of forty
 * members it meant, which is most of the value of checking at all.
 */
static void
rejects(const asn_TYPE_descriptor_t *td, const void *sptr, const char *what,
        const char *naming) {
    char   err[256];
    size_t elen = sizeof err;
    VNT_CASE(what);
    err[0] = '\0';
    if(vn_check_constraints(td, sptr, err, &elen) == 0) {
        fprintf(stderr, "FAIL [%s]: accepted, should not have\n", what);
        vnt_failures++;
        return;
    }
    if(!strstr(err, naming)) {
        fprintf(stderr, "FAIL [%s]: message does not name '%s': %s\n", what,
                naming, err);
        vnt_failures++;
    }
    VNT_TRUE(elen == strlen(err));
}

int
main(void) {
    static const uint8_t four[] = {1, 2, 3, 4};
    static const uint8_t two[] = {1, 2};
    Record_t good, bad;

    mk_record(&good, four, sizeof four, 0);

    /* --- the leaf cases ---------------------------------------------------- */

    accepts(&asn_DEF_Record, &good, "a value inside its SIZE passes");

    mk_record(&bad, two, sizeof two, 0);
    rejects(&asn_DEF_Record, &bad,
            "a SIZE violation behind an unconstrained member is caught", "code");

    mk_record(&bad, four, sizeof four, "far too long a label");
    rejects(&asn_DEF_Record, &bad, "a violated range on an OPTIONAL is caught",
            "label");

    /* An absent OPTIONAL is not a violation. */
    mk_record(&good, four, sizeof four, 0);
    accepts(&asn_DEF_Record, &good, "an absent OPTIONAL member is fine");

    /* --- through a list ---------------------------------------------------- */

    {
        Record_t  ok, no;
        Record_t *items[2];
        Records_t list;

        mk_record(&ok, four, sizeof four, 0);
        memset(&list, 0, sizeof list);
        items[0] = &ok;
        list.list.array = items;
        list.list.count = 1;
        list.list.size = 2;
        accepts(&asn_DEF_Records, &list, "a clean list passes");

        mk_record(&no, two, sizeof two, 0);
        items[1] = &no;
        list.list.count = 2;
        rejects(&asn_DEF_Records, &list,
                "a violation in the second list element is caught", "[1]");
    }

    /* --- through an alternative -------------------------------------------- */

    {
        Pick_t p;
        memset(&p, 0, sizeof p);
        p.present = Pick_PR_plain;
        p.choice.plain = 7;
        accepts(&asn_DEF_Pick, &p, "an unconstrained alternative passes");

        memset(&p, 0, sizeof p);
        p.present = Pick_PR_rec;
        mk_record(&p.choice.rec, two, sizeof two, 0);
        rejects(&asn_DEF_Pick, &p,
                "a violation inside a CHOICE alternative is caught", "rec");
    }

    /* --- a schema with nothing to check ------------------------------------ */

    {
        Loose_t l;
        memset(&l, 0, sizeof l);
        l.a = 5;
        l.b.buf = (uint8_t *)four;
        l.b.size = sizeof four;
        accepts(&asn_DEF_Loose, &l, "an unconstrained type is left alone");
    }

    /* --- a missing mandatory member ----------------------------------------- */

    {
        /* asn1c stores a mandatory constructed member inline, so absence can
         * only be seen for a pointer member; this is the case it can see. */
        Records_t list;
        Record_t *items[1];
        memset(&list, 0, sizeof list);
        items[0] = 0;
        list.list.array = items;
        list.list.count = 1;
        list.list.size = 1;
        accepts(&asn_DEF_Records, &list,
                "a null list slot is skipped rather than dereferenced");
    }

    return vnt_report("t_check");
}
