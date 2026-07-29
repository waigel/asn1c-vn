/*
 * vn_rd_constructed.c -- reading SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE and
 * open types.
 *
 * Ownership, the part that normally leaks: an in-progress list element lives in
 * the list's own _asn_ctx.ptr until ASN_SET_ADD takes it, exactly as asn1c does
 * (constr_SET_OF.c), so the type's free function collects it if we abandon. A
 * member is stored into its parent before anything can fail, so it is always
 * reachable from the caller's root pointer.
 */

#include <stdlib.h>
#include <string.h>

#include <asn_SET_OF.h>
#include <constr_CHOICE.h>
#include <constr_SEQUENCE.h>
#include <constr_SET_OF.h>
#include "vn_internal.h"

/* Address of a member, and its slot, so a pointer member can be filled in. */
static void **
vn_rd_member_slot(const asn_TYPE_member_t *elm, void *sptr, void **direct) {
    void *p = (char *)sptr + elm->memb_offset;
    if(elm->flags & ATF_POINTER) return (void **)p;
    *direct = p;
    return direct;
}

/*
 * A member being absent cannot be inferred from the structure alone: asn1c stores
 * a mandatory member inline, so an omitted one is indistinguishable from one
 * whose value happens to be zero. Which members actually appeared therefore has
 * to be tracked while parsing.
 */
#define VN_RD_MAX_MEMBERS 512

/*
 * SEQUENCE and SET differ in exactly one rule, so they share this.
 *
 * 25.20 requires a SEQUENCE's component values to be "in the same order as the
 * corresponding NamedType sequences", while the NOTE to 27.9 says a SET's "may
 * appear in any order". Both still require every non-OPTIONAL, non-DEFAULT
 * member to be present exactly once.
 */
