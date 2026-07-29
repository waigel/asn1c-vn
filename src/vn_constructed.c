/*
 * vn_constructed.c -- value notation for composite types.
 *
 * SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE and open types. These recurse
 * through vn_encode_value() rather than knowing their members' syntax.
 */

#include <OPEN_TYPE.h>
#include <asn_SET_OF.h>
#include <constr_CHOICE.h>
#include <constr_SEQUENCE.h>
#include "vn_internal.h"

/*
 * Address of a member, or NULL when an ATF_POINTER member is absent. asn1c
 * represents OPTIONAL and DEFAULT members as pointers, so a null pointer is
 * the only signal that a member carries no value.
 */
const void *
vn_member_ptr(const asn_TYPE_member_t *elm, const void *sptr) {
    const void *p = (const char *)sptr + elm->memb_offset;
    if(elm->flags & ATF_POINTER) return *(const void *const *)p;
    return p;
}

/* SEQUENCE and SET share one form: { field value, field value }. */
int
vn_h_sequence(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
              const void *sptr, int level) {
    unsigned i;
    int emitted = 0;

    if(vn_putc(w, '{') < 0) return -1;
    if(vn_is_annotated(w) && td->name && td->name[0]) {
        if(vn_putc(w, ' ') < 0) return -1;
        if(vn_comment(w, "%s", td->name) < 0) return -1;
    }

    for(i = 0; i < td->elements_count; i++) {
        const asn_TYPE_member_t *elm = &td->elements[i];
        const void *memb = vn_member_ptr(elm, sptr);

        if(!memb) {
            /*
             * A member absent from the encoding may still have a DEFAULT value.
             * asn1c retains it for natively stored types -- INTEGER, BOOLEAN,
             * ENUMERATED -- as a setter on the member, so the value can be
             * reconstructed and emitted. Printing it explicitly is valid X.680
             * and matches what reference tooling does.
             *
             * For buffer-backed types (OCTET STRING, BIT STRING, strings) asn1c
             * discards the default entirely, so nothing can be recovered and the
             * member stays omitted. See README "Deviations from X.680".
             */
            if(elm->default_value_set) {
                void *dflt = 0;
                int rc;

                if(elm->default_value_set(&dflt) != 0) {
                    if(dflt) ASN_STRUCT_FREE(*elm->type, dflt);
                    return vn_fail(w, td, sptr,
                                   "cannot reconstruct the DEFAULT value of %s",
                                   elm->name ? elm->name : "?");
                }
                rc = 0;
                if(emitted && vn_putc(w, ',') < 0) rc = -1;
                if(!rc && vn_break(w, level + 1) < 0) rc = -1;
                if(!rc && elm->name && elm->name[0]) {
                    if(vn_puts(w, elm->name) < 0) rc = -1;
                    if(!rc && vn_putc(w, ' ') < 0) rc = -1;
                }
                if(!rc) {
                    char saved[sizeof w->member_key];
                    memcpy(saved, w->member_key, sizeof saved);
                    vn_member_key(w->member_key, sizeof w->member_key, td->name,
                                  elm->name);
                    if(vn_encode_value(w, elm->type, dflt, level + 1) < 0)
                        rc = -1;
                    memcpy(w->member_key, saved, sizeof saved);
                }
                if(!rc && vn_is_annotated(w)) {
                    if(vn_putc(w, ' ') < 0) rc = -1;
                    if(!rc && vn_comment(w, "DEFAULT") < 0) rc = -1;
                }
                ASN_STRUCT_FREE(*elm->type, dflt);
                if(rc < 0) return -1;
                emitted = 1;
                continue;
            }

            /* Nothing to emit. In annotated mode say so, but leave `emitted`
             * alone -- a comment is not a value, so it must neither attract a
             * comma of its own nor make the next real member expect one. */
            if(vn_is_annotated(w)) {
                if(vn_break(w, level + 1) < 0) return -1;
                if(vn_comment(w, "%s absent", elm->name ? elm->name : "?") < 0)
                    return -1;
            }
            continue;
        }

        if(emitted && vn_putc(w, ',') < 0) return -1;
        if(vn_break(w, level + 1) < 0) return -1;
        if(elm->name && elm->name[0]) {
            if(vn_puts(w, elm->name) < 0) return -1;
            if(vn_putc(w, ' ') < 0) return -1;
        }
        {
            char saved[sizeof w->member_key];
            int  rc;
            memcpy(saved, w->member_key, sizeof saved);
            vn_member_key(w->member_key, sizeof w->member_key, td->name,
                          elm->name);
            rc = vn_encode_value(w, elm->type, memb, level + 1);
            memcpy(w->member_key, saved, sizeof saved);
            if(rc < 0) return -1;
        }
        emitted = 1;
    }

    if(!emitted) return vn_puts(w, " }");
    if(vn_break(w, level) < 0) return -1;
    return vn_putc(w, '}');
}

