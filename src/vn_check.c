/*
 * vn_check.c -- subtype constraints, checked over the whole value.
 *
 * asn1c compiles a schema's SIZE, range and alphabet constraints into per-member
 * checker functions and hangs them off the member table, which is exactly what
 * is needed. What is missing is a walk that reaches all of them:
 * SEQUENCE_constraint iterates the members but leaves on the first that has no
 * constraint of its own, because that branch returns the member type's result
 * instead of keeping it and continuing. SET_constraint does the same in both
 * branches. So the walk here is ours, and it calls only the leaf checkers, never
 * the container ones.
 *
 * CHOICE_constraint and SET_OF_constraint are correct in asn1c, but going
 * through them would mean re-entering SEQUENCE_constraint one level down, so
 * this descends into those too.
 */

#include <stdio.h>
#include <string.h>

#include <asn_SET_OF.h>
#include <constr_CHOICE.h>
#include <constr_SEQUENCE.h>
#include <constr_SET_OF.h>
#include "vn_internal.h"

#define VN_OPTAB(name) extern asn_TYPE_operation_t name
VN_OPTAB(asn_OP_CHOICE);
VN_OPTAB(asn_OP_OPEN_TYPE);
VN_OPTAB(asn_OP_SEQUENCE);
VN_OPTAB(asn_OP_SEQUENCE_OF);
VN_OPTAB(asn_OP_SET);
VN_OPTAB(asn_OP_SET_OF);

/* Where the first failure is recorded; the walk stops at it. */
typedef struct vn_ck_s {
    char  *errbuf;
    size_t errlen;
    size_t used;
    int    failed;
    /* Path to the offending member, for a message a person can act on. */
    char   path[VN_MEMBER_KEY_MAX];
} vn_ck_t;

/*
 * asn1c reports a failure through a callback rather than a return string. The
 * text it produces names the type and its own source location; the member path
 * is ours to add, since a bare "OCTET STRING: constraint failed" leaves the
 * caller to guess which of forty members it meant.
 */
static void
vn_ck_failed(void *key, const asn_TYPE_descriptor_t *td, const void *sptr,
             const char *fmt, ...) {
    vn_ck_t *ck = (vn_ck_t *)key;
    (void)td;
    (void)sptr;
    (void)fmt;
    ck->failed = 1;
}

/*
 * Extend the diagnostic path by one step: "header.iccid", "certs[2].code".
 * An index binds to the name before it; every other step is dotted on.
 */
static void
vn_ck_push(char *path, size_t sz, const char *step) {
    size_t n = strlen(path);

    if(!step || !step[0] || n + 2 >= sz) return;
    if(step[0] == '[') snprintf(path + n, sz - n, "%s", step);
    else snprintf(path + n, sz - n, "%s%s", n ? "." : "", step);
}

/*
 * Name the member, not just its type. "OCTET STRING: constraint failed" is true
 * of forty members of a profile and useful for none of them.
 */
static int
vn_ck_note(vn_ck_t *ck, const asn_TYPE_descriptor_t *td, const char *what) {
    const char *type = td && td->name ? td->name : "value";

    if(ck->errbuf && ck->errlen) {
        int n;
        if(ck->path[0])
            n = snprintf(ck->errbuf, ck->errlen, "%s (%s): %s", ck->path, type,
                         what);
        else
            n = snprintf(ck->errbuf, ck->errlen, "%s: %s", type, what);
        ck->used = (n > 0 && (size_t)n < ck->errlen) ? (size_t)n
                                                     : strlen(ck->errbuf);
    }
    return -1;
}

static int vn_ck_walk(vn_ck_t *ck, const asn_TYPE_descriptor_t *td,
                      const void *sptr);

/* Run whichever checker applies to this member, then descend into it. */
static int
vn_ck_member(vn_ck_t *ck, const asn_TYPE_member_t *elm, const void *memb_ptr) {
    asn_constr_check_f *check = elm->encoding_constraints.general_constraints;
    char                saved[VN_MEMBER_KEY_MAX];

    memcpy(saved, ck->path, sizeof saved);
    vn_ck_push(ck->path, sizeof ck->path, elm->name);

    if(check) {
        ck->failed = 0;
        if(check(elm->type, memb_ptr, vn_ck_failed, ck) != 0 || ck->failed) {
            vn_ck_note(ck, elm->type, "constraint failed");
            memcpy(ck->path, saved, sizeof saved);
            return -1;
        }
    }
    if(vn_ck_walk(ck, elm->type, memb_ptr) != 0) {
        memcpy(ck->path, saved, sizeof saved);
        return -1;
    }
    memcpy(ck->path, saved, sizeof saved);
    return 0;
}

/* The member's address, or NULL when an OPTIONAL one is absent. */
static const void *
vn_ck_member_ptr(const asn_TYPE_member_t *elm, const void *sptr, int *absent) {
    *absent = 0;
    if(elm->flags & ATF_POINTER) {
        const void *p = *(const void *const *)((const char *)sptr
                                               + elm->memb_offset);
        if(!p) *absent = 1;
        return p;
    }
    return (const void *)((const char *)sptr + elm->memb_offset);
}

