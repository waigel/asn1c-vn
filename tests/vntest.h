/*
 * vntest.h -- minimal assertion harness for the encoder tests.
 */
#ifndef VNTEST_H
#define VNTEST_H

#include <stdio.h>
#include <stddef.h>
#include "vn_encoder.h"

extern int         vnt_failures;
extern const char *vnt_case;

#define VNT_CASE(name) (vnt_case = (name))

#define VNT_TRUE(cond)                                                       \
    do {                                                                     \
        if(!(cond)) {                                                        \
            fprintf(stderr, "FAIL [%s] %s:%d: %s\n", vnt_case, __FILE__,     \
                    __LINE__, #cond);                                        \
            vnt_failures++;                                                  \
        }                                                                    \
    } while(0)

#define VNT_STREQ(got, want) vnt_streq(__FILE__, __LINE__, (got), (want))

void vnt_streq(const char *file, int line, const char *got, const char *want);
int  vnt_report(const char *suite); /* prints a summary, returns an exit code */

/* Growable byte buffer, shared by the capture helpers and the XER check. */
typedef struct { char *buf; size_t len, cap; } vnt_str_t;
int vnt_append(vnt_str_t *s, const void *data, size_t size);

/*
 * Encode into a heap buffer. Returns a NUL-terminated string the caller must
 * free(), or NULL when vn_encode() failed. On failure reason[] holds the text
 * vn_encode wrote to its errbuf.
 */
char *vnt_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                 const vn_options_t *opts, char *reason, size_t reasonlen);

/* Encode expecting failure. Returns 1 when it failed as intended. */
int vnt_encode_fails(const asn_TYPE_descriptor_t *td, const void *sptr,
                     const vn_options_t *opts, char *reason, size_t reasonlen);

#endif /* VNTEST_H */