/* SEQUENCE OF and SET OF: { value, value }, with no member names. */
int
vn_h_set_of(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            int level) {
    const asn_anonymous_set_ *list = _A_CSET_FROM_VOID(sptr);
    const asn_TYPE_descriptor_t *elt;
    int i;

    if(td->elements_count != 1 || !td->elements[0].type)
        return vn_fail(w, td, sptr,
                       "list type %s has no element type descriptor",
                       td->name ? td->name : "(unnamed)");
    elt = td->elements[0].type;

    if(vn_putc(w, '{') < 0) return -1;
    if(vn_is_annotated(w) && td->name && td->name[0]) {
        if(vn_putc(w, ' ') < 0) return -1;
        if(vn_comment(w, "%s, %d element(s)", td->name, list->count) < 0)
            return -1;
    }
    if(list->count <= 0) return vn_puts(w, " }");

    for(i = 0; i < list->count; i++) {
        if(i && vn_putc(w, ',') < 0) return -1;
        if(vn_break(w, level + 1) < 0) return -1;
        if(!list->array[i])
            return vn_fail(w, td, sptr,
                           "list %s element %d is a null pointer",
                           td->name ? td->name : "(unnamed)", i);
        if(vn_encode_value(w, elt, list->array[i], level + 1) < 0) return -1;
    }
    if(vn_break(w, level) < 0) return -1;
    return vn_putc(w, '}');
}

/*
 * CHOICE: `alternative : value`. Also serves table-constrained open types,
 * which asn1c lays out identically.
 */
int
vn_h_choice(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            int level) {
    const asn_CHOICE_specifics_t *specs =
        (const asn_CHOICE_specifics_t *)td->specifics;
    const asn_TYPE_member_t *elm;
    const void *memb;
    const void *pres;
    unsigned present;

    if(!specs)
        return vn_fail(w, td, sptr, "CHOICE %s has no specifics",
                       td->name ? td->name : "(unnamed)");

    /* The present index lives at pres_offset and is pres_size bytes wide. */
    pres = (const char *)sptr + specs->pres_offset;
    switch(specs->pres_size) {
    case sizeof(int):
        present = (unsigned)*(const int *)pres;
        break;
    case sizeof(short):
        present = (unsigned)*(const short *)pres;
        break;
    case sizeof(char):
        present = (unsigned)*(const unsigned char *)pres;
        break;
    default:
        return vn_fail(w, td, sptr,
                       "CHOICE %s uses an unexpected presence width of %u bytes",
                       td->name ? td->name : "(unnamed)", specs->pres_size);
    }

    /* Zero means nothing is selected; members are numbered from one. */
    if(present == 0 || present > td->elements_count)
        return vn_fail(w, td, sptr, "CHOICE %s has no alternative selected",
                       td->name ? td->name : "(unnamed)");

    elm = &td->elements[present - 1];
    memb = vn_member_ptr(elm, sptr);
    if(!memb)
        return vn_fail(w, td, sptr,
                       "CHOICE %s selects %s but that member is a null pointer",
                       td->name ? td->name : "(unnamed)",
                       elm->name ? elm->name : "?");

    if(elm->name && elm->name[0]) {
        if(vn_puts(w, elm->name) < 0) return -1;
        if(vn_puts(w, " : ") < 0) return -1;
    }
    return vn_encode_value(w, elm->type, memb, level);
}

/*
 * A table-constrained open type resolves to a concrete descriptor: asn1c stores
 * the decoded value under the selected alternative and gives the descriptor an
 * asn_CHOICE_specifics_t, so the CHOICE handler applies unchanged. Verified
 * against the generated Msg.c, where the open type member's descriptor pairs
 * &asn_OP_OPEN_TYPE with asn_SPC_body_specs.
 */
int
vn_h_open_type(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
               const void *sptr, int level) {
    return vn_h_choice(w, td, sptr, level);
}
