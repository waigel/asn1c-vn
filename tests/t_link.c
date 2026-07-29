/*
 * t_link.c -- proves the module compiles and links against asn1c-generated
 * code, and that a type with no handler fails loudly instead of emitting
 * anything.
 *
 * This also guards the weak-operation-table arrangement in src/vn_optabs.c:
 * prim.asn1 deliberately omits most ASN.1 types, so this binary links against
 * a generated directory that lacks most skeletons.
 */
#include <stdlib.h>
#include "vntest.h"
#include "Holder.h"

int
main(void) {
    Holder_t *h = (Holder_t *)calloc(1, sizeof(*h));
    char reason[160];

    VNT_CASE("descriptor is reachable");
    VNT_TRUE(asn_DEF_Holder.op != 0);
    VNT_TRUE(asn_DEF_Holder.elements_count == 8);

    VNT_CASE("a type with no handler fails loudly");
    VNT_TRUE(vnt_encode_fails(&asn_DEF_Holder, h, 0, reason, sizeof reason));
    VNT_TRUE(reason[0] != '\0');

    ASN_STRUCT_FREE(asn_DEF_Holder, h);
    return vnt_report("t_link");
}
