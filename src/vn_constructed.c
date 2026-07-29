/*
 * vn_constructed.c -- value notation for composite types.
 *
 * SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE and open types. These recurse
 * through vn_encode_value() rather than knowing their members' syntax.
 */

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
            /* Absent member: omit it. In annotated mode say so, but leave
             * `emitted` alone -- a comment is not a value, so it must neither
             * attract a comma of its own nor make the next real member think
             * one is due. */
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
        if(vn_encode_value(w, elm->type, memb, level + 1) < 0) return -1;
        emitted = 1;
    }

    if(!emitted) return vn_puts(w, " }");
    if(vn_break(w, level) < 0) return -1;
    return vn_putc(w, '}');
}
