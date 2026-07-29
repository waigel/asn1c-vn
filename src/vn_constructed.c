/*
 * vn_constructed.c -- value notation for composite types.
 *
 * SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE and open types. These recurse
 * through vn_encode_value() rather than knowing their members' syntax.
 */

#include "vn_internal.h"
