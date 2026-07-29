/*
 * vn_primitive.c -- value notation for leaf types.
 *
 * Handlers here know the syntax of a single ASN.1 type and nothing about where
 * the bytes go; that is the writer's business.
 */

#include <BOOLEAN.h>
#include <NULL.h>
#include "vn_internal.h"

int
vn_h_boolean(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             int level) {
    const BOOLEAN_t *b = (const BOOLEAN_t *)sptr;
    (void)td;
    (void)level;
    /* BER allows any nonzero octet to mean true, so test for nonzero rather
     * than comparing against 1. */
    return vn_puts(w, *b ? "TRUE" : "FALSE");
}

int
vn_h_null(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
          int level) {
    (void)td;
    (void)sptr;
    (void)level;
    return vn_puts(w, "NULL");
}
