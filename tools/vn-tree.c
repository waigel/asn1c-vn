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
 * A schema may recurse -- a Tree whose children are Trees -- so the walk
 * carries the descriptors on the current path and stops when it meets one
 * again, recording that it did rather than silently truncating. The check
 * suite proves the guard on tests/schemas/tree.asn1, the one schema here that
 * recurses, where it must fire exactly once.
 *
 * An earlier version of this comment claimed the eUICC schema itself recurses,
 * "File contains an Fcp which contains a File". It does not: Fcp is a flat
 * SEQUENCE of tagged OCTET STRINGs, the graph of the 86 types reachable from
 * ProfileElement has no cycle, and on that schema the guard never fires. The
 * claim survived here for weeks because nothing exercised it in either
 * direction -- which is why the test schema above exists.
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

        /*
         * Except when that type is an inline CHOICE, which has no tag of its
         * own: "File ::= SEQUENCE OF CHOICE { fileDescriptor Fcp, ... }" puts
         * fileDescriptor directly inside the File, and an instance reads
         * <adf-usim><fileDescriptor>, not <adf-usim><CHOICE><fileDescriptor>.
         * Descend without adding a step so the paths match real documents.
         */
        if((!m->name || !m->name[0]) && mt->elements_count
           && (!mt->name || !mt->name[0] || !strcmp(mt->name, "CHOICE"))) {
            for(j = 0; j < depth; j++)
                if(seen[j] == mt) recursive = 1;
            if(recursive) continue;
            seen[depth] = mt;
            walk(mt, path, seen, depth + 1);
            continue;
        }

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
