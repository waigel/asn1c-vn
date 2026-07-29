/*
 * fuzz_read.c -- libFuzzer entry point for the reader.
 *
 * The encoder only ever saw structures asn1c had already validated. The reader
 * takes text from elsewhere, so it is the first part of this project with an
 * attack surface, and the design calls fuzzing mandatory rather than optional.
 *
 * Nothing here asserts a result: a rejection is as correct as an acceptance. What
 * is being tested is that no input crashes, reads out of bounds, or leaks — which
 * is why this is only meaningful under the sanitizers.
 *
 *   make fuzz-read            # build
 *   ./fuzz-read -runs=200000  # or -max_total_time=60
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vn_encoder.h"
#include "Nested.h"

/* A table, so identifier input is exercised as well as the numeric forms. */
static const vn_named_value_t level_names[] = {{"low", 0}, {"medium", 5}};
static const vn_type_names_t  types[] = {{"Level", level_names, 2, 0}};
static const vn_annotations_t annotations = {types, 1};

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    void             *st = 0;
    vn_read_options_t ro;
    char              reason[256];

    memset(&ro, 0, sizeof ro);
    /* The final byte steers the options, so flag combinations get explored
     * without needing a separate corpus per setting. */
    if(size > 0) {
        if(data[size - 1] & 1u) ro.flags |= VN_RF_EOF;
        if(data[size - 1] & 2u) ro.flags |= VN_RF_LENIENT;
        if(data[size - 1] & 4u) ro.annotations = &annotations;
        size--;
    }
    ro.errbuf = reason;
    ro.errlen = sizeof reason;

    (void)vn_decode(0, &asn_DEF_Nested, &st, &ro, data, size);

    /* The caller owns the tree whatever the outcome; a leak here is a finding. */
    if(st) ASN_STRUCT_FREE(asn_DEF_Nested, st);
    return 0;
}
