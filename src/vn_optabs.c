/*
 * vn_optabs.c -- weak fallback definitions for asn1c's operation tables.
 *
 * asn1c copies only the skeletons a schema actually uses into its output
 * directory: a schema with no BOOLEAN yields no BOOLEAN.c and no BOOLEAN.h.
 * The type dispatch in vn_encoder.c nevertheless names every operation table,
 * so without help the link would fail against any real generated directory.
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