static int
vn_rd_components(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr,
                 int ordered) {
    const asn_SEQUENCE_specifics_t *specs =
        (const asn_SEQUENCE_specifics_t *)td->specifics;
    const char   *what = ordered ? "SEQUENCE" : "SET";
    size_t        from = r->pos;
    vn_token_t    tok;
    void         *st;
    unsigned      next = 0;
    /* A comma separates values: it must follow one and precede another. */
    int           after_value = 0, after_comma = 0;
    unsigned char seen[VN_RD_MAX_MEMBERS / 8];

    if(!specs)
        return vn_rd_fail(r, from, "%s %s has no specifics", what,
                          td->name ? td->name : "(unnamed)");
    if(td->elements_count > VN_RD_MAX_MEMBERS)
        return vn_rd_fail(r, from, "%s has more than %d members",
                          td->name ? td->name : "this SEQUENCE",
                          VN_RD_MAX_MEMBERS);
    memset(seen, 0, sizeof seen);

    if(vn_rd_token(r, &tok) != VT_LBRACE) {
        if(tok.kind == VT_INCOMPLETE || tok.kind == VT_END)
            return vn_rd_more(r, from);
        return vn_rd_fail(r, from, "expected { to start %s",
                          td->name ? td->name : what);
    }

    st = vn_rd_alloc(r, sptr, specs->struct_size);
    if(!st) return VR_FAIL;

    for(;;) {
        size_t     item = r->pos;
        vn_token_e k = vn_rd_token(r, &tok);
        unsigned   i;
        void      *direct = 0;
        void     **slot;

        if(k == VT_INCOMPLETE || k == VT_END) return vn_rd_more(r, from);
        if(k == VT_RBRACE) {
            if(after_comma)
                return vn_rd_fail(r, item, "trailing comma before }");
            break;
        }
        if(k == VT_COMMA) {
            if(!after_value)
                return vn_rd_fail(r, item, "comma with no preceding member");
            after_comma = 1;
            after_value = 0;
            continue;
        }
        if(k != VT_IDENT)
            return vn_rd_fail(r, item, "expected a member name or }");
        if(after_value)
            return vn_rd_fail(r, item, "expected a comma between members");

        /*
         * In a SEQUENCE the components come in declaration order, so the search
         * starts where the previous member left off; that also rejects a
         * duplicate, whose index lies before `next`. A SET admits any order
         * (27.9), so its search starts from the beginning and only the
         * already-seen bitmap rejects a repeat.
         */
        for(i = ordered ? next : 0; i < td->elements_count; i++) {
            const char *nm = td->elements[i].name;
            if(nm && strlen(nm) == tok.body_len
               && memcmp(nm, tok.body, tok.body_len) == 0)
                break;
        }
        if(i >= td->elements_count) {
            unsigned j;
            for(j = 0; ordered && j < next; j++) {
                const char *nm = td->elements[j].name;
                if(nm && strlen(nm) == tok.body_len
                   && memcmp(nm, tok.body, tok.body_len) == 0)
                    return vn_rd_fail(r, item,
                                      "member '%.*s' appears out of order or "
                                      "twice in %s",
                                      (int)tok.body_len, tok.body,
                                      td->name ? td->name : "");
            }
            return vn_rd_fail(r, item, "%s has no member '%.*s'",
                              td->name ? td->name : what,
                              (int)tok.body_len, tok.body);
        }
        if(seen[i / 8] & (unsigned char)(1u << (i % 8)))
            return vn_rd_fail(r, item, "member '%.*s' appears twice in %s",
                              (int)tok.body_len, tok.body,
                              td->name ? td->name : what);

        slot = vn_rd_member_slot(&td->elements[i], st, &direct);
        {
            char saved[sizeof r->member_key];
            int  rc;
            memcpy(saved, r->member_key, sizeof saved);
            vn_member_key(r->member_key, sizeof r->member_key, td->name,
                          td->elements[i].name);
            rc = vn_rd_value(r, td->elements[i].type, slot);
            memcpy(r->member_key, saved, sizeof saved);
            if(rc != VR_OK) return rc;
        }
        seen[i / 8] |= (unsigned char)(1u << (i % 8));
        next = i + 1;
        after_value = 1;
        after_comma = 0;
    }

    /*
     * Fill in defaults and check that nothing mandatory is missing. An absent
     * DEFAULT member is materialised where asn1c kept the value, which keeps the
     * round trip byte-identical: DER omits a component equal to its default
     * anyway, via default_value_cmp.
     */
    {
        unsigned i;
        for(i = 0; i < td->elements_count; i++) {
            const asn_TYPE_member_t *elm = &td->elements[i];
            void                    *direct = 0;
            void                   **slot = vn_rd_member_slot(elm, st, &direct);

            if(seen[i / 8] & (1u << (i % 8))) continue;
            if((elm->flags & ATF_POINTER) && elm->default_value_set) {
                if(elm->default_value_set(slot))
                    return vn_rd_fail(r, from, "cannot install the default of %s",
                                      elm->name ? elm->name : "?");
                continue;
            }
            if(!elm->optional)
                return vn_rd_fail(r, from, "%s is missing mandatory member '%s'",
                                  td->name ? td->name : what,
                                  elm->name ? elm->name : "?");
        }
    }

    return VR_OK;
}

int
vn_rd_sequence(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    return vn_rd_components(r, td, sptr, 1);
}

int
vn_rd_set(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    return vn_rd_components(r, td, sptr, 0);
}

int
vn_rd_set_of(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    const asn_SET_OF_specifics_t *specs =
        (const asn_SET_OF_specifics_t *)td->specifics;
    const asn_TYPE_descriptor_t *elt;
    size_t                       from = r->pos;
    vn_token_t                   tok;
    void                        *st;
    asn_anonymous_set_          *list;
    asn_struct_ctx_t            *ctx;
    int                          after_value = 0, after_comma = 0;

    if(!specs || td->elements_count != 1 || !td->elements[0].type)
        return vn_rd_fail(r, from, "list type %s is not usable",
                          td->name ? td->name : "(unnamed)");
    elt = td->elements[0].type;

    if(vn_rd_token(r, &tok) != VT_LBRACE) {
        if(tok.kind == VT_INCOMPLETE || tok.kind == VT_END)
            return vn_rd_more(r, from);
        return vn_rd_fail(r, from, "expected { to start %s",
                          td->name ? td->name : "a list");
    }

    st = vn_rd_alloc(r, sptr, specs->struct_size);
    if(!st) return VR_FAIL;
    list = _A_SET_FROM_VOID(st);
    ctx = (asn_struct_ctx_t *)((char *)st + specs->ctx_offset);

    for(;;) {
        size_t     item = r->pos;
        vn_token_e k = vn_rd_token(r, &tok);
        int        rc;

        if(k == VT_INCOMPLETE || k == VT_END) return vn_rd_more(r, from);
        if(k == VT_RBRACE) {
            if(after_comma)
                return vn_rd_fail(r, item, "trailing comma before }");
            break;
        }
        if(k == VT_COMMA) {
            if(!after_value)
                return vn_rd_fail(r, item, "comma with no preceding element");
            after_comma = 1;
            after_value = 0;
            continue;
        }
        if(after_value)
            return vn_rd_fail(r, item, "expected a comma between elements");

        /* Re-present this element's own start; parse it into ctx->ptr so an
         * abandoned partial element is still reachable for the free function. */
        r->pos = item;
        {
            char saved[sizeof r->member_key];
            /* "Member" is what asn1c calls an anonymous element type, so that
             * is this step of the scope path; see the writer's counterpart. */
            memcpy(saved, r->member_key, sizeof saved);
            vn_member_key(r->member_key, sizeof r->member_key, td->name,
                          "Member");
            rc = vn_rd_value(r, elt, &ctx->ptr);
            memcpy(r->member_key, saved, sizeof saved);
        }
        if(rc != VR_OK) return rc;
        if(ASN_SET_ADD(list, ctx->ptr) != 0) {
            ASN_STRUCT_FREE(*elt, ctx->ptr);
            ctx->ptr = 0;
            return vn_rd_fail(r, item, "cannot add an element to %s",
                              td->name ? td->name : "the list");
        }
        ctx->ptr = 0;
        after_value = 1;
        after_comma = 0;
    }

    return VR_OK;
}

