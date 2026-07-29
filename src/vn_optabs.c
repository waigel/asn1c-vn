/*
 * vn_optabs.c -- weak fallback definitions for asn1c's skeleton symbols.
 *
 * asn1c copies only the skeletons a schema actually uses into its output
 * directory: a schema with no BOOLEAN yields no BOOLEAN.c and no BOOLEAN.h.
 * The type dispatch in vn_encoder.c nevertheless names every operation table,
 * and the handlers call a handful of skeleton helper functions, so without
 * help the link would fail against any real generated directory.
 *
 * Each table below is defined *weakly*. When the real skeleton is present its
 * strong definition overrides ours and dispatch works normally. When it is
 * absent, the reference resolves to the zero-filled dummy here -- a distinct
 * address no type descriptor can ever point at, so it simply never matches.
 *
 * Weak references (__attribute__((weak)) on an extern declaration, or
 * weak_import) do NOT work for this on Mach-O, where they apply only to
 * dynamic libraries. Weak definitions do, on both ELF and Mach-O.
 *
 * This file must stay a separate translation unit. A weak definition sitting
 * in the same unit as its reference can be bound at compile time, which would
 * defeat the override.
 */

#include <asn_application.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "asn1c-vn needs GCC or clang for weak symbol support; see README"
#endif

#define VN_WEAK_OPTAB(name) \
    __attribute__((weak)) asn_TYPE_operation_t name

VN_WEAK_OPTAB(asn_OP_ANY);
VN_WEAK_OPTAB(asn_OP_BIT_STRING);
VN_WEAK_OPTAB(asn_OP_BMPString);
VN_WEAK_OPTAB(asn_OP_BOOLEAN);
VN_WEAK_OPTAB(asn_OP_CHOICE);
VN_WEAK_OPTAB(asn_OP_ENUMERATED);
VN_WEAK_OPTAB(asn_OP_GeneralString);
VN_WEAK_OPTAB(asn_OP_GeneralizedTime);
VN_WEAK_OPTAB(asn_OP_GraphicString);
VN_WEAK_OPTAB(asn_OP_IA5String);
VN_WEAK_OPTAB(asn_OP_INTEGER);
VN_WEAK_OPTAB(asn_OP_ISO646String);
VN_WEAK_OPTAB(asn_OP_NULL);
VN_WEAK_OPTAB(asn_OP_NativeEnumerated);
VN_WEAK_OPTAB(asn_OP_NativeInteger);
VN_WEAK_OPTAB(asn_OP_NativeReal);
VN_WEAK_OPTAB(asn_OP_NumericString);
VN_WEAK_OPTAB(asn_OP_OBJECT_IDENTIFIER);
VN_WEAK_OPTAB(asn_OP_OCTET_STRING);
VN_WEAK_OPTAB(asn_OP_OPEN_TYPE);
VN_WEAK_OPTAB(asn_OP_ObjectDescriptor);
VN_WEAK_OPTAB(asn_OP_PrintableString);
VN_WEAK_OPTAB(asn_OP_REAL);
VN_WEAK_OPTAB(asn_OP_RELATIVE_OID);
VN_WEAK_OPTAB(asn_OP_SEQUENCE);
VN_WEAK_OPTAB(asn_OP_SEQUENCE_OF);
VN_WEAK_OPTAB(asn_OP_SET);
VN_WEAK_OPTAB(asn_OP_SET_OF);
VN_WEAK_OPTAB(asn_OP_T61String);
VN_WEAK_OPTAB(asn_OP_TeletexString);
VN_WEAK_OPTAB(asn_OP_UTCTime);
VN_WEAK_OPTAB(asn_OP_UTF8String);
VN_WEAK_OPTAB(asn_OP_UniversalString);
VN_WEAK_OPTAB(asn_OP_VideotexString);
VN_WEAK_OPTAB(asn_OP_VisibleString);

/*
 * The same treatment for the skeleton helper functions the handlers call.
 *
 * Each stub is unreachable in practice: a handler only runs when a descriptor's
 * op matches the corresponding operation table, and that table is defined in
 * the very skeleton that also defines these functions. If the skeleton is
 * absent, the type cannot occur in the data either. The stubs exist purely so
 * the link succeeds.
 */

#include <INTEGER.h>
#include <OBJECT_IDENTIFIER.h>
#include "RELATIVE-OID.h"

__attribute__((weak)) int
asn_INTEGER2imax(const INTEGER_t *i, intmax_t *l) {
    (void)i;
    (void)l;
    return -1;
}

__attribute__((weak)) int
asn_INTEGER2long(const INTEGER_t *i, long *l) {
    (void)i;
    (void)l;
    return -1;
}

__attribute__((weak)) const asn_INTEGER_enum_map_t *
INTEGER_map_value2enum(const asn_INTEGER_specifics_t *specs, long value) {
    (void)specs;
    (void)value;
    return 0;
}

__attribute__((weak)) ssize_t
OBJECT_IDENTIFIER_get_arcs(const OBJECT_IDENTIFIER_t *oid, asn_oid_arc_t *arcs,
                           size_t arc_slots) {
    (void)oid;
    (void)arcs;
    (void)arc_slots;
    return -1;
}

__attribute__((weak)) ssize_t
RELATIVE_OID_get_arcs(const RELATIVE_OID_t *roid, asn_oid_arc_t *arcs,
                      size_t arc_slots) {
    (void)roid;
    (void)arcs;
    (void)arc_slots;
    return -1;
}

/*
 * Weak fallback for the annotation table.
 *
 * A consumer that wants identifier output links the file produced by
 * vn-annotate, whose strong definition overrides this one. Without it the table
 * is empty and the encoder emits the numeric forms, which are equally valid
 * X.680. Same mechanism as the operation tables above, for the same reason.
 */
#include <vn_encoder.h>

__attribute__((weak)) const vn_annotations_t vn_generated_annotations = { 0, 0 };

/* Skeleton functions and descriptors the reader needs, same reasoning as above:
 * a generated directory only contains what its schema uses. */

#include <asn_SET_OF.h>

__attribute__((weak)) int
asn_set_add(void *asn_set_of_x, void *ptr) {
    (void)asn_set_of_x;
    (void)ptr;
    return -1;
}

__attribute__((weak)) int
asn_long2INTEGER(INTEGER_t *i, long l) {
    (void)i;
    (void)l;
    return -1;
}

__attribute__((weak)) int
asn_INTEGER2ulong(const INTEGER_t *i, unsigned long *l) {
    (void)i;
    (void)l;
    return -1;
}

__attribute__((weak)) int
OBJECT_IDENTIFIER_set_arcs(OBJECT_IDENTIFIER_t *oid, const asn_oid_arc_t *arcs,
                           size_t arc_slots) {
    (void)oid;
    (void)arcs;
    (void)arc_slots;
    return -1;
}

__attribute__((weak)) int
RELATIVE_OID_set_arcs(RELATIVE_OID_t *roid, const asn_oid_arc_t *arcs,
                      size_t arcs_count) {
    (void)roid;
    (void)arcs;
    (void)arcs_count;
    return -1;
}

__attribute__((weak)) asn_TYPE_descriptor_t asn_DEF_INTEGER;