static int
vn_ck_components(vn_ck_t *ck, const asn_TYPE_descriptor_t *td,
                 const void *sptr) {
    size_t i;

    for(i = 0; i < td->elements_count; i++) {
        const asn_TYPE_member_t *elm = &td->elements[i];
        int                      absent;
        const void              *memb_ptr = vn_ck_member_ptr(elm, sptr, &absent);

        if(absent) {
            if(elm->optional) continue;
            vn_ck_push(ck->path, sizeof ck->path, elm->name);
            return vn_ck_note(ck, td, "mandatory member absent");
        }
        if(vn_ck_member(ck, elm, memb_ptr) != 0) return -1;
    }
    return 0;
}

static int
vn_ck_choice(vn_ck_t *ck, const asn_TYPE_descriptor_t *td, const void *sptr) {
    const asn_CHOICE_specifics_t *specs =
        (const asn_CHOICE_specifics_t *)td->specifics;
    const void *pres;
    unsigned    present;
    int         absent;
    const void *memb_ptr;

    if(!specs) return 0;
    pres = (const char *)sptr + specs->pres_offset;
    switch(specs->pres_size) {
    case sizeof(int): present = (unsigned)*(const int *)pres; break;
    case sizeof(short): present = (unsigned)*(const short *)pres; break;
    case sizeof(char): present = (unsigned)*(const unsigned char *)pres; break;
    default: return 0;
    }
    /* Zero means nothing is selected; the encoder will refuse it separately. */
    if(present == 0 || present > td->elements_count) return 0;

    memb_ptr = vn_ck_member_ptr(&td->elements[present - 1], sptr, &absent);
    if(absent) return 0;
    return vn_ck_member(ck, &td->elements[present - 1], memb_ptr);
}

static int
vn_ck_list(vn_ck_t *ck, const asn_TYPE_descriptor_t *td, const void *sptr) {
    const asn_anonymous_set_ *list = _A_CSET_FROM_VOID(sptr);
    const asn_TYPE_member_t  *elm;
    asn_constr_check_f       *check;
    int                       i;

    if(td->elements_count != 1 || !td->elements[0].type) return 0;
    elm = &td->elements[0];
    check = elm->encoding_constraints.general_constraints;

    for(i = 0; i < list->count; i++) {
        const void *memb_ptr = list->array[i];
        char        saved[VN_MEMBER_KEY_MAX];

        if(!memb_ptr) continue; /* a hole is the caller's problem, not ours */
        memcpy(saved, ck->path, sizeof saved);
        {
            char idx[24];
            snprintf(idx, sizeof idx, "[%d]", i);
            vn_ck_push(ck->path, sizeof ck->path, idx);
        }
        if(check) {
            ck->failed = 0;
            if(check(elm->type, memb_ptr, vn_ck_failed, ck) != 0 || ck->failed) {
                vn_ck_note(ck, elm->type, "constraint failed");
                memcpy(ck->path, saved, sizeof saved);
                return -1;
            }
        }
        if(vn_ck_walk(ck, elm->type, memb_ptr) != 0) {
            memcpy(ck->path, saved, sizeof saved);
            return -1;
        }
        memcpy(ck->path, saved, sizeof saved);
    }
    return 0;
}

static int
vn_ck_walk(vn_ck_t *ck, const asn_TYPE_descriptor_t *td, const void *sptr) {
    if(!td || !sptr) return 0;

    if(td->op == &asn_OP_SEQUENCE || td->op == &asn_OP_SET)
        return vn_ck_components(ck, td, sptr);
    if(td->op == &asn_OP_CHOICE || td->op == &asn_OP_OPEN_TYPE)
        return vn_ck_choice(ck, td, sptr);
    if(td->op == &asn_OP_SEQUENCE_OF || td->op == &asn_OP_SET_OF)
        return vn_ck_list(ck, td, sptr);

    /*
     * A leaf. Its own constraint function is the generated one for its type,
     * which is correct -- only the container ones are not -- so it is called
     * here and nowhere else.
     */
    if(td->encoding_constraints.general_constraints) {
        ck->failed = 0;
        if(td->encoding_constraints.general_constraints(td, sptr, vn_ck_failed,
                                                        ck)
               != 0
           || ck->failed)
            return vn_ck_note(ck, td, "constraint failed");
    }
    return 0;
}

int
vn_check_constraints(const asn_TYPE_descriptor_t *td, const void *sptr,
                     char *errbuf, size_t *errlen) {
    vn_ck_t ck;

    memset(&ck, 0, sizeof ck);
    ck.errbuf = errbuf;
    ck.errlen = errlen ? *errlen : 0;
    if(errbuf && ck.errlen) errbuf[0] = '\0';

    if(!td || !sptr) {
        if(errlen) *errlen = 0;
        return 0;
    }
    if(vn_ck_walk(&ck, td, sptr) != 0) {
        if(errlen) *errlen = ck.used;
        return -1;
    }
    if(errlen) *errlen = 0;
    return 0;
}
