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
 * The C names are not the ASN.1 names: asn1c_make_identifier() rewrites every
 * character a C identifier cannot carry -- in legal ASN.1 that is only the
 * hyphen -- to an underscore, so the header says Threshold_light_red where the
 * schema says light-red. X.680 12.3 admits letters, digits and hyphens only,
 * which makes the reverse map total: every lone underscore was a hyphen. The
 * emission path maps them back; a run of underscores is asn1c's scope
 * separator for inline members (Box__mode) and is left alone. The runtime
 * descriptor names the encoder matches against keep their hyphens
 * (asn_DEF_Pe_Level.name is "Pe-Level"), so the table keys get the same
 * treatment.
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

/*
 * A C name with asn1c's identifier mangling undone: each lone underscore
 * becomes the hyphen it once was, a run of underscores is the inline-member
 * scope separator and stays. Returns a static buffer; one use per printf.
 */
static const char *
asn1_name(const char *s) {
    static char out[MAX_NAME];
    size_t i = 0, o = 0;

    while(s[i] && o + 1 < sizeof out) {
        if(s[i] == '_' && s[i + 1] != '_' && (i == 0 || s[i - 1] != '_')) {
            out[o++] = '-';
            i++;
        } else {
            out[o++] = s[i++];
        }
    }
    out[o] = '\0';
    return out;
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
 * One forward scan over a header.
 *
 * asn1c emits the "Dependencies" enums first, then the struct, then the base
 * typedef, so the declarations that describe a type always arrive after the enum
 * that named its values and a single pass suffices.
 */
static void
scan_file(const char *path) {
    char          line[4096];
    FILE         *f = fopen(path, "r");
    type_names_t *cur = 0;
    char          in_struct[MAX_NAME];

    if(!f) {
        fprintf(stderr, "vn-annotate: cannot open %s\n", path);
        return;
    }
    in_struct[0] = '\0';

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

        /*
         * An inline member has no base typedef of its own, because it is
         * declared inside its parent's struct:
         *
         *     typedef struct Slot {
         *         BIT_STRING_t  marks;         // or  BIT_STRING_t *marks;
         *
         * so that declaration is the only statement of its representation. A
         * member whose type is a named one, or an inline BIT STRING with no
         * named bits, reaches the same line but has no entry to update, and the
         * lookup below simply finds nothing.
         */
        if(in_struct[0]) {
            if(*p == '}') {
                in_struct[0] = '\0';
            } else if(strncmp(p, "BIT_STRING_t", 12) == 0) {
                char *q = skip_ws(p + 12);
                char  member[MAX_NAME], key[MAX_NAME];
                if(*q == '*') q = skip_ws(q + 1);
                if(take_ident(q, member, sizeof member)) {
                    type_names_t *t;
                    snprintf(key, sizeof key, "%s__%s", in_struct, member);
                    t = find_type(key);
                    if(t) {
                        t->base_seen = 1;
                        t->is_bit_string = 1;
                    }
                }
            }
            continue;
        }
        if(strncmp(p, "typedef struct ", 15) == 0) {
            char *q = skip_ws(p + 15);
            take_ident(q, in_struct, sizeof in_struct);
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
            printf("    { \"%s\", %ld },\n", asn1_name(types[i].values[j].name),
                   types[i].values[j].value);
        printf("};\n");
        emitted++;
    }

    printf("\nstatic const vn_type_names_t vn_types[] = {\n");
    for(i = 0; i < type_count; i++) {
        if(types[i].count == 0) continue;
        /* The string key gets the ASN.1 spelling, to match what the runtime
         * descriptor carries; the array reference stays a C identifier. */
        printf("    { \"%s\", vn_nv_%s, %lu, %d },\n",
               asn1_name(types[i].type_name), types[i].type_name,
               (unsigned long)types[i].count, types[i].is_bit_string);
    }
    printf("};\n\n");

    printf("const vn_annotations_t vn_generated_annotations = {\n");
    printf("    vn_types, sizeof vn_types / sizeof vn_types[0]\n");
    printf("};\n");

    fprintf(stderr, "vn-annotate: %lu type(s) with identifiers\n",
            (unsigned long)emitted);
    return 0;
}
