/*
 * vnscan.h -- a test-only scanner over ASN.1 value notation.
 *
 * This is deliberately not a parser: it knows nothing of any schema. It decides
 * whether text is structurally well-formed value notation, and it pulls out the
 * scalar values in document order. Together with the XER cross-check those two
 * cover what a golden file cannot: structure, and semantics.
 */
#ifndef VNSCAN_H
#define VNSCAN_H

#include <stddef.h>

/*
 * Returns 1 when `text` is well-formed value notation, else 0 with a reason in
 * err. Checks brace balance, comma placement, string termination and that every
 * `:` is followed by a value.
 */
int vn_scan_wellformed(const char *text, char *err, size_t errlen);

/*
 * Extract scalar values in document order. Field names and CHOICE alternative
 * names are not scalars; numbers, identifiers used as values, cstrings,
 * hstrings and bstrings are.
 *
 * A brace group containing two or more bare numbers and no commas is an
 * OBJECT IDENTIFIER arc list, and is emitted as a single space-separated
 * scalar. `{ 1, 2, 3 }` keeps its commas and stays three scalars.
 *
 * On success returns 1, sets *count, and fills out[] with strings the caller
 * must free(). On failure returns 0 with a reason in err.
 */
int vn_scan_scalars(const char *text, char **out, size_t max, size_t *count,
                    char *err, size_t errlen);

#endif /* VNSCAN_H */
