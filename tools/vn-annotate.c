/*
 * vn-annotate.c -- generate a vn_annotations_t table from asn1c's headers.
 *
 * asn1c does not keep INTEGER named numbers or BIT STRING named bit lists in the
 * runtime descriptors, and for INTEGER it must not: X.693 8.3.4 prohibits the
 * identifier form in XER, and asn1c's INTEGER__dump would emit <pukAppl1/>
 * instead of 1. Patching the compiler here would be wrong.
 *
 * The names do survive in the generated headers as real C enums:
 *
 *     typedef enum PINKeyReferenceValue {
 *         PINKeyReferenceValue_pinAppl1  = 1,
 *         ...
 *     } e_PINKeyReferenceValue;
 *     typedef long  PINKeyReferenceValue_t;      // named numbers
 *
 *     typedef enum Flags { Flags_keyCert = 0, ... } e_Flags;
 *     typedef BIT_STRING_t  Flags_t;             // named bits
 *
 * Those are declarations, not comments, so they are trustworthy. The trailing
 * typedef tells named bits from named numbers.
 *
 * ENUMERATED produces the same header shape as an INTEGER with named numbers, so
 * the two cannot be distinguished here. That is harmless: the encoder consults
 * the annotations only where the runtime map is absent.
 *
 * Deliberately not a source: the DEFAULT value asn1c writes into a header
 * comment. A long or oddly spaced literal makes its comment emitter overrun and
 * corrupt neighbouring members' comments, silently.
 *
 *   usage: vn-annotate <generated-dir> [more-dirs...] > vn_annotations.c
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TYPES 4096
#define MAX_VALUES 1024
#define MAX_NAME 256

typedef struct {
    char name[MAX_NAME];
    long value;
} named_value_t;

typedef struct {
    char          type_name[MAX_NAME];
    named_value_t values[MAX_VALUES];
    size_t        count;
    int           is_bit_string;
    int           base_seen;
} type_names_t;

static type_names_t types[MAX_TYPES];
static size_t       type_count;

static type_names_t *
find_type(const char *name) {
    size_t i;
    for(i = 0; i < type_count; i++)
        if(strcmp(types[i].type_name, name) == 0) return &types[i];
    return 0;
}

static char *
skip_ws(char *p) {
    while(*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Copy an identifier, returning the count of characters taken. */
static size_t
take_ident(const char *p, char *out, size_t outsz) {
    size_t n = 0;
    while(p[n] && (isalnum((unsigned char)p[n]) || p[n] == '_')) {
        if(n + 1 < outsz) out[n] = p[n];
        n++;
    }
    out[n < outsz ? n : outsz - 1] = '\0';
    return n;
}

/*
 * Two passes over one header.
 *
 * The enum body and the base typedef are separate declarations, and the typedef
 * follows the enum, so a single forward scan handles both in order.
 */
static void
scan_file(const char *path) {
    char          line[4096];
    FILE         *f = fopen(path, "r");
    type_names_t *cur = 0;

    if(!f) {
        fprintf(stderr, "vn-annotate: cannot open %s\n", path);
        return;
    }

    while(fgets(line, sizeof line, f)) {
        char *p = skip_ws(line);

        if(strncmp(p, "typedef enum ", 13) == 0) {
            char name[MAX_NAME];
            p = skip_ws(p + 13);
            if(!take_ident(p, name, sizeof name)) continue;
            if(find_type(name)) { /* already seen in another header */
                cur = 0;
                continue;
            }
            if(type_count >= MAX_TYPES) {
                fprintf(stderr, "vn-annotate: more than %d types\n", MAX_TYPES);
                fclose(f);
                return;
            }
            cur = &types[type_count++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->type_name, sizeof cur->type_name, "%s", name);
            continue;
        }

        if(cur) {
            /* Inside an enum body: `<Type>_<member> = <n>,` */
            size_t prefix = strlen(cur->type_name);
            if(*p == '}') {
                cur = 0; /* body ended; the base typedef comes later */
                continue;
            }
            if(strncmp(p, cur->type_name, prefix) == 0 && p[prefix] == '_') {
                char        member[MAX_NAME];
                const char *q = p + prefix + 1;
                size_t      n = take_ident(q, member, sizeof member);
                q = skip_ws((char *)q + n);
                if(*q == '=' && cur->count < MAX_VALUES) {
                    named_value_t *nv = &cur->values[cur->count];
                    snprintf(nv->name, sizeof nv->name, "%s", member);
                    nv->value = strtol(q + 1, 0, 0);
                    cur->count++;
                }
            }
            continue;
        }

        /* `typedef <base> <Type>_t;` gives the representation. */
        if(strncmp(p, "typedef ", 8) == 0) {
            char  base[MAX_NAME];
            char *rest = skip_ws(p + 8);
            size_t n = take_ident(rest, base, sizeof base);
            if(n) {
                char *tail = skip_ws(rest + n);
                char  tname[MAX_NAME];
                if(take_ident(tail, tname, sizeof tname)) {
                    size_t tl = strlen(tname);
                    if(tl > 2 && strcmp(tname + tl - 2, "_t") == 0) {
                        type_names_t *t;
                        tname[tl - 2] = '\0';
                        t = find_type(tname);
                        if(t && !t->base_seen) {
                            t->base_seen = 1;
                            t->is_bit_string =
                                (strcmp(base, "BIT_STRING_t") == 0);
                        }
                    }
                }
            }
        }
    }

    fclose(f);
}

static void
scan_dir(const char *dir) {
    DIR           *d = opendir(dir);
    struct dirent *e;

    if(!d) {
        fprintf(stderr, "vn-annotate: cannot open directory %s\n", dir);
        exit(1);
    }
    while((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        char   path[4096];
        if(n < 3 || strcmp(e->d_name + n - 2, ".h") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        scan_file(path);
    }
    closedir(d);
}

int
main(int argc, char **argv) {
    size_t i, j, emitted = 0;

    if(argc < 2) {
        fprintf(stderr,
                "usage: %s <generated-dir> [more-dirs...] > vn_annotations.c\n",
                argv[0]);
        return 2;
    }
    for(i = 1; i < (size_t)argc; i++) scan_dir(argv[i]);

    printf("/* Generated by vn-annotate. Do not edit.\n");
    printf(" *\n");
    printf(" * Identifiers that asn1c parses but does not keep in the runtime\n");
    printf(" * descriptors, recovered from the generated headers.\n");
    printf(" */\n\n");
    printf("#include <vn_encoder.h>\n\n");

    for(i = 0; i < type_count; i++) {
        if(types[i].count == 0) continue;
        printf("static const vn_named_value_t vn_nv_%s[] = {\n",
               types[i].type_name);
        for(j = 0; j < types[i].count; j++)
            printf("    { \"%s\", %ld },\n", types[i].values[j].name,
                   types[i].values[j].value);
        printf("};\n");
        emitted++;
    }

    printf("\nstatic const vn_type_names_t vn_types[] = {\n");
    for(i = 0; i < type_count; i++) {
        if(types[i].count == 0) continue;
        printf("    { \"%s\", vn_nv_%s, %lu, %d },\n", types[i].type_name,
               types[i].type_name, (unsigned long)types[i].count,
               types[i].is_bit_string);
    }
    printf("};\n\n");

    printf("const vn_annotations_t vn_generated_annotations = {\n");
    printf("    vn_types, sizeof vn_types / sizeof vn_types[0]\n");
    printf("};\n");

    fprintf(stderr, "vn-annotate: %lu type(s) with identifiers\n",
            (unsigned long)emitted);
    return 0;
}