int
vn_rd_choice(vn_reader_t *r, const asn_TYPE_descriptor_t *td, void **sptr) {
    const asn_CHOICE_specifics_t *specs =
        (const asn_CHOICE_specifics_t *)td->specifics;
    size_t     from = r->pos;
    vn_token_t tok;
    void      *st;
    unsigned   i;
    void      *direct = 0;
    void     **slot;

    if(!specs)
        return vn_rd_fail(r, from, "CHOICE %s has no specifics",
                          td->name ? td->name : "(unnamed)");

    switch(vn_rd_token(r, &tok)) {
    case VT_INCOMPLETE:
    case VT_END: return vn_rd_more(r, from);
    case VT_IDENT: break;
    default:
        return vn_rd_fail(r, from, "expected an alternative name for %s",
                          td->name ? td->name : "a CHOICE");
    }

    for(i = 0; i < td->elements_count; i++) {
        const char *nm = td->elements[i].name;
        if(nm && strlen(nm) == tok.body_len
           && memcmp(nm, tok.body, tok.body_len) == 0)
            break;
    }
    if(i >= td->elements_count)
        return vn_rd_fail(r, from, "%s has no alternative '%.*s'",
                          td->name ? td->name : "this CHOICE",
                          (int)tok.body_len, tok.body);

    {
        size_t     colon = r->pos;
        vn_token_e k = vn_rd_token(r, &tok);
        if(k == VT_INCOMPLETE || k == VT_END) return vn_rd_more(r, from);
        if(k != VT_COLON)
            return vn_rd_fail(r, colon, "expected : after the alternative name");
    }

    st = vn_rd_alloc(r, sptr, specs->struct_size);
    if(!st) return VR_FAIL;

    /* Record the selection before parsing, so the free function knows which
     * union arm to release if the value turns out to be malformed. */
    {
        void *pres = (char *)st + specs->pres_offset;
        switch(specs->pres_size) {
        case sizeof(int): *(int *)pres = (int)(i + 1); break;
        case sizeof(short): *(short *)pres = (short)(i + 1); break;
        case sizeof(char): *(unsigned char *)pres = (unsigned char)(i + 1); break;
        default:
            return vn_rd_fail(r, from,
                              "CHOICE %s uses an unexpected presence width of %u",
                              td->name ? td->name : "", specs->pres_size);
        }
    }

    slot = vn_rd_member_slot(&td->elements[i], st, &direct);
    /* Scope the alternative, as the writer does: an inline one is Pick__speed
     * in the annotation table but arrives here as asn_DEF_NativeInteger. */
    {
        char saved[sizeof r->member_key];
        int  rc;
        memcpy(saved, r->member_key, sizeof saved);
        vn_member_key(r->member_key, sizeof r->member_key, td->name,
                      td->elements[i].name);
        rc = vn_rd_value(r, td->elements[i].type, slot);
        memcpy(r->member_key, saved, sizeof saved);
        return rc;
    }
}
