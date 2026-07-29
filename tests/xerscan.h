/*
 * xerscan.h -- test-only scanner over asn1c's XER output, plus a normaliser
 * that maps either dialect's scalars onto one comparable form.
 */
#ifndef XERSCAN_H
#define XERSCAN_H

#include <stddef.h>

/*
 * Extract scalar values from XER in document order.
 *   <tag>text</tag>  -> text
 *   <tag/>           -> tag        (how asn1c writes BOOLEAN and ENUMERATED)
 *   <tag></tag>      -> ""         (an empty string or octet string)
 * A tag holding only child elements contributes nothing of its own.
 *
 * Returns 1 on success, filling out[] with strings the caller must free().
 */
int xer_scan_scalars(const char *xer, char **out, size_t max, size_t *count,
                     char *err, size_t errlen);

/*
 * Normalise one value-notation scalar into a comparable form, e.g. "B:1",
 * "H:00AABB", "S:text", "I:-5", "O:2.23.143.1". Value notation is unambiguous
 * here because it marks an hstring with H and a bstring with B.
 *
 * Returns a string the caller must free(), or NULL for a shape it does not
 * recognise. A caller must treat NULL as a failure and never as "equal": this is
 * the one place a wrong assumption could hide a real difference.
 */
char *vn_norm_scalar(const char *s);

/*
 * Normalise one XER scalar, which may be ambiguous.
 *
 * asn1c writes OCTET STRING and BIT STRING alike as bare digits, and every bit
 * string is also valid hex, so "0110" is either two octets or four bits with no
 * way to tell them apart without the schema. This therefore yields up to two
 * candidate normalisations, and a comparison succeeds if the value-notation side
 * matches either one.
 *
 * That tolerance is confined to the choice of hstring versus bstring, and cannot
 * mask a wrong *value*: the bit content still has to agree. The choice of form
 * itself is pinned separately, by the dedicated BIT STRING tests and the golden
 * files.
 *
 * Returns the number of candidates written to cands[] (0 on failure); each is a
 * string the caller must free().
 */
size_t xer_norm_candidates(const char *s, char **cands, size_t max);

#endif /* XERSCAN_H */
