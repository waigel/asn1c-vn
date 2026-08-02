/*
 * vn-tree.c -- print a schema's element tree as JSON, from asn1c's own
 * runtime type descriptors.
 *
 * Documentation tooling needs to know which elements a schema can produce and
 * what they are called in XER. That is derivable from the ASN.1 source, but
 * deriving it means writing an ASN.1 parser, and a second parser is a second
 * thing that can disagree with the first. asn1c has already parsed the schema;
 * the answer is sitting in asn_TYPE_descriptor_t.
 *
 *     td->elements[i].name   the ASN.1 identifier of a SEQUENCE/SET/CHOICE member
 *     td->elements[i].type   its type descriptor
 *     td->xml_tag            the name XER writes for a value of that type
 *
 * The distinction between those last two matters and is easy to get wrong by
 * hand. A member of a SEQUENCE appears under the member's name; a member of a
 * SEQUENCE OF has no member name of its own and appears under its type's
 * xml_tag, which is why an instance contains <PINConfiguration> although no
 * field is called that. Reading it from the descriptor removes the guess.
 *
 * Recursion is real here: File contains an Fcp which contains a File. The walk
 * carries the descriptors on the current path and stops when it meets one
 * again, recording that it did rather than silently truncating.
 */

#include <stdio.h>
#include <string.h>

#include <asn_application.h>
#include <constr_TYPE.h>

#define VN_CAT_(a, b) a##b
#define VN_CAT(a, b) VN_CAT_(a, b)
#define VN_PDU_DEF VN_CAT(asn_DEF_, PDU)

extern asn_TYPE_descriptor_t VN_PDU_DEF;

#define MAX_PATH 32

static int first_record = 1;

static void
json_string(const char *s) {
    putchar('"');
    for(; s && *s; s++) {
        switch(*s) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        default:
            if((unsigned char)*s < 0x20)
                printf("\\u%04x", (unsigned char)*s);
            else
                putchar(*s);
        }
    }
    putchar('"');
}

static void
record(const char *path, const char *element, const asn_TYPE_descriptor_t *td,
       int optional, int recursive) {
    if(!first_record) fputs(",\n", stdout);
    first_record = 0;
    fputs("  {\"path\": ", stdout);
    json_string(path);
    fputs(", \"element\": ", stdout);
    json_string(element);
    fputs(", \"type\": ", stdout);
    json_string(td->name && td->name[0] ? td->name : "(anonymous)");
    printf(", \"optional\": %s", optional ? "true" : "false");
    if(recursive) fputs(", \"recursive\": true", stdout);
    putchar('}');
}

static void
walk(const asn_TYPE_descriptor_t *td, const char *path,
     const asn_TYPE_descriptor_t **seen, int depth) {
    char child[4096];
    unsigned i;

    if(depth >= MAX_PATH) return;

    for(i = 0; i < td->elements_count; i++) {
        const asn_TYPE_member_t *m = &td->elements[i];
        const asn_TYPE_descriptor_t *mt = m->type;
        const char *name;
        int recursive = 0, j;

        if(!mt) continue;

        /*
         * A SEQUENCE OF or SET OF has exactly one member and it carries no
         * name; XER writes such a value under its type's xml_tag instead.
         */
        name = (m->name && m->name[0])
                   ? m->name
                   : (mt->xml_tag ? mt->xml_tag : mt->name);
        if(!name || !name[0]) continue;

        for(j = 0; j < depth; j++)
            if(seen[j] == mt) recursive = 1;

        snprintf(child, sizeof(child), "%s%s%s", path, path[0] ? "/" : "", name);
        record(child, name, mt, m->optional != 0, recursive);

        if(recursive) continue;

        seen[depth] = mt;
        walk(mt, child, seen, depth + 1);
    }
}

int
main(int argc, char **argv) {
    const asn_TYPE_descriptor_t *seen[MAX_PATH];
    const asn_TYPE_descriptor_t *root = &VN_PDU_DEF;

    (void)argc;
    (void)argv;

    printf("{\n \"root\": ");
    json_string(root->name);
    printf(",\n \"elements\": [\n");
    seen[0] = root;
    walk(root, "", seen, 1);
    printf("\n ]\n}\n");
    return 0;
}
