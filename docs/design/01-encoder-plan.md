# asn1c-vn Implementation Plan

> Historical record: the implementation plan this encoder was built from, kept
> because its task notes explain several non-obvious decisions. Steps use
> checkbox syntax; boxes are unticked because the plan was executed in one pass
> rather than tracked.

**Goal:** A runtime addon for vlm/asn1c that serialises a decoded ASN.1 structure into X.680 value notation.

**Architecture:** A generic encoder that walks `asn_TYPE_descriptor_t` values, dispatching on the `asn_OP_*` operation-table pointer. Four source files with one responsibility each: byte sink and indentation (`vn_writer.c`), type dispatch (`vn_encoder.c`), leaf types (`vn_primitive.c`), composite types (`vn_constructed.c`). No changes to asn1c itself.

**Tech Stack:** C99, POSIX make, asn1c 0.9.29 runtime skeletons. No third-party dependencies. Tests are C programs; no Python.

**Spec:** `docs/design/01-encoder.md`

## Global Constraints

- Language: **C99** (`-std=c99`), compiled clean under `-Wall -Wextra`.
- Runtime ABI pinned to **asn1c 0.9.29** (tested tree `v0.9.29-7-g8a274c3f`). `asn1c` must be on `PATH` for tests.
- No dependencies beyond asn1c's generated skeletons and libc (+ `-lm`, which the skeletons need).
- **All repository content in English** — code, comments, commit messages, docs.
- On macOS, `-D_DARWIN_C_SOURCE` is required; `GeneralizedTime.c` needs `struct tm` and `timegm`.
- Public symbols are prefixed `vn_` / `VN_`; internal ones `vn_` with `static` where possible.
- License: BSD-2-Clause, matching asn1c.
- Never emit output that is not valid X.680 value notation, with exactly one documented exception (bare `ANY` as hex, spec §5.2). Every unsupported case sets `encoded = -1` and writes a reason.
- Commit after every task.

## Deviation from spec §7, recorded here

The spec describes the XER cross-check as comparing sequences of **(path, scalar) pairs**. Building comparable paths is not viable without duplicating schema knowledge in the harness, for two empirically verified reasons:

1. asn1c's XER names SEQUENCE OF elements by their **type** (`<INTEGER>`, `<Inner>`), while value notation gives list elements no name at all. Aligning them requires knowing which elements are list members — i.e. schema awareness in a tokeniser meant to be schema-free.
2. asn1c's XER wraps long OCTET STRING and BIT STRING values across lines with embedded spaces (`45 00 F8 8A 3E B2 0A B4 01`), so scalar text needs whitespace-stripping normalisation regardless.

The oracle is therefore split into two cheaper checks with the same combined coverage:

- **Scalar sequence equality** (Task 12) — both encoders visit members in descriptor order, so the ordered sequence of scalar values must match exactly. Catches wrong values, wrong order, missing and extra members.
- **VN well-formedness scan** (Task 11) — an independent scanner verifies brace balance, comma placement and `alternative :` form, catching the structural errors that a scalar-only comparison cannot see.

Field-name correctness is covered by golden files (Task 11); names come verbatim from `td->elements[i].name`, so they are low-risk.

Also correct the spec's normalisation table: XER OCTET STRING is space-separated hex pairs, not contiguous.

## Amendment found during Task 1: asn1c ships only the skeletons a schema uses

Verified empirically: a generated directory for a schema without BOOLEAN contains no `BOOLEAN.c` **and no `BOOLEAN.h`**. Same for `REAL`, `ENUMERATED`, `UTF8String` and every other unused type. This breaks the naive dispatch design in two separate ways:

1. **Link failure.** A table naming all 35 `asn_OP_*` globals cannot link against a minimal generated directory. Weak *references* (`__attribute__((weak))`, `weak_import`) do not fix this on Mach-O, where they only work for dylibs — tested and rejected.
2. **Compile failure.** `#include <BOOLEAN.h>` fails for the same reason.

The fix, verified on clang/arm64:

- **Compile** `src/*.c` against asn1c's *installed skeleton directory* (`SKELDIR`, default `$(dirname $(command -v asn1c))/../share/asn1c`), which always holds the complete header set. The library needs no headers from `GEN_DIR` at all. Compiling against installed headers while linking against a generated directory's objects is safe precisely because the ABI is pinned to one asn1c version.
- **Link** with `src/vn_optabs.c` providing a **weak definition** of every `asn_OP_*` table. A strong definition from a real skeleton always overrides a weak one, so a present type dispatches exactly as before. An absent type resolves to a distinct zero-filled dummy that no descriptor can point at, so it simply never matches.

`src/vn_optabs.c` must stay a separate translation unit; a weak definition in the same unit as its reference can be resolved at compile time and lose the override. The weak attribute needs GCC or clang, which is stated in the README.

**Task 8 found this applies to skeleton *functions* too**, not only the operation tables: `OBJECT_IDENTIFIER_get_arcs` and `RELATIVE_OID_get_arcs` are missing whenever their types are unused. `src/vn_optabs.c` therefore also carries weak stubs for every skeleton function the handlers call — `asn_INTEGER2imax`, `asn_INTEGER2long`, `INTEGER_map_value2enum`, and the two arc readers. Each stub is unreachable in practice: a handler only runs when a descriptor's `op` matches an operation table defined in the same skeleton that defines these functions. That the override works is verified by the enumerated-identifier test, which needs the real `INTEGER_map_value2enum`; the stub returns NULL and would fail it.

Consequences for the tasks below: Task 1 also creates `src/vn_optabs.c` and the `SKELDIR` machinery, and its dispatch table is present-but-empty. Handlers in later tasks include per-type headers freely, since `SKELDIR` guarantees them.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `include/vn_encoder.h` | Public API: `vn_mode_e`, `vn_options_t`, `VN_F_*`, `vn_encode`, `vn_fprint` |
| `src/vn_internal.h` | `vn_writer_t`, writer helpers, `vn_encode_value` — shared by src/*.c only |
| `src/vn_writer.c` | Byte sink, indentation, mode policy, comments, error capture. Knows no ASN.1 types. |
| `src/vn_encoder.c` | `td->op` → handler table, `vn_encode`, `vn_fprint`. Knows types, not their syntax. |
| `src/vn_primitive.c` | BOOLEAN, NULL, INTEGER (incl. bignum), ENUMERATED, OCTET STRING, BIT STRING, OID, strings, times, ANY |
| `src/vn_constructed.c` | SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE, OPEN TYPE |
| `tools/asn1vn.c` | CLI: DER on stdin → VN on stdout |
| `tests/vntest.h`, `tests/vntest.c` | Assertion macros, buffer capture, pass/fail reporting |
| `tests/vnscan.h`, `tests/vnscan.c` | VN well-formedness scanner + scalar extractor (test-only) |
| `tests/xerscan.h`, `tests/xerscan.c` | XER scalar extractor (test-only) |
| `tests/t_*.c` | One test binary per area |
| `tests/schemas/*.asn1` | Test schemas, one per type family plus a kitchen sink |
| `tests/golden/*.vn` | Expected output per schema per mode |

---

## Task 1: Repository skeleton, build system, test harness

Delivers a repo that builds an empty-but-linked encoder and runs a passing test suite. Proves the ABI assumption before any encoding logic exists.

**Files:**
- Create: `LICENSE`, `Makefile`, `include/vn_encoder.h`, `src/vn_internal.h`, `src/vn_encoder.c`, `src/vn_writer.c`, `src/vn_primitive.c`, `src/vn_constructed.c`
- Create: `tests/vntest.h`, `tests/vntest.c`, `tests/schemas/prim.asn1`, `tests/t_link.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `vn_mode_e`, `vn_options_t`, `VN_F_LENIENT|VN_F_ENUM_WITH_VALUE|VN_F_STRICT_ANY`, `vn_encode()`, `vn_fprint()`, `vn_writer_t`, and the test helpers `vnt_streq()`, `vnt_report()`, `vnt_encode()`.

- [ ] **Step 1: Write `LICENSE`** — BSD-2-Clause, copyright `2026 Johannes Waigel`.

- [ ] **Step 2: Write the public header**

```c
/* include/vn_encoder.h */
#ifndef VN_ENCODER_H
#define VN_ENCODER_H

#include <stdio.h>
#include <stddef.h>
#include <asn_application.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output flavour. All three emit valid X.680 value notation. */
typedef enum {
    VN_MODE_PRETTY = 0, /* for reading: 4-space indent, hex wrapped */
    VN_MODE_CANONICAL,  /* for diffing: fixed 2-space indent, no wrapping */
    VN_MODE_ANNOTATED   /* pretty + X.680 comments */
} vn_mode_e;

#define VN_F_LENIENT         0x01u /* emit questionable values instead of failing */
#define VN_F_ENUM_WITH_VALUE 0x02u /* `green -- (1) --` instead of `green` */
#define VN_F_STRICT_ANY      0x04u /* fail on bare ANY instead of emitting hex */

typedef struct vn_options_s {
    vn_mode_e mode;
    int       indent_width; /* 0 = mode default (4); ignored when CANONICAL */
    int       line_width;   /* 0 = mode default (76); ignored when CANONICAL */
    unsigned  flags;
    char     *errbuf;       /* optional; receives a human-readable reason */
    size_t    errlen;
} vn_options_t;

/*
 * Serialise *sptr, an instance of *td, as ASN.1 value notation.
 * Same shape as xer_encode(): bytes leave through the consume callback so the
 * destination (file, buffer, socket) is the caller's business.
 * opts may be NULL, meaning pretty defaults.
 */
asn_enc_rval_t vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                         const vn_options_t *opts,
                         asn_app_consume_bytes_f *cb, void *key);

/* Convenience: write straight to a stream. 0 on success, -1 on failure. */
int vn_fprint(FILE *stream, const asn_TYPE_descriptor_t *td, const void *sptr,
              const vn_options_t *opts);

#ifdef __cplusplus
}
#endif
#endif /* VN_ENCODER_H */
```

- [ ] **Step 3: Write the internal header**

```c
/* src/vn_internal.h -- shared by src/*.c only, not installed */
#ifndef VN_INTERNAL_H
#define VN_INTERNAL_H

#include <sys/types.h>
#include "vn_encoder.h"

typedef struct vn_writer_s {
    asn_app_consume_bytes_f *cb;
    void       *key;
    vn_mode_e   mode;
    int         indent_width;
    int         line_width;
    unsigned    flags;
    char       *errbuf;
    size_t      errlen;
    size_t      written;   /* bytes handed to cb so far */
    int         failed;    /* sticky */
    const asn_TYPE_descriptor_t *failed_td;
    const void                  *failed_sptr;
} vn_writer_t;

/* vn_writer.c */
void vn_writer_init(vn_writer_t *w, const vn_options_t *opts,
                    asn_app_consume_bytes_f *cb, void *key);
int  vn_put(vn_writer_t *w, const char *s, size_t len);
int  vn_puts(vn_writer_t *w, const char *s);
int  vn_putc(vn_writer_t *w, char c);
int  vn_printf(vn_writer_t *w, const char *fmt, ...);
int  vn_break(vn_writer_t *w, int level);     /* newline + indent for level */
int  vn_comment(vn_writer_t *w, const char *fmt, ...); /* no-op unless ANNOTATED */
int  vn_fail(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             const char *fmt, ...);           /* always returns -1 */
int  vn_is_annotated(const vn_writer_t *w);

/* vn_encoder.c */
int vn_encode_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                    const void *sptr, int level);

/* vn_primitive.c and vn_constructed.c handlers share this shape. */
typedef int (*vn_handler_f)(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                            const void *sptr, int level);

int vn_h_boolean(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);
int vn_h_null(vn_writer_t *, const asn_TYPE_descriptor_t *, const void *, int);

#endif /* VN_INTERNAL_H */
```

- [ ] **Step 4: Write stub implementations**

`src/vn_writer.c`, `src/vn_primitive.c`, `src/vn_constructed.c` may be empty apart from an `#include "vn_internal.h"` for now. `src/vn_encoder.c` gets a placeholder that proves linkage:

```c
/* src/vn_encoder.c */
#include <string.h>
#include "vn_internal.h"

int
vn_encode_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    (void)level;
    return vn_fail(w, td, sptr, "not implemented yet");
}

asn_enc_rval_t
vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
          const vn_options_t *opts, asn_app_consume_bytes_f *cb, void *key) {
    asn_enc_rval_t er;
    vn_writer_t w;

    vn_writer_init(&w, opts, cb, key);
    if(vn_encode_value(&w, td, sptr, 0) < 0) {
        er.encoded = -1;
        er.failed_type = w.failed_td ? w.failed_td : td;
        er.structure_ptr = w.failed_sptr ? w.failed_sptr : sptr;
    } else {
        er.encoded = (ssize_t)w.written;
        er.failed_type = 0;
        er.structure_ptr = 0;
    }
    return er;
}

static int
vn_write_stream(const void *buffer, size_t size, void *app_key) {
    FILE *f = (FILE *)app_key;
    return fwrite(buffer, 1, size, f) == size ? 0 : -1;
}

int
vn_fprint(FILE *stream, const asn_TYPE_descriptor_t *td, const void *sptr,
          const vn_options_t *opts) {
    asn_enc_rval_t er = vn_encode(td, sptr, opts, vn_write_stream, stream);
    return er.encoded < 0 ? -1 : 0;
}
```

Add a minimal `vn_writer_init` and `vn_fail` to `src/vn_writer.c` so this links; Task 2 replaces them with the tested versions.

```c
/* src/vn_writer.c -- minimal, replaced in Task 2 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vn_internal.h"

void
vn_writer_init(vn_writer_t *w, const vn_options_t *opts,
               asn_app_consume_bytes_f *cb, void *key) {
    memset(w, 0, sizeof(*w));
    w->cb = cb;
    w->key = key;
    w->mode = opts ? opts->mode : VN_MODE_PRETTY;
    w->indent_width = opts && opts->indent_width > 0 ? opts->indent_width : 4;
    w->line_width = opts && opts->line_width > 0 ? opts->line_width : 76;
    w->flags = opts ? opts->flags : 0u;
    w->errbuf = opts ? opts->errbuf : 0;
    w->errlen = opts ? opts->errlen : 0;
    if(w->mode == VN_MODE_CANONICAL) { w->indent_width = 2; w->line_width = 0; }
}

int
vn_fail(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
        const char *fmt, ...) {
    if(!w->failed) {
        w->failed = 1;
        w->failed_td = td;
        w->failed_sptr = sptr;
        if(w->errbuf && w->errlen) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(w->errbuf, w->errlen, fmt, ap);
            va_end(ap);
        }
    }
    return -1;
}
```

- [ ] **Step 5: Write the test harness**

```c
/* tests/vntest.h */
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
int  vnt_report(const char *suite); /* prints summary, returns exit code */

/*
 * Encode into a heap buffer. Returns a NUL-terminated string the caller must
 * free(), or NULL if vn_encode() failed. On failure, reason[] holds the text
 * vn_encode wrote to its errbuf.
 */
char *vnt_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                 const vn_options_t *opts, char *reason, size_t reasonlen);

/* Encode expecting failure. Returns 1 if it failed as intended, else 0. */
int vnt_encode_fails(const asn_TYPE_descriptor_t *td, const void *sptr,
                     const vn_options_t *opts, char *reason, size_t reasonlen);

#endif /* VNTEST_H */
```

```c
/* tests/vntest.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"

int         vnt_failures = 0;
const char *vnt_case = "(none)";

void
vnt_streq(const char *file, int line, const char *got, const char *want) {
    if(got && want && strcmp(got, want) == 0) return;
    fprintf(stderr, "FAIL [%s] %s:%d:\n  want |%s|\n  got  |%s|\n", vnt_case,
            file, line, want ? want : "(null)", got ? got : "(null)");
    vnt_failures++;
}

int
vnt_report(const char *suite) {
    if(vnt_failures) {
        fprintf(stderr, "%s: %d failure(s)\n", suite, vnt_failures);
        return 1;
    }
    printf("%s: ok\n", suite);
    return 0;
}

typedef struct { char *buf; size_t len, cap; } vnt_buf_t;

static int
vnt_consume(const void *data, size_t size, void *key) {
    vnt_buf_t *b = (vnt_buf_t *)key;
    if(b->len + size + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        char *nb;
        while(cap < b->len + size + 1) cap <<= 1;
        nb = (char *)realloc(b->buf, cap);
        if(!nb) return -1;
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, data, size);
    b->len += size;
    b->buf[b->len] = '\0';
    return 0;
}

static char *
vnt_run(const asn_TYPE_descriptor_t *td, const void *sptr,
        const vn_options_t *opts, char *reason, size_t reasonlen, int *ok) {
    vnt_buf_t b;
    vn_options_t local;
    asn_enc_rval_t er;

    memset(&b, 0, sizeof(b));
    local = opts ? *opts : (vn_options_t){VN_MODE_PRETTY, 0, 0, 0u, 0, 0};
    if(reason && reasonlen) {
        reason[0] = '\0';
        local.errbuf = reason;
        local.errlen = reasonlen;
    }
    er = vn_encode(td, sptr, &local, vnt_consume, &b);
    *ok = er.encoded >= 0;
    if(!*ok) { free(b.buf); return 0; }
    return b.buf ? b.buf : strdup("");
}

char *
vnt_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
           const vn_options_t *opts, char *reason, size_t reasonlen) {
    int ok = 0;
    return vnt_run(td, sptr, opts, reason, reasonlen, &ok);
}

int
vnt_encode_fails(const asn_TYPE_descriptor_t *td, const void *sptr,
                 const vn_options_t *opts, char *reason, size_t reasonlen) {
    int ok = 0;
    char *s = vnt_run(td, sptr, opts, reason, reasonlen, &ok);
    free(s);
    return !ok;
}
```

- [ ] **Step 6: Write the first test schema**

```asn1
-- tests/schemas/prim.asn1 -- leaf types with no composition
Prim DEFINITIONS AUTOMATIC TAGS ::= BEGIN

Colour  ::= ENUMERATED { red(0), green(1), blue(2) }
BigInt  ::= INTEGER
Holder  ::= SEQUENCE {
    flag  BOOLEAN,
    void  NULL,
    small INTEGER,
    big   BigInt,
    col   Colour,
    data  OCTET STRING
}

END
```

- [ ] **Step 7: Write the linkage test**

```c
/* tests/t_link.c -- proves the module links against generated code and that
 * an unimplemented type fails loudly rather than emitting anything. */
#include <stdlib.h>
#include "vntest.h"
#include "Holder.h"

int
main(void) {
    Holder_t *h = calloc(1, sizeof(*h));
    char reason[128];

    VNT_CASE("descriptor is reachable");
    VNT_TRUE(asn_DEF_Holder.op != 0);
    VNT_TRUE(asn_DEF_Holder.elements_count == 6);

    VNT_CASE("unimplemented type fails loudly");
    VNT_TRUE(vnt_encode_fails(&asn_DEF_Holder, h, 0, reason, sizeof reason));
    VNT_TRUE(reason[0] != '\0');

    ASN_STRUCT_FREE(asn_DEF_Holder, h);
    return vnt_report("t_link");
}
```

- [ ] **Step 8: Write the Makefile**

```make
# asn1c-vn -- ASN.1 value notation encoder for vlm/asn1c
ASN1C  ?= asn1c
CC     ?= cc
CFLAGS ?= -O2 -g
STD    := -std=c99
WARN   := -Wall -Wextra -Wno-unused-parameter

VN_SRCS := src/vn_writer.c src/vn_encoder.c src/vn_primitive.c src/vn_constructed.c
VN_INC  := -Iinclude -Isrc

EXTRA :=
ifeq ($(shell uname -s),Darwin)
EXTRA += -D_DARWIN_C_SOURCE
endif

# ---- library ---------------------------------------------------------------
# Compiled against a generated directory's headers; the result is reusable
# across any asn1c 0.9.29 output.
GEN_DIR ?=
PDU     ?=

libvn.a: $(VN_SRCS)
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	$(CC) $(STD) $(WARN) $(CFLAGS) $(EXTRA) $(VN_INC) -I$(GEN_DIR) -c $(VN_SRCS)
	ar rcs $@ vn_writer.o vn_encoder.o vn_primitive.o vn_constructed.o
	rm -f vn_writer.o vn_encoder.o vn_primitive.o vn_constructed.o

# ---- CLI ------------------------------------------------------------------
asn1vn: tools/asn1vn.c $(VN_SRCS)
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	$(CC) $(STD) $(WARN) $(CFLAGS) $(EXTRA) $(VN_INC) -I$(GEN_DIR) \
	    -DPDU=$(PDU) $^ \
	    $(filter-out $(GEN_DIR)/converter-example.c,$(wildcard $(GEN_DIR)/*.c)) \
	    -o $@ -lm

# ---- tests ----------------------------------------------------------------
SCHEMAS := prim
TESTS   := t_link

# asn1c writes into the current directory, so generate inside the target dir.
tests/gen/%/.stamp: tests/schemas/%.asn1
	rm -rf tests/gen/$*
	mkdir -p tests/gen/$*
	cd tests/gen/$* && $(ASN1C) -fcompound-names -no-gen-example \
	    $(abspath $<) >/dev/null
	touch $@

# Every test binary named t_<x> builds against tests/gen/<x>; t_link uses prim.
t_link_SCHEMA := prim

define TEST_RULE
tests/bin/$(1): tests/$(1).c tests/vntest.c $(VN_SRCS) tests/gen/$$($(1)_SCHEMA)/.stamp
	mkdir -p tests/bin
	$$(CC) $$(STD) $$(WARN) $$(CFLAGS) $$(EXTRA) $$(VN_INC) -Itests \
	    -Itests/gen/$$($(1)_SCHEMA) \
	    tests/$(1).c tests/vntest.c $$(VN_SRCS) \
	    $$(wildcard tests/gen/$$($(1)_SCHEMA)/*.c) -o $$@ -lm
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

check: $(addprefix tests/bin/,$(TESTS))
	@rc=0; for t in $(addprefix tests/bin/,$(TESTS)); do \
	    ./$$t || rc=1; \
	done; exit $$rc

clean:
	rm -rf tests/bin tests/gen libvn.a asn1vn *.o

.PHONY: check clean
```

- [ ] **Step 9: Run the test to verify it passes**

Run: `make check`
Expected: `asn1c` generates `tests/gen/prim/`, `tests/bin/t_link` builds with no warnings, and prints `t_link: ok`. If `asn_DEF_Holder.elements_count` is not 6, the schema and test disagree — fix the test, not the schema.

- [ ] **Step 10: Commit**

```bash
git add LICENSE Makefile include src tests
git commit -m "build: repository skeleton with linkage proof

Public API and a stub encoder that fails loudly, plus a C test harness
and the first generated-code build. Proves the asn1c 0.9.29 runtime ABI
assumption before any encoding logic is written."
```

---

## Task 2: Writer — output sink, indentation, modes, errors

**Files:**
- Modify: `src/vn_writer.c` (replace the minimal version)
- Create: `tests/t_writer.c`
- Modify: `Makefile` (add `t_writer` to `TESTS`, `t_writer_SCHEMA := prim`)

**Interfaces:**
- Consumes: `vn_writer_t`, `vn_writer_init`, `vn_fail` from Task 1.
- Produces: `vn_put`, `vn_puts`, `vn_putc`, `vn_printf`, `vn_break`, `vn_comment`, `vn_is_annotated`. `vn_break(w, level)` writes `"\n"` followed by `level * w->indent_width` spaces. `vn_comment` is a no-op unless mode is `VN_MODE_ANNOTATED`. All return `0` on success and `-1` once the writer has failed; every one is a no-op after the first failure, so callers may defer checking.

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_writer.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vn_internal.h"

typedef struct { char buf[512]; size_t len; } sink_t;

static int
sink_consume(const void *data, size_t size, void *key) {
    sink_t *s = (sink_t *)key;
    if(s->len + size >= sizeof(s->buf)) return -1;
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return 0;
}

static int refusing_consume(const void *d, size_t n, void *k) {
    (void)d; (void)n; (void)k; return -1;
}

int
main(void) {
    sink_t s;
    vn_writer_t w;
    vn_options_t o;
    char reason[128];

    /* pretty: 4-space indent by default */
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, sink_consume, &s);
    VNT_CASE("pretty break indents 4 per level");
    vn_puts(&w, "{");
    vn_break(&w, 1);
    vn_puts(&w, "a 1");
    vn_break(&w, 0);
    vn_puts(&w, "}");
    VNT_STREQ(s.buf, "{\n    a 1\n}");
    VNT_TRUE(w.written == strlen("{\n    a 1\n}"));

    /* canonical: fixed 2-space indent, ignores indent_width */
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.mode = VN_MODE_CANONICAL;
    o.indent_width = 9;
    o.line_width = 20;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_CASE("canonical ignores indent_width and line_width");
    vn_break(&w, 2);
    vn_puts(&w, "x");
    VNT_STREQ(s.buf, "\n    x");
    VNT_TRUE(w.indent_width == 2);
    VNT_TRUE(w.line_width == 0);

    /* comments only in annotated mode */
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.mode = VN_MODE_PRETTY;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_CASE("pretty suppresses comments");
    VNT_TRUE(vn_is_annotated(&w) == 0);
    vn_comment(&w, "Type %s", "Holder");
    VNT_STREQ(s.buf, "");

    memset(&s, 0, sizeof s);
    o.mode = VN_MODE_ANNOTATED;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_CASE("annotated emits inline comment form");
    VNT_TRUE(vn_is_annotated(&w) == 1);
    vn_comment(&w, "Type %s", "Holder");
    VNT_STREQ(s.buf, "-- Type Holder --");

    /* printf */
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, sink_consume, &s);
    VNT_CASE("printf formats and counts");
    vn_printf(&w, "%d/%s", 42, "x");
    VNT_STREQ(s.buf, "42/x");
    VNT_TRUE(w.written == 4);

    /* sink failure is sticky and suppresses later output */
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, refusing_consume, &s);
    VNT_CASE("sink failure is sticky");
    VNT_TRUE(vn_puts(&w, "a") == -1);
    VNT_TRUE(w.failed == 1);
    VNT_TRUE(vn_puts(&w, "b") == -1);
    VNT_STREQ(s.buf, "");

    /* vn_fail records the first reason only */
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.errbuf = reason;
    o.errlen = sizeof reason;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_CASE("first failure reason wins");
    VNT_TRUE(vn_fail(&w, 0, 0, "first %d", 1) == -1);
    VNT_TRUE(vn_fail(&w, 0, 0, "second") == -1);
    VNT_STREQ(reason, "first 1");

    return vnt_report("t_writer");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: link error for `vn_put`, `vn_puts`, `vn_putc`, `vn_printf`, `vn_break`, `vn_comment`, `vn_is_annotated`.

- [ ] **Step 3: Implement the writer**

Append to `src/vn_writer.c` (keep `vn_writer_init` and `vn_fail` from Task 1):

```c
int
vn_put(vn_writer_t *w, const char *s, size_t len) {
    if(w->failed) return -1;
    if(len == 0) return 0;
    if(w->cb(s, len, w->key) < 0)
        return vn_fail(w, w->failed_td, w->failed_sptr, "output callback failed");
    w->written += len;
    return 0;
}

int vn_puts(vn_writer_t *w, const char *s) { return vn_put(w, s, strlen(s)); }
int vn_putc(vn_writer_t *w, char c)        { return vn_put(w, &c, 1); }

int
vn_printf(vn_writer_t *w, const char *fmt, ...) {
    char scratch[128];
    va_list ap;
    int n;

    if(w->failed) return -1;
    va_start(ap, fmt);
    n = vsnprintf(scratch, sizeof scratch, fmt, ap);
    va_end(ap);
    if(n < 0 || (size_t)n >= sizeof scratch)
        return vn_fail(w, w->failed_td, w->failed_sptr,
                       "internal: formatted value exceeds %zu bytes",
                       sizeof scratch);
    return vn_put(w, scratch, (size_t)n);
}

int
vn_break(vn_writer_t *w, int level) {
    static const char spaces[16] = "                ";
    int n = level * w->indent_width;

    if(vn_putc(w, '\n') < 0) return -1;
    while(n > 0) {
        int chunk = n > (int)sizeof spaces ? (int)sizeof spaces : n;
        if(vn_put(w, spaces, (size_t)chunk) < 0) return -1;
        n -= chunk;
    }
    return 0;
}

int vn_is_annotated(const vn_writer_t *w) {
    return w->mode == VN_MODE_ANNOTATED;
}

int
vn_comment(vn_writer_t *w, const char *fmt, ...) {
    char scratch[192];
    va_list ap;
    int n;

    if(w->failed || !vn_is_annotated(w)) return w->failed ? -1 : 0;
    va_start(ap, fmt);
    n = vsnprintf(scratch, sizeof scratch, fmt, ap);
    va_end(ap);
    if(n < 0) return vn_fail(w, 0, 0, "internal: bad comment format");
    if(vn_puts(w, "-- ") < 0) return -1;
    /* A comment must not contain "--"; X.680 11.6 ends the comment there. */
    {
        char *p;
        for(p = scratch; *p; p++)
            if(p[0] == '-' && p[1] == '-') p[0] = '~';
    }
    if(vn_puts(w, scratch) < 0) return -1;
    return vn_puts(w, " --");
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_writer: ok` and `t_link: ok`.

- [ ] **Step 5: Commit**

```bash
git add src/vn_writer.c tests/t_writer.c Makefile
git commit -m "feat: output writer with mode-aware indentation and sticky errors

The writer knows about bytes, indentation and comments but nothing about
ASN.1 types. Failures are sticky so handlers can defer error checks."
```

---

## Task 3: Dispatch table and loud failure for every unhandled type

**Files:**
- Modify: `src/vn_encoder.c`
- Modify: `src/vn_primitive.c` (add BOOLEAN and NULL, the two simplest handlers)
- Create: `tests/t_dispatch.c`
- Modify: `Makefile` (`TESTS += t_dispatch`, `t_dispatch_SCHEMA := prim`)

**Interfaces:**
- Consumes: writer helpers from Task 2.
- Produces: `vn_encode_value()` dispatching on `td->op`; handlers `vn_h_boolean`, `vn_h_null`. Later tasks add entries to the table in `src/vn_encoder.c` and handler bodies in `vn_primitive.c` / `vn_constructed.c`.

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_dispatch.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"

int
main(void) {
    char reason[160];
    char *out;
    BOOLEAN_t b;
    NULL_t nil = 0;

    VNT_CASE("BOOLEAN true");
    b = 1;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "TRUE");
    free(out);

    VNT_CASE("BOOLEAN false is any zero value");
    b = 0;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "FALSE");
    free(out);

    VNT_CASE("BOOLEAN nonzero other than 1 is still TRUE");
    b = 0xff;
    out = vnt_encode(&asn_DEF_BOOLEAN, &b, 0, reason, sizeof reason);
    VNT_STREQ(out, "TRUE");
    free(out);

    VNT_CASE("NULL");
    out = vnt_encode(&asn_DEF_NULL, &nil, 0, reason, sizeof reason);
    VNT_STREQ(out, "NULL");
    free(out);

    VNT_CASE("unhandled type names itself in the reason");
    VNT_TRUE(vnt_encode_fails(&asn_DEF_REAL, 0, 0, reason, sizeof reason));
    VNT_TRUE(strstr(reason, "REAL") != 0);

    VNT_CASE("failure identifies the type via failed_type");
    {
        asn_enc_rval_t er = vn_encode(&asn_DEF_REAL, 0, 0, 0, 0);
        VNT_TRUE(er.encoded == -1);
        VNT_TRUE(er.failed_type == &asn_DEF_REAL);
    }

    return vnt_report("t_dispatch");
}
```

Note: `asn_DEF_REAL` requires `REAL.c` in the generated directory. `prim.asn1` has no REAL, so add a `real REAL OPTIONAL` member to `Holder` — asn1c then generates `REAL.c`. Do that in this task and update `t_link`'s `elements_count` assertion from 6 to 7.

Also note the last case passes `cb = 0`: `vn_encode` must reach the dispatch and fail on the type before ever calling the callback. If it crashes, the dispatch is happening after a write.

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: `t_dispatch` fails — `vn_encode_value` currently answers "not implemented yet" for everything, so the BOOLEAN case reports `want |TRUE| got |(null)|`.

- [ ] **Step 3: Implement dispatch and the two handlers**

Replace `vn_encode_value` in `src/vn_encoder.c`:

```c
#include <BOOLEAN.h>
#include <NULL.h>

/*
 * asn1c 0.9.29 gives every built-in type its own operation table, so the type
 * is identified by comparing td->op against those globals. This is the one
 * place that depends on the asn1c version; see README "ABI pinning".
 */
static const struct vn_dispatch_s {
    const asn_TYPE_operation_t *op;
    vn_handler_f                handler;
    const char                 *label; /* for the "unsupported" message */
} vn_dispatch[] = {
    { &asn_OP_BOOLEAN, vn_h_boolean, "BOOLEAN" },
    { &asn_OP_NULL,    vn_h_null,    "NULL"    },
};

int
vn_encode_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    size_t i;

    if(w->failed) return -1;
    if(!td) return vn_fail(w, td, sptr, "internal: null type descriptor");
    if(!td->op) return vn_fail(w, td, sptr, "type %s has no operation table",
                               td->name ? td->name : "(unnamed)");

    for(i = 0; i < sizeof vn_dispatch / sizeof vn_dispatch[0]; i++)
        if(vn_dispatch[i].op == td->op)
            return vn_dispatch[i].handler(w, td, sptr, level);

    return vn_fail(w, td, sptr,
                   "no value notation for type %s: unsupported or unknown "
                   "operation table",
                   td->name && td->name[0] ? td->name : "(unnamed)");
}
```

In `src/vn_primitive.c`:

```c
#include <BOOLEAN.h>
#include <NULL.h>
#include "vn_internal.h"

int
vn_h_boolean(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             int level) {
    const BOOLEAN_t *b = (const BOOLEAN_t *)sptr;
    (void)td; (void)level;
    return vn_puts(w, *b ? "TRUE" : "FALSE");
}

int
vn_h_null(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
          int level) {
    (void)td; (void)sptr; (void)level;
    return vn_puts(w, "NULL");
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: all three suites `ok`. The REAL case must report a reason mentioning `REAL`, proving `td->name` reaches the message.

- [ ] **Step 5: Commit**

```bash
git add src tests Makefile
git commit -m "feat: type dispatch on asn_OP_* tables, with BOOLEAN and NULL

Unknown or unsupported operation tables fail with a named reason rather
than emitting anything, so a caller cannot mistake a partial encode for a
complete one."
```

---

## Task 4: INTEGER and ENUMERATED

Covers `asn_OP_INTEGER`, `asn_OP_NativeInteger`, `asn_OP_ENUMERATED`, `asn_OP_NativeEnumerated`, including integers too large for `intmax_t`.

**Files:**
- Modify: `src/vn_primitive.c`, `src/vn_encoder.c` (4 table entries)
- Create: `tests/t_integer.c`
- Modify: `Makefile` (`TESTS += t_integer`, `t_integer_SCHEMA := prim`)

**Interfaces:**
- Produces: `vn_h_integer`, `vn_h_native_integer`, `vn_h_enumerated`, `vn_h_native_enumerated`, and internal `static int vn_int_decimal(vn_writer_t *, const INTEGER_t *)` handling arbitrary width.

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_integer.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"
#include "Colour.h"
#include "BigInt.h"

static void
check_int(const char *label, const unsigned char *der, size_t derlen,
          const char *want) {
    INTEGER_t *v = 0;
    asn_dec_rval_t rv;
    char reason[160], *out;

    VNT_CASE(label);
    rv = ber_decode(0, &asn_DEF_BigInt, (void **)&v, der, derlen);
    VNT_TRUE(rv.code == RC_OK);
    out = vnt_encode(&asn_DEF_BigInt, v, 0, reason, sizeof reason);
    VNT_STREQ(out, want);
    free(out);
    ASN_STRUCT_FREE(asn_DEF_BigInt, v);
}

int
main(void) {
    char reason[160], *out;
    long small;
    Colour_t col;

    /* NativeInteger: asn1c uses `long` for unconstrained small INTEGERs */
    VNT_CASE("native integer zero");
    small = 0;
    out = vnt_encode(&asn_DEF_NativeInteger, &small, 0, reason, sizeof reason);
    VNT_STREQ(out, "0");
    free(out);

    VNT_CASE("native integer negative");
    small = -12345;
    out = vnt_encode(&asn_DEF_NativeInteger, &small, 0, reason, sizeof reason);
    VNT_STREQ(out, "-12345");
    free(out);

    /* buffer-backed INTEGER, values chosen to span the intmax_t boundary */
    { const unsigned char d[] = {0x02, 0x01, 0x00};
      check_int("INTEGER 0", d, sizeof d, "0"); }
    { const unsigned char d[] = {0x02, 0x01, 0x7f};
      check_int("INTEGER 127", d, sizeof d, "127"); }
    { const unsigned char d[] = {0x02, 0x01, 0x80};
      check_int("INTEGER -128", d, sizeof d, "-128"); }
    /* 2^64 = 18446744073709551616, one past unsigned 64-bit */
    { const unsigned char d[] = {0x02, 0x09, 0x01, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00};
      check_int("INTEGER 2^64", d, sizeof d, "18446744073709551616"); }
    /* -2^64 */
    { const unsigned char d[] = {0x02, 0x09, 0xff, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00};
      check_int("INTEGER -2^64", d, sizeof d, "-18446744073709551616"); }
    /* 2^128 - 1, all ones over 17 bytes */
    { const unsigned char d[] = {0x02, 0x11, 0x00,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      check_int("INTEGER 2^128-1", d, sizeof d,
                "340282366920938463463374607431768211455"); }

    /* ENUMERATED renders the identifier; the number alone is not valid VN */
    VNT_CASE("enumerated identifier");
    col = 1;
    out = vnt_encode(&asn_DEF_Colour, &col, 0, reason, sizeof reason);
    VNT_STREQ(out, "green");
    free(out);

    VNT_CASE("enumerated with value annotation uses a comment");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_ENUM_WITH_VALUE;
        col = 2;
        out = vnt_encode(&asn_DEF_Colour, &col, &o, reason, sizeof reason);
        VNT_STREQ(out, "blue -- (2) --");
        free(out);
    }

    VNT_CASE("unknown enumerated value fails under strict enumeration");
    col = 99;
    VNT_TRUE(vnt_encode_fails(&asn_DEF_Colour, &col, 0, reason, sizeof reason));
    VNT_TRUE(strstr(reason, "99") != 0);

    VNT_CASE("unknown enumerated value is a number under VN_F_LENIENT");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_LENIENT;
        col = 99;
        out = vnt_encode(&asn_DEF_Colour, &col, &o, reason, sizeof reason);
        VNT_STREQ(out, "99");
        free(out);
    }

    return vnt_report("t_integer");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: `t_integer` reports `(null)` for every case — no INTEGER handler is registered yet.

- [ ] **Step 3: Implement decimal conversion and the handlers**

The bignum path exists because asn1c itself gives up past `intmax_t` and prints `AA:BB:CC` (`INTEGER.c:179-198`), which is not value notation. Algorithm: take the magnitude as a big-endian byte array (two's-complement negated first when negative), then repeatedly divide by 10 collecting remainders.

```c
#include <limits.h>
#include <string.h>
#include <INTEGER.h>
#include <NativeInteger.h>
#include <NativeEnumerated.h>
#include "vn_internal.h"

/* Longest decimal we will produce; 128 bytes of magnitude needs 309 digits. */
#define VN_INT_MAX_OCTETS 160
#define VN_INT_MAX_DIGITS 400

/*
 * Render an arbitrary-width two's-complement big-endian integer as decimal.
 * Handles the values asn_INTEGER2imax() cannot.
 */
static int
vn_int_big_decimal(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                   const INTEGER_t *st) {
    unsigned char mag[VN_INT_MAX_OCTETS];
    char digits[VN_INT_MAX_DIGITS];
    size_t len = st->size, ndigits = 0, first;
    int negative;

    if(len == 0) return vn_puts(w, "0");
    if(len > sizeof mag)
        return vn_fail(w, td, st, "INTEGER of %zu octets exceeds the %zu octet "
                                  "limit of this encoder", len, sizeof mag);

    negative = (st->buf[0] & 0x80) != 0;
    memcpy(mag, st->buf, len);
    if(negative) { /* two's complement negate in place */
        size_t i = len;
        while(i-- > 0) mag[i] = (unsigned char)~mag[i];
        for(i = len; i-- > 0;) if(++mag[i] != 0) break;
    }

    /* strip leading zero octets */
    for(first = 0; first + 1 < len && mag[first] == 0; first++) ;

    do { /* long division of mag[first..len) by 10 */
        unsigned rem = 0;
        size_t i;
        for(i = first; i < len; i++) {
            unsigned cur = (rem << 8) | mag[i];
            mag[i] = (unsigned char)(cur / 10);
            rem = cur % 10;
        }
        if(ndigits >= sizeof digits)
            return vn_fail(w, td, st, "internal: decimal buffer overflow");
        digits[ndigits++] = (char)('0' + rem);
        while(first < len && mag[first] == 0) first++;
    } while(first < len);

    if(negative && vn_putc(w, '-') < 0) return -1;
    while(ndigits-- > 0)
        if(vn_putc(w, digits[ndigits]) < 0) return -1;
    return 0;
}

static int
vn_int_decimal(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
               const INTEGER_t *st) {
    intmax_t v;
    if(!st->buf || st->size == 0) return vn_puts(w, "0");
    if(asn_INTEGER2imax(st, &v) == 0) return vn_printf(w, "%jd", v);
    return vn_int_big_decimal(w, td, st);
}

int
vn_h_integer(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
             int level) {
    (void)level;
    return vn_int_decimal(w, td, (const INTEGER_t *)sptr);
}

int
vn_h_native_integer(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                    const void *sptr, int level) {
    const asn_INTEGER_specifics_t *specs =
        (const asn_INTEGER_specifics_t *)td->specifics;
    (void)level;
    if(specs && specs->field_unsigned)
        return vn_printf(w, "%lu", *(const unsigned long *)sptr);
    return vn_printf(w, "%ld", *(const long *)sptr);
}

/* Shared by ENUMERATED and NativeEnumerated once the value is a long. */
static int
vn_enum_value(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
              const void *sptr, long value) {
    const asn_INTEGER_specifics_t *specs =
        (const asn_INTEGER_specifics_t *)td->specifics;
    const asn_INTEGER_enum_map_t *item =
        specs ? INTEGER_map_value2enum(specs, value) : 0;

    if(item && item->enum_name) {
        if(vn_puts(w, item->enum_name) < 0) return -1;
        if(w->flags & VN_F_ENUM_WITH_VALUE) {
            /* X.680 EnumeratedValue is an identifier; the number must be a
             * comment, never `green (1)`. */
            if(vn_putc(w, ' ') < 0) return -1;
            if(vn_is_annotated(w)) return vn_comment(w, "(%ld)", value);
            if(vn_printf(w, "-- (%ld) --", value) < 0) return -1;
        }
        return 0;
    }
    if(w->flags & VN_F_LENIENT) return vn_printf(w, "%ld", value);
    return vn_fail(w, td, sptr,
                   "ENUMERATED %s has no identifier for value %ld",
                   td->name ? td->name : "(unnamed)", value);
}

int
vn_h_native_enumerated(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                       const void *sptr, int level) {
    (void)level;
    return vn_enum_value(w, td, sptr, *(const long *)sptr);
}

int
vn_h_enumerated(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    const INTEGER_t *st = (const INTEGER_t *)sptr;
    long value;
    (void)level;
    if(asn_INTEGER2long(st, &value) != 0)
        return vn_fail(w, td, sptr, "ENUMERATED %s value does not fit in long",
                       td->name ? td->name : "(unnamed)");
    return vn_enum_value(w, td, sptr, value);
}
```

Declare the four handlers in `src/vn_internal.h` and add to the dispatch table in `src/vn_encoder.c` (with `#include <NativeInteger.h>` and `<NativeEnumerated.h>`):

```c
    { &asn_OP_INTEGER,           vn_h_integer,           "INTEGER"          },
    { &asn_OP_NativeInteger,     vn_h_native_integer,    "NativeInteger"    },
    { &asn_OP_ENUMERATED,        vn_h_enumerated,        "ENUMERATED"       },
    { &asn_OP_NativeEnumerated,  vn_h_native_enumerated, "NativeEnumerated" },
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_integer: ok`. The `2^128-1` case is the one that proves the bignum path; if it prints something like `FF:FF:...` the code fell through to asn1c's own formatting.

- [ ] **Step 5: Commit**

```bash
git add src tests Makefile
git commit -m "feat: INTEGER and ENUMERATED, including arbitrary-width decimals

asn1c prints large integers as colon-separated hex, which is not value
notation, so decimal conversion is done here by long division.

ENUMERATED emits the identifier; VN_F_ENUM_WITH_VALUE puts the number in
a comment because X.680 EnumeratedValue is an identifier only."
```

---

## Task 5: OCTET STRING and hex output

**Files:**
- Modify: `src/vn_primitive.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/t_octet.c`
- Modify: `Makefile` (`TESTS += t_octet`, `t_octet_SCHEMA := prim`)

**Interfaces:**
- Produces: `vn_h_octet_string`, and internal `int vn_put_hex(vn_writer_t *, const unsigned char *, size_t, int level)` — emits `'..'H`, uppercase, wrapping at `line_width` on an even digit boundary when `line_width > 0`. Reused by Task 8 (BIT STRING) and Task 10 (ANY).

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_octet.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"

static char *
enc_os(const unsigned char *bytes, size_t len, const vn_options_t *o) {
    OCTET_STRING_t os;
    char reason[160];
    memset(&os, 0, sizeof os);
    os.buf = (uint8_t *)bytes;
    os.size = len;
    return vnt_encode(&asn_DEF_OCTET_STRING, &os, o, reason, sizeof reason);
}

int
main(void) {
    char *out;

    VNT_CASE("empty octet string");
    out = enc_os((const unsigned char *)"", 0, 0);
    VNT_STREQ(out, "''H");
    free(out);

    VNT_CASE("hex digits are uppercase");
    { const unsigned char b[] = {0x00, 0xaa, 0xbb};
      out = enc_os(b, sizeof b, 0);
      VNT_STREQ(out, "'00AABB'H");
      free(out); }

    VNT_CASE("all byte values round out correctly");
    { const unsigned char b[] = {0x0f, 0xf0, 0xff, 0x01};
      out = enc_os(b, sizeof b, 0);
      VNT_STREQ(out, "'0FF0FF01'H");
      free(out); }

    VNT_CASE("canonical mode never wraps");
    {
        vn_options_t o;
        unsigned char b[64];
        size_t i;
        char want[2 + 128 + 2 + 1];
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_CANONICAL;
        for(i = 0; i < sizeof b; i++) b[i] = (unsigned char)i;
        out = enc_os(b, sizeof b, &o);
        want[0] = '\'';
        for(i = 0; i < sizeof b; i++)
            sprintf(want + 1 + i * 2, "%02X", (unsigned)b[i]);
        strcpy(want + 1 + sizeof b * 2, "'H");
        VNT_STREQ(out, want);
        VNT_TRUE(strchr(out, '\n') == 0);
        free(out);
    }

    VNT_CASE("pretty mode wraps long hex on an even boundary");
    {
        vn_options_t o;
        unsigned char b[40];
        const char *p;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_PRETTY;
        o.line_width = 20;
        memset(b, 0xab, sizeof b);
        out = enc_os(b, sizeof b, &o);
        VNT_TRUE(strchr(out, '\n') != 0);
        /* every line must hold an even number of hex digits, so no byte is
         * split across a line break */
        for(p = out; *p;) {
            const char *nl = strchr(p, '\n');
            size_t n = 0;
            const char *q;
            for(q = p; q != (nl ? nl : p + strlen(p)); q++)
                if(strchr("0123456789ABCDEF", *q)) n++;
            VNT_TRUE(n % 2 == 0);
            if(!nl) break;
            p = nl + 1;
        }
        free(out);
    }

    return vnt_report("t_octet");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: `t_octet` reports `(null)` — no OCTET STRING handler yet.

- [ ] **Step 3: Implement hex output**

In `src/vn_primitive.c`:

```c
#include <OCTET_STRING.h>

int
vn_put_hex(vn_writer_t *w, const unsigned char *buf, size_t len, int level) {
    static const char hexdigits[] = "0123456789ABCDEF";
    size_t i, on_line = 0;
    /* budget for hex digits on a continuation line, leaving room for indent */
    int budget = w->line_width - (level + 1) * w->indent_width;

    if(budget < 8) budget = 8;
    if(vn_putc(w, '\'') < 0) return -1;
    for(i = 0; i < len; i++) {
        if(w->line_width > 0 && on_line + 2 > (size_t)budget) {
            if(vn_break(w, level + 1) < 0) return -1;
            on_line = 0;
        }
        if(vn_putc(w, hexdigits[buf[i] >> 4]) < 0) return -1;
        if(vn_putc(w, hexdigits[buf[i] & 0x0f]) < 0) return -1;
        on_line += 2;
    }
    return vn_puts(w, "'H");
}

int
vn_h_octet_string(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                  const void *sptr, int level) {
    const OCTET_STRING_t *os = (const OCTET_STRING_t *)sptr;
    (void)td;
    return vn_put_hex(w, os->buf, os->buf ? os->size : 0, level);
}
```

Declare `vn_put_hex` and `vn_h_octet_string` in `src/vn_internal.h`, and register:

```c
    { &asn_OP_OCTET_STRING, vn_h_octet_string, "OCTET STRING" },
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_octet: ok`.

- [ ] **Step 5: Commit**

```bash
git add src tests Makefile
git commit -m "feat: OCTET STRING as uppercase hstring, wrapped in pretty mode

Wrapping happens on an even digit boundary so a byte is never split
across lines. Canonical mode never wraps, keeping output deterministic."
```

---

## Task 6: SEQUENCE and SET

**Files:**
- Modify: `src/vn_constructed.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/schemas/constructed.asn1`, `tests/t_sequence.c`
- Modify: `Makefile` (`SCHEMAS += constructed`, `TESTS += t_sequence`, `t_sequence_SCHEMA := constructed`)

**Interfaces:**
- Produces: `vn_h_sequence` (serves SEQUENCE and SET), plus internal `const void *vn_member_ptr(const asn_TYPE_member_t *elm, const void *sptr)` returning the member address or `NULL` when an `ATF_POINTER` member is absent.

- [ ] **Step 1: Write the test schema**

```asn1
-- tests/schemas/constructed.asn1
Constructed DEFINITIONS AUTOMATIC TAGS ::= BEGIN

Colour ::= ENUMERATED { red(0), green(1), blue(2) }

Inner ::= SEQUENCE {
    id   INTEGER,
    tag  OCTET STRING
}

Pair ::= SEQUENCE {
    first  INTEGER,
    second BOOLEAN OPTIONAL
}

Nested ::= SEQUENCE {
    name   OCTET STRING,
    inner  Inner,
    col    Colour,
    opt    INTEGER OPTIONAL,
    deflt  INTEGER DEFAULT 7
}

Empty ::= SEQUENCE { }

Numbers ::= SEQUENCE OF INTEGER
Inners  ::= SEQUENCE OF Inner
Bag     ::= SET OF INTEGER

Choice ::= CHOICE {
    nothing NULL,
    flag    BOOLEAN,
    inner   Inner
}

Wrapper ::= SEQUENCE {
    pick    Choice,
    numbers Numbers,
    inners  Inners
}

END
```

- [ ] **Step 2: Write the failing test**

```c
/* tests/t_sequence.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Pair.h"
#include "Nested.h"
#include "Empty.h"

int
main(void) {
    char reason[160], *out;

    VNT_CASE("empty sequence");
    {
        Empty_t e;
        memset(&e, 0, sizeof e);
        out = vnt_encode(&asn_DEF_Empty, &e, 0, reason, sizeof reason);
        VNT_STREQ(out, "{ }");
        free(out);
    }

    VNT_CASE("absent OPTIONAL member is omitted entirely");
    {
        Pair_t p;
        memset(&p, 0, sizeof p);
        p.first = 3;
        p.second = 0;
        out = vnt_encode(&asn_DEF_Pair, &p, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    first 3\n}");
        free(out);
    }

    VNT_CASE("present OPTIONAL member is emitted with a comma");
    {
        Pair_t p;
        BOOLEAN_t b = 1;
        memset(&p, 0, sizeof p);
        p.first = 3;
        p.second = &b;
        out = vnt_encode(&asn_DEF_Pair, &p, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    first 3,\n    second TRUE\n}");
        free(out);
    }

    VNT_CASE("nested sequence indents and scalars stay on the field line");
    {
        Nested_t n;
        const unsigned char name[] = {0xde, 0xad};
        const unsigned char tag[] = {0x01};
        memset(&n, 0, sizeof n);
        n.name.buf = (uint8_t *)name; n.name.size = sizeof name;
        n.inner.id = 5;
        n.inner.tag.buf = (uint8_t *)tag; n.inner.tag.size = sizeof tag;
        n.col = 1;
        out = vnt_encode(&asn_DEF_Nested, &n, 0, reason, sizeof reason);
        VNT_STREQ(out,
                  "{\n"
                  "    name 'DEAD'H,\n"
                  "    inner {\n"
                  "        id 5,\n"
                  "        tag '01'H\n"
                  "    },\n"
                  "    col green\n"
                  "}");
        free(out);
    }

    VNT_CASE("canonical mode uses two-space indent");
    {
        Pair_t p;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_CANONICAL;
        memset(&p, 0, sizeof p);
        p.first = 1;
        out = vnt_encode(&asn_DEF_Pair, &p, &o, reason, sizeof reason);
        VNT_STREQ(out, "{\n  first 1\n}");
        free(out);
    }

    VNT_CASE("annotated mode names the type and notes absent members");
    {
        Pair_t p;
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.mode = VN_MODE_ANNOTATED;
        memset(&p, 0, sizeof p);
        p.first = 1;
        out = vnt_encode(&asn_DEF_Pair, &p, &o, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "-- Pair --") != 0);
        VNT_TRUE(out && strstr(out, "second") != 0); /* noted as absent */
        free(out);
    }

    return vnt_report("t_sequence");
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `make check`
Expected: `t_sequence` reports `(null)` for every case.

- [ ] **Step 4: Implement SEQUENCE and SET**

In `src/vn_constructed.c`:

```c
#include <string.h>
#include <constr_SEQUENCE.h>
#include <constr_SET_OF.h>
#include <constr_CHOICE.h>
#include <asn_SET_OF.h>
#include "vn_internal.h"

const void *
vn_member_ptr(const asn_TYPE_member_t *elm, const void *sptr) {
    const void *p = (const char *)sptr + elm->memb_offset;
    if(elm->flags & ATF_POINTER) return *(const void *const *)p;
    return p;
}

int
vn_h_sequence(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
              const void *sptr, int level) {
    unsigned i;
    int emitted = 0;

    if(vn_putc(w, '{') < 0) return -1;
    if(vn_is_annotated(w) && td->name && td->name[0]) {
        if(vn_putc(w, ' ') < 0) return -1;
        if(vn_comment(w, "%s", td->name) < 0) return -1;
    }

    for(i = 0; i < td->elements_count; i++) {
        const asn_TYPE_member_t *elm = &td->elements[i];
        const void *memb = vn_member_ptr(elm, sptr);

        if(!memb) { /* absent OPTIONAL/DEFAULT member: omit it */
            if(vn_is_annotated(w)) {
                if(emitted && vn_putc(w, ',') < 0) return -1;
                if(vn_break(w, level + 1) < 0) return -1;
                if(vn_comment(w, "%s absent", elm->name ? elm->name : "?") < 0)
                    return -1;
                /* a comment is not a value, so it must not count as emitted */
            }
            continue;
        }

        if(emitted && vn_putc(w, ',') < 0) return -1;
        if(vn_break(w, level + 1) < 0) return -1;
        if(elm->name && elm->name[0]) {
            if(vn_puts(w, elm->name) < 0) return -1;
            if(vn_putc(w, ' ') < 0) return -1;
        }
        if(vn_encode_value(w, elm->type, memb, level + 1) < 0) return -1;
        emitted = 1;
    }

    if(!emitted) return vn_puts(w, " }");
    if(vn_break(w, level) < 0) return -1;
    return vn_putc(w, '}');
}
```

Careful with the annotated-absent branch: emitting a comment where a value would go must not make the *next* real member print a leading comma, and must not leave a dangling comma before `}`. The `emitted` flag stays untouched for comments, which is why the comment is placed on its own line without a comma of its own.

Declare `vn_h_sequence` and `vn_member_ptr` in `src/vn_internal.h`, then register both constructed tables — SET has the same value notation as SEQUENCE:

```c
    { &asn_OP_SEQUENCE, vn_h_sequence, "SEQUENCE" },
    { &asn_OP_SET,      vn_h_sequence, "SET"      },
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make check`
Expected: `t_sequence: ok`. If the absent-OPTIONAL case shows a trailing comma, the `emitted` bookkeeping is wrong.

- [ ] **Step 6: Commit**

```bash
git add src tests Makefile
git commit -m "feat: SEQUENCE and SET value notation

Absent OPTIONAL members are omitted; annotated mode notes them in a
comment without disturbing comma placement."
```

---

## Task 7: SEQUENCE OF, SET OF, CHOICE

**Files:**
- Modify: `src/vn_constructed.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/t_collection.c`
- Modify: `Makefile` (`TESTS += t_collection`, `t_collection_SCHEMA := constructed`)

**Interfaces:**
- Produces: `vn_h_set_of` (serves SEQUENCE OF and SET OF), `vn_h_choice`.

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_collection.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Numbers.h"
#include "Inners.h"
#include "Choice.h"

int
main(void) {
    char reason[160], *out;

    VNT_CASE("empty list");
    {
        Numbers_t n;
        memset(&n, 0, sizeof n);
        out = vnt_encode(&asn_DEF_Numbers, &n, 0, reason, sizeof reason);
        VNT_STREQ(out, "{ }");
        free(out);
    }

    VNT_CASE("list of scalars, no member names");
    {
        Numbers_t n;
        long a = 1, b = 2, c = 3;
        long *items[3];
        memset(&n, 0, sizeof n);
        items[0] = &a; items[1] = &b; items[2] = &c;
        n.list.array = (long **)items;
        n.list.count = 3;
        n.list.size = 3;
        out = vnt_encode(&asn_DEF_Numbers, &n, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    1,\n    2,\n    3\n}");
        free(out);
    }

    VNT_CASE("list of sequences nests");
    {
        Inners_t l;
        Inner_t i0;
        const unsigned char t0[] = {0xaa};
        Inner_t *items[1];
        memset(&l, 0, sizeof l);
        memset(&i0, 0, sizeof i0);
        i0.id = 9;
        i0.tag.buf = (uint8_t *)t0;
        i0.tag.size = sizeof t0;
        items[0] = &i0;
        l.list.array = items;
        l.list.count = 1;
        l.list.size = 1;
        out = vnt_encode(&asn_DEF_Inners, &l, 0, reason, sizeof reason);
        VNT_STREQ(out, "{\n    {\n        id 9,\n        tag 'AA'H\n    }\n}");
        free(out);
    }

    VNT_CASE("choice uses `alternative : value`");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_flag;
        c.choice.flag = 1;
        out = vnt_encode(&asn_DEF_Choice, &c, 0, reason, sizeof reason);
        VNT_STREQ(out, "flag : TRUE");
        free(out);
    }

    VNT_CASE("choice of a constructed alternative");
    {
        Choice_t c;
        const unsigned char t[] = {0x01, 0x02};
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_inner;
        c.choice.inner.id = 4;
        c.choice.inner.tag.buf = (uint8_t *)t;
        c.choice.inner.tag.size = sizeof t;
        out = vnt_encode(&asn_DEF_Choice, &c, 0, reason, sizeof reason);
        VNT_STREQ(out, "inner : {\n    id 4,\n    tag '0102'H\n}");
        free(out);
    }

    VNT_CASE("unset choice fails rather than guessing");
    {
        Choice_t c;
        memset(&c, 0, sizeof c);
        c.present = Choice_PR_NOTHING;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Choice, &c, 0, reason, sizeof reason));
        VNT_TRUE(strstr(reason, "no alternative") != 0);
    }

    return vnt_report("t_collection");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: `t_collection` reports `(null)` for all cases.

- [ ] **Step 3: Implement collections and CHOICE**

Append to `src/vn_constructed.c`:

```c
int
vn_h_set_of(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            int level) {
    const asn_anonymous_set_ *list = _A_CSET_FROM_VOID(sptr);
    const asn_TYPE_descriptor_t *elt;
    int i;

    if(td->elements_count != 1 || !td->elements[0].type)
        return vn_fail(w, td, sptr,
                       "list type %s has no element type descriptor",
                       td->name ? td->name : "(unnamed)");
    elt = td->elements[0].type;

    if(vn_putc(w, '{') < 0) return -1;
    if(vn_is_annotated(w) && td->name && td->name[0]) {
        if(vn_putc(w, ' ') < 0) return -1;
        if(vn_comment(w, "%s, %d element(s)", td->name, list->count) < 0)
            return -1;
    }
    if(list->count == 0) return vn_puts(w, " }");

    for(i = 0; i < list->count; i++) {
        if(i && vn_putc(w, ',') < 0) return -1;
        if(vn_break(w, level + 1) < 0) return -1;
        if(!list->array[i])
            return vn_fail(w, td, sptr, "list %s element %d is a null pointer",
                           td->name ? td->name : "(unnamed)", i);
        if(vn_encode_value(w, elt, list->array[i], level + 1) < 0) return -1;
    }
    if(vn_break(w, level) < 0) return -1;
    return vn_putc(w, '}');
}

int
vn_h_choice(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            int level) {
    const asn_CHOICE_specifics_t *specs =
        (const asn_CHOICE_specifics_t *)td->specifics;
    const asn_TYPE_member_t *elm;
    const void *memb;
    unsigned present;

    if(!specs) return vn_fail(w, td, sptr, "CHOICE %s has no specifics",
                              td->name ? td->name : "(unnamed)");

    /* The present index sits at pres_offset and is pres_size bytes wide. */
    switch(specs->pres_size) {
    case sizeof(int):
        present = (unsigned)*(const int *)((const char *)sptr + specs->pres_offset);
        break;
    case sizeof(short):
        present = (unsigned)*(const short *)((const char *)sptr + specs->pres_offset);
        break;
    case sizeof(char):
        present = (unsigned)*(const char *)((const char *)sptr + specs->pres_offset);
        break;
    default:
        return vn_fail(w, td, sptr,
                       "CHOICE %s uses an unexpected presence width of %u bytes",
                       td->name ? td->name : "(unnamed)", specs->pres_size);
    }

    /* 0 means "nothing selected"; members are numbered from 1. */
    if(present == 0 || present > td->elements_count)
        return vn_fail(w, td, sptr, "CHOICE %s has no alternative selected",
                       td->name ? td->name : "(unnamed)");

    elm = &td->elements[present - 1];
    memb = vn_member_ptr(elm, sptr);
    if(!memb)
        return vn_fail(w, td, sptr,
                       "CHOICE %s selects %s but the member is a null pointer",
                       td->name ? td->name : "(unnamed)",
                       elm->name ? elm->name : "?");

    if(elm->name && elm->name[0]) {
        if(vn_puts(w, elm->name) < 0) return -1;
        if(vn_puts(w, " : ") < 0) return -1;
    }
    return vn_encode_value(w, elm->type, memb, level);
}
```

Register:

```c
    { &asn_OP_SEQUENCE_OF, vn_h_set_of, "SEQUENCE OF" },
    { &asn_OP_SET_OF,      vn_h_set_of, "SET OF"      },
    { &asn_OP_CHOICE,      vn_h_choice, "CHOICE"      },
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_collection: ok`.

- [ ] **Step 5: Commit**

```bash
git add src tests Makefile
git commit -m "feat: SEQUENCE OF, SET OF and CHOICE

List elements carry no field name, per X.680. CHOICE reads the present
index from asn_CHOICE_specifics_t and fails when nothing is selected."
```

---

## Task 8: BIT STRING, OBJECT IDENTIFIER, RELATIVE-OID

**Files:**
- Modify: `src/vn_primitive.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/t_bits_oid.c`
- Modify: `Makefile` (`TESTS += t_bits_oid`, `t_bits_oid_SCHEMA := prim`); add `bits BIT STRING` and `oid OBJECT IDENTIFIER` members to `Holder` in `prim.asn1` so the skeletons are generated, and update `t_link`'s `elements_count`.

**Interfaces:**
- Produces: `vn_h_bit_string`, `vn_h_oid`, `vn_h_relative_oid`.

- [ ] **Step 1: Write the failing test**

```c
/* tests/t_bits_oid.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Holder.h"

static char *
enc_bits(const unsigned char *b, size_t len, int unused) {
    BIT_STRING_t bs;
    char reason[160];
    memset(&bs, 0, sizeof bs);
    bs.buf = (uint8_t *)b;
    bs.size = len;
    bs.bits_unused = unused;
    return vnt_encode(&asn_DEF_BIT_STRING, &bs, 0, reason, sizeof reason);
}

int
main(void) {
    char reason[160], *out;

    VNT_CASE("empty bit string is an empty bstring");
    out = enc_bits((const unsigned char *)"", 0, 0);
    VNT_STREQ(out, "''B");
    free(out);

    VNT_CASE("8 bits are a multiple of 4, so hstring");
    { const unsigned char b[] = {0xab};
      out = enc_bits(b, 1, 0);
      VNT_STREQ(out, "'AB'H");
      free(out); }

    VNT_CASE("12 bits are a multiple of 4, so hstring drops the padding");
    { const unsigned char b[] = {0xab, 0xc0};
      out = enc_bits(b, 2, 4);
      VNT_STREQ(out, "'ABC'H");
      free(out); }

    VNT_CASE("14 bits are not a multiple of 4, so bstring");
    { const unsigned char b[] = {0x61, 0xd4}; /* 0110000111010100 */
      out = enc_bits(b, 2, 2);
      VNT_STREQ(out, "'01100001110101'B");
      free(out); }

    VNT_CASE("single set bit");
    { const unsigned char b[] = {0x80};
      out = enc_bits(b, 1, 7);
      VNT_STREQ(out, "'1'B");
      free(out); }

    VNT_CASE("object identifier arcs are space separated");
    {
        OBJECT_IDENTIFIER_t oid;
        asn_oid_arc_t arcs[4] = {2, 23, 143, 1};
        memset(&oid, 0, sizeof oid);
        VNT_TRUE(OBJECT_IDENTIFIER_set_arcs(&oid, arcs, 4) == 0);
        out = vnt_encode(&asn_DEF_OBJECT_IDENTIFIER, &oid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 2 23 143 1 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_OBJECT_IDENTIFIER, &oid);
    }

    VNT_CASE("large arc values survive");
    {
        OBJECT_IDENTIFIER_t oid;
        asn_oid_arc_t arcs[3] = {1, 2, 4294967295u};
        memset(&oid, 0, sizeof oid);
        VNT_TRUE(OBJECT_IDENTIFIER_set_arcs(&oid, arcs, 3) == 0);
        out = vnt_encode(&asn_DEF_OBJECT_IDENTIFIER, &oid, 0, reason,
                         sizeof reason);
        VNT_STREQ(out, "{ 1 2 4294967295 }");
        free(out);
        ASN_STRUCT_RESET(asn_DEF_OBJECT_IDENTIFIER, &oid);
    }

    return vnt_report("t_bits_oid");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: `t_bits_oid` reports `(null)` for all cases.

- [ ] **Step 3: Implement**

In `src/vn_primitive.c`:

```c
#include <BIT_STRING.h>
#include <OBJECT_IDENTIFIER.h>
#include "RELATIVE-OID.h"   /* note the dash in asn1c's filename */

int
vn_h_bit_string(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                const void *sptr, int level) {
    const BIT_STRING_t *bs = (const BIT_STRING_t *)sptr;
    size_t nbits;

    if(bs->bits_unused < 0 || bs->bits_unused > 7)
        return vn_fail(w, td, sptr, "BIT STRING has bits_unused = %d, "
                                    "which must be 0..7", bs->bits_unused);
    nbits = bs->buf ? bs->size * 8 - (size_t)bs->bits_unused : 0;

    /* X.680 allows both forms; use hstring when the bit count divides by 4,
     * because it is far more compact, and bstring otherwise so that no
     * padding bit is ever invented. */
    if(nbits > 0 && nbits % 4 == 0) {
        static const char hexdigits[] = "0123456789ABCDEF";
        size_t i, ndigits = nbits / 4;
        if(vn_putc(w, '\'') < 0) return -1;
        for(i = 0; i < ndigits; i++) {
            unsigned nib = bs->buf[i / 2];
            nib = (i % 2 == 0) ? (nib >> 4) : (nib & 0x0f);
            if(vn_putc(w, hexdigits[nib]) < 0) return -1;
        }
        return vn_puts(w, "'H");
    }

    if(vn_putc(w, '\'') < 0) return -1;
    {
        size_t i;
        for(i = 0; i < nbits; i++) {
            unsigned bit = (bs->buf[i / 8] >> (7 - i % 8)) & 1u;
            if(vn_putc(w, bit ? '1' : '0') < 0) return -1;
        }
    }
    return vn_puts(w, "'B");
}

/* Shared by OBJECT IDENTIFIER and RELATIVE-OID: `{ arc arc arc }`. */
static int
vn_put_arcs(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            ssize_t (*get_arcs)(const void *, asn_oid_arc_t *, size_t)) {
    asn_oid_arc_t arcs[32];
    ssize_t count, i;

    count = get_arcs(sptr, arcs, sizeof arcs / sizeof arcs[0]);
    if(count < 0)
        return vn_fail(w, td, sptr, "cannot read arcs of %s",
                       td->name ? td->name : "(unnamed)");
    if((size_t)count > sizeof arcs / sizeof arcs[0])
        return vn_fail(w, td, sptr,
                       "%s has %zd arcs, more than the %zu this encoder holds",
                       td->name ? td->name : "(unnamed)", count,
                       sizeof arcs / sizeof arcs[0]);

    if(vn_puts(w, "{") < 0) return -1;
    for(i = 0; i < count; i++)
        if(vn_printf(w, " %lu", (unsigned long)arcs[i]) < 0) return -1;
    return vn_puts(w, " }");
}

static ssize_t
vn_oid_arcs(const void *sptr, asn_oid_arc_t *arcs, size_t slots) {
    return OBJECT_IDENTIFIER_get_arcs((const OBJECT_IDENTIFIER_t *)sptr, arcs,
                                      slots);
}

static ssize_t
vn_roid_arcs(const void *sptr, asn_oid_arc_t *arcs, size_t slots) {
    return RELATIVE_OID_get_arcs((const RELATIVE_OID_t *)sptr, arcs, slots);
}

int
vn_h_oid(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
         int level) {
    (void)level;
    return vn_put_arcs(w, td, sptr, vn_oid_arcs);
}

int
vn_h_relative_oid(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
                  const void *sptr, int level) {
    (void)level;
    return vn_put_arcs(w, td, sptr, vn_roid_arcs);
}
```

Register:

```c
    { &asn_OP_BIT_STRING,        vn_h_bit_string,   "BIT STRING"   },
    { &asn_OP_OBJECT_IDENTIFIER, vn_h_oid,          "OBJECT IDENTIFIER" },
    { &asn_OP_RELATIVE_OID,      vn_h_relative_oid, "RELATIVE-OID" },
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_bits_oid: ok`. The 14-bit case is the one that proves no padding bit leaks into the output.

- [ ] **Step 5: Commit**

```bash
git add src tests Makefile
git commit -m "feat: BIT STRING, OBJECT IDENTIFIER and RELATIVE-OID

BIT STRING uses hstring when the bit count divides by four and bstring
otherwise, so padding bits are never invented. Named bit lists are not in
the runtime ABI and are out of scope."
```

---

## Task 9: Restricted strings and time types

**Files:**
- Modify: `src/vn_primitive.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/schemas/strings.asn1`, `tests/t_strings.c`
- Modify: `Makefile` (`SCHEMAS += strings`, `TESTS += t_strings`, `t_strings_SCHEMA := strings`)

**Interfaces:**
- Produces: `vn_h_string` (all restricted string types plus the time types), registered against 14 operation tables.

- [ ] **Step 1: Write the test schema**

```asn1
-- tests/schemas/strings.asn1
Strings DEFINITIONS AUTOMATIC TAGS ::= BEGIN

AllStrings ::= SEQUENCE {
    utf8      UTF8String,
    ia5       IA5String,
    printable PrintableString,
    numeric   NumericString,
    visible   VisibleString,
    general   GeneralString,
    graphic   GraphicString,
    teletex   TeletexString,
    videotex  VideotexString,
    bmp       BMPString,
    universal UniversalString,
    objdesc   ObjectDescriptor,
    gtime     GeneralizedTime,
    utime     UTCTime
}

END
```

- [ ] **Step 2: Write the failing test**

```c
/* tests/t_strings.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "AllStrings.h"

static char *
enc(const asn_TYPE_descriptor_t *td, const char *s, size_t len,
    const vn_options_t *o, char *reason, size_t rlen) {
    OCTET_STRING_t os;
    memset(&os, 0, sizeof os);
    os.buf = (uint8_t *)s;
    os.size = len;
    return vnt_encode(td, &os, o, reason, rlen);
}

int
main(void) {
    char reason[200], *out;

    VNT_CASE("empty string");
    out = enc(&asn_DEF_UTF8String, "", 0, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\"");
    free(out);

    VNT_CASE("plain ascii");
    out = enc(&asn_DEF_IA5String, "hello", 5, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"hello\"");
    free(out);

    VNT_CASE("embedded quote is doubled per X.680 11.14");
    out = enc(&asn_DEF_UTF8String, "a\"b", 3, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"a\"\"b\"");
    free(out);

    VNT_CASE("only quote is doubled, backslash is literal");
    out = enc(&asn_DEF_UTF8String, "a\\b", 3, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"a\\b\"");
    free(out);

    VNT_CASE("utf-8 bytes pass through unchanged");
    out = enc(&asn_DEF_UTF8String, "\xc3\xa4", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\xc3\xa4\"");
    free(out);

    VNT_CASE("control character fails by default");
    VNT_TRUE(enc(&asn_DEF_IA5String, "a\nb", 3, 0, reason, sizeof reason) == 0);
    VNT_TRUE(strstr(reason, "control") != 0);

    VNT_CASE("control character passes under VN_F_LENIENT");
    {
        vn_options_t o;
        memset(&o, 0, sizeof o);
        o.flags = VN_F_LENIENT;
        out = enc(&asn_DEF_IA5String, "a\nb", 3, &o, reason, sizeof reason);
        VNT_STREQ(out, "\"a\nb\"");
        free(out);
    }

    VNT_CASE("BMPString is transcoded from UTF-16BE to UTF-8");
    /* U+00E4 as UTF-16BE is 00 E4; as UTF-8 it is C3 A4 */
    out = enc(&asn_DEF_BMPString, "\x00\xe4", 2, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"\xc3\xa4\"");
    free(out);

    VNT_CASE("UniversalString is transcoded from UTF-32BE to UTF-8");
    /* U+1F600 as UTF-32BE is 00 01 F6 00; as UTF-8 it is F0 9F 98 80 */
    out = enc(&asn_DEF_UniversalString, "\x00\x01\xf6\x00", 4, 0, reason,
              sizeof reason);
    VNT_STREQ(out, "\"\xf0\x9f\x98\x80\"");
    free(out);

    VNT_CASE("odd-length BMPString fails");
    VNT_TRUE(enc(&asn_DEF_BMPString, "\x00", 1, 0, reason, sizeof reason) == 0);
    VNT_TRUE(strstr(reason, "BMPString") != 0);

    VNT_CASE("GeneralizedTime is a cstring of the raw bytes");
    out = enc(&asn_DEF_GeneralizedTime, "20260729124800Z", 15, 0, reason,
              sizeof reason);
    VNT_STREQ(out, "\"20260729124800Z\"");
    free(out);

    VNT_CASE("UTCTime is a cstring of the raw bytes");
    out = enc(&asn_DEF_UTCTime, "260729124800Z", 13, 0, reason, sizeof reason);
    VNT_STREQ(out, "\"260729124800Z\"");
    free(out);

    return vnt_report("t_strings");
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `make check`
Expected: `t_strings` reports `(null)` for all cases.

- [ ] **Step 4: Implement**

In `src/vn_primitive.c`:

```c
/* Append one code point as UTF-8. Returns -1 on failure. */
static int
vn_put_utf8(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            unsigned long cp) {
    char b[4];
    if(cp < 0x80) { b[0] = (char)cp; return vn_put(w, b, 1); }
    if(cp < 0x800) {
        b[0] = (char)(0xc0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3f));
        return vn_put(w, b, 2);
    }
    if(cp < 0x10000) {
        if(cp >= 0xd800 && cp <= 0xdfff)
            return vn_fail(w, td, sptr,
                           "code point U+%04lX is an unpaired surrogate", cp);
        b[0] = (char)(0xe0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[2] = (char)(0x80 | (cp & 0x3f));
        return vn_put(w, b, 3);
    }
    if(cp <= 0x10ffff) {
        b[0] = (char)(0xf0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[3] = (char)(0x80 | (cp & 0x3f));
        return vn_put(w, b, 4);
    }
    return vn_fail(w, td, sptr, "code point U+%lX is outside Unicode", cp);
}

/*
 * All restricted string types and the two time types share one form: an X.680
 * cstring. A literal quote is doubled (11.14). Control characters have no
 * cstring representation -- X.680 provides the character-defs form for those,
 * which this encoder does not implement -- so they fail unless VN_F_LENIENT.
 */
int
vn_h_string(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
            int level) {
    const OCTET_STRING_t *os = (const OCTET_STRING_t *)sptr;
    const asn_OCTET_STRING_specifics_t *specs =
        (const asn_OCTET_STRING_specifics_t *)td->specifics;
    enum asn_OS_Subvariant sub = specs ? specs->subvariant : ASN_OSUBV_STR;
    size_t i, len = os->buf ? os->size : 0;

    (void)level;
    if(vn_putc(w, '"') < 0) return -1;

    if(sub == ASN_OSUBV_U16) {
        if(len % 2)
            return vn_fail(w, td, sptr,
                           "BMPString length %zu is not a multiple of 2", len);
        for(i = 0; i < len; i += 2) {
            unsigned long cp = ((unsigned long)os->buf[i] << 8) | os->buf[i + 1];
            if(cp == '"' && vn_putc(w, '"') < 0) return -1;
            if(vn_put_utf8(w, td, sptr, cp) < 0) return -1;
        }
    } else if(sub == ASN_OSUBV_U32) {
        if(len % 4)
            return vn_fail(w, td, sptr,
                           "UniversalString length %zu is not a multiple of 4",
                           len);
        for(i = 0; i < len; i += 4) {
            unsigned long cp = ((unsigned long)os->buf[i] << 24)
                             | ((unsigned long)os->buf[i + 1] << 16)
                             | ((unsigned long)os->buf[i + 2] << 8)
                             | os->buf[i + 3];
            if(cp == '"' && vn_putc(w, '"') < 0) return -1;
            if(vn_put_utf8(w, td, sptr, cp) < 0) return -1;
        }
    } else {
        for(i = 0; i < len; i++) {
            unsigned char c = os->buf[i];
            /* Reject C0 and DEL. UTF-8 continuation bytes are >= 0x80 and
             * pass through untouched, so multibyte text is preserved. */
            if((c < 0x20 || c == 0x7f) && !(w->flags & VN_F_LENIENT))
                return vn_fail(w, td, sptr,
                               "%s contains control character 0x%02X at offset "
                               "%zu, which has no cstring form in X.680",
                               td->name ? td->name : "string", c, i);
            if(c == '"' && vn_putc(w, '"') < 0) return -1;
            if(vn_putc(w, (char)c) < 0) return -1;
        }
    }

    return vn_putc(w, '"');
}
```

Register all fourteen, adding the needed includes (`UTF8String.h`, `IA5String.h`, `PrintableString.h`, `NumericString.h`, `VisibleString.h`, `GeneralString.h`, `GraphicString.h`, `TeletexString.h`, `T61String.h`, `VideotexString.h`, `BMPString.h`, `UniversalString.h`, `ObjectDescriptor.h`, `ISO646String.h`, `GeneralizedTime.h`, `UTCTime.h`):

```c
    { &asn_OP_UTF8String,       vn_h_string, "UTF8String"       },
    { &asn_OP_IA5String,        vn_h_string, "IA5String"        },
    { &asn_OP_PrintableString,  vn_h_string, "PrintableString"  },
    { &asn_OP_NumericString,    vn_h_string, "NumericString"    },
    { &asn_OP_VisibleString,    vn_h_string, "VisibleString"    },
    { &asn_OP_ISO646String,     vn_h_string, "ISO646String"     },
    { &asn_OP_GeneralString,    vn_h_string, "GeneralString"    },
    { &asn_OP_GraphicString,    vn_h_string, "GraphicString"    },
    { &asn_OP_TeletexString,    vn_h_string, "TeletexString"    },
    { &asn_OP_T61String,        vn_h_string, "T61String"        },
    { &asn_OP_VideotexString,   vn_h_string, "VideotexString"   },
    { &asn_OP_BMPString,        vn_h_string, "BMPString"        },
    { &asn_OP_UniversalString,  vn_h_string, "UniversalString"  },
    { &asn_OP_ObjectDescriptor, vn_h_string, "ObjectDescriptor" },
    { &asn_OP_GeneralizedTime,  vn_h_string, "GeneralizedTime"  },
    { &asn_OP_UTCTime,          vn_h_string, "UTCTime"          },
```

Some of these headers may not exist as separate files for aliased types; if a header is missing, take the `extern` declaration from the type's own header as asn1c generates it. Verify with `ls /usr/local/share/asn1c/`.

- [ ] **Step 5: Run test to verify it passes**

Run: `make check`
Expected: `t_strings: ok`.

- [ ] **Step 6: Commit**

```bash
git add src tests Makefile
git commit -m "feat: restricted string types and time types as X.680 cstrings

Quotes are doubled per 11.14. BMPString and UniversalString are
transcoded from UTF-16BE/UTF-32BE to UTF-8. Control characters have no
cstring form, so they fail unless VN_F_LENIENT is set."
```

---

## Task 10: Bare ANY and table-constrained OPEN TYPE

**Files:**
- Modify: `src/vn_primitive.c`, `src/vn_constructed.c`, `src/vn_encoder.c`, `src/vn_internal.h`
- Create: `tests/schemas/opentype.asn1`, `tests/t_opentype.c`
- Modify: `Makefile` (`SCHEMAS += opentype`, `TESTS += t_opentype`, `t_opentype_SCHEMA := opentype`)

**Interfaces:**
- Produces: `vn_h_any` (hex, the one documented X.680 deviation) and `vn_h_open_type` (recurses into the resolved type).

- [ ] **Step 1: Write the test schema**

```asn1
-- tests/schemas/opentype.asn1
OpenType DEFINITIONS AUTOMATIC TAGS ::= BEGIN

MY-CLASS ::= CLASS { &id INTEGER UNIQUE, &Type } WITH SYNTAX { &Type IDENTIFIED BY &id }

Set1 MY-CLASS ::= { { UTF8String IDENTIFIED BY 1 } | { BOOLEAN IDENTIFIED BY 2 } }

Msg ::= SEQUENCE {
    id   MY-CLASS.&id   ({Set1}),
    body MY-CLASS.&Type ({Set1}{@id})
}

Opaque ::= SEQUENCE {
    id   INTEGER,
    blob ANY
}

END
```

- [ ] **Step 2: Write the failing test**

```c
/* tests/t_opentype.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "Msg.h"
#include "Opaque.h"

int
main(void) {
    char reason[200], *out;

    VNT_CASE("bare ANY is emitted as hex");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x04, 0x03, 0x01, 0x02, 0x03};
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        out = vnt_encode(&asn_DEF_Opaque, &o, 0, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "'0403010203'H") != 0);
        free(out);
    }

    VNT_CASE("bare ANY fails under VN_F_STRICT_ANY");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x05, 0x00};
        vn_options_t opt;
        memset(&opt, 0, sizeof opt);
        opt.flags = VN_F_STRICT_ANY;
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        VNT_TRUE(vnt_encode_fails(&asn_DEF_Opaque, &o, &opt, reason,
                                  sizeof reason));
        VNT_TRUE(strstr(reason, "ANY") != 0);
    }

    VNT_CASE("annotated mode marks bare ANY as unrendered");
    {
        Opaque_t o;
        const unsigned char raw[] = {0x05, 0x00};
        vn_options_t opt;
        memset(&opt, 0, sizeof opt);
        opt.mode = VN_MODE_ANNOTATED;
        memset(&o, 0, sizeof o);
        o.id = 1;
        o.blob.buf = (uint8_t *)raw;
        o.blob.size = sizeof raw;
        out = vnt_encode(&asn_DEF_Opaque, &o, &opt, reason, sizeof reason);
        VNT_TRUE(out && strstr(out, "ANY") != 0);
        free(out);
    }

    /* A table-constrained open type is not opaque: asn1c has already decoded
     * it into the right structure, so real value notation is possible. */
    VNT_CASE("open type renders the resolved alternative");
    {
        Msg_t *m = 0;
        /* id 1 selects UTF8String; body carries "hi" */
        const unsigned char der[] = {
            0x30, 0x09,
            0x80, 0x01, 0x01,             /* id 1 */
            0xa1, 0x04, 0x0c, 0x02, 'h', 'i'  /* body: UTF8String "hi" */
        };
        asn_dec_rval_t rv = ber_decode(0, &asn_DEF_Msg, (void **)&m, der,
                                       sizeof der);
        VNT_TRUE(rv.code == RC_OK);
        if(rv.code == RC_OK) {
            out = vnt_encode(&asn_DEF_Msg, m, 0, reason, sizeof reason);
            VNT_TRUE(out && strstr(out, "\"hi\"") != 0);
            VNT_TRUE(out && strstr(out, "'") == 0); /* not hex */
            free(out);
        }
        ASN_STRUCT_FREE(asn_DEF_Msg, m);
    }

    return vnt_report("t_opentype");
}
```

If the hand-built DER does not decode, print the bytes asn1c produces instead: build a `Msg_t` in code, `der_encode` it to a buffer, and hexdump that to get the correct literal. Do not weaken the test to make it pass.

- [ ] **Step 3: Run test to verify it fails**

Run: `make check`
Expected: `t_opentype` reports failures for all cases.

- [ ] **Step 4: Implement**

In `src/vn_primitive.c`:

```c
#include <ANY.h>

/*
 * A bare ANY carries no type information at runtime -- asn1c compiles it to an
 * OCTET STRING with subvariant ASN_OSUBV_ANY -- so there is no way to render
 * the value it contains. Emitting hex is the one deliberate deviation from
 * X.680 in this encoder; see README "Deviations from X.680".
 */
int
vn_h_any(vn_writer_t *w, const asn_TYPE_descriptor_t *td, const void *sptr,
         int level) {
    const ANY_t *any = (const ANY_t *)sptr;

    if(w->flags & VN_F_STRICT_ANY)
        return vn_fail(w, td, sptr,
                       "bare ANY cannot be rendered as value notation because "
                       "the runtime carries no type information for it");
    if(vn_put_hex(w, any->buf, any->buf ? any->size : 0, level) < 0) return -1;
    if(vn_is_annotated(w)) {
        if(vn_putc(w, ' ') < 0) return -1;
        return vn_comment(w, "bare ANY, %zu octets, not X.680 value notation",
                          any->buf ? any->size : (size_t)0);
    }
    return 0;
}
```

In `src/vn_constructed.c`:

```c
#include <OPEN_TYPE.h>

/*
 * A table-constrained open type resolves to a concrete descriptor: asn1c stores
 * the decoded value under the selected alternative, exactly like a CHOICE.
 */
int
vn_h_open_type(vn_writer_t *w, const asn_TYPE_descriptor_t *td,
               const void *sptr, int level) {
    return vn_h_choice(w, td, sptr, level);
}
```

Register:

```c
    { &asn_OP_ANY,       vn_h_any,       "ANY"       },
    { &asn_OP_OPEN_TYPE, vn_h_open_type, "OPEN TYPE" },
```

If the open-type test shows that `asn_OP_OPEN_TYPE` descriptors do not carry `asn_CHOICE_specifics_t`, read the actual layout from the generated `Msg.c` and adapt `vn_h_open_type` to that, rather than forcing the CHOICE path.

- [ ] **Step 5: Run test to verify it passes**

Run: `make check`
Expected: `t_opentype: ok`.

- [ ] **Step 6: Commit**

```bash
git add src tests Makefile
git commit -m "feat: bare ANY as hex, table-constrained open types rendered fully

Bare ANY has no runtime type information, so hex is the only option and
is the encoder's single documented deviation from X.680. Table-constrained
open types resolve to a real descriptor and render as value notation."
```

---

## Task 11: Golden files and an independent well-formedness scanner

**Files:**
- Create: `tests/vnscan.h`, `tests/vnscan.c`, `tests/t_scan.c`, `tests/t_golden.c`, `tests/golden/*.vn`, `tests/fixtures/`
- Modify: `Makefile` (`TESTS += t_scan t_golden`, both `_SCHEMA := constructed`)

**Interfaces:**
- Produces: `int vn_scan_wellformed(const char *text, char *err, size_t errlen)` returning 1 when the text is structurally valid value notation; `int vn_scan_scalars(const char *text, char **out, size_t max, size_t *count, char *err, size_t errlen)` extracting scalar values in document order. The scalar extractor is consumed by Task 12.

- [ ] **Step 1: Write the failing test for the scanner**

```c
/* tests/t_scan.c -- the scanner is test-only, so it gets its own tests */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vnscan.h"

static void
ok(const char *text) {
    char err[160];
    VNT_CASE(text);
    if(!vn_scan_wellformed(text, err, sizeof err)) {
        fprintf(stderr, "FAIL: expected well-formed, got: %s\n  in: %s\n",
                err, text);
        vnt_failures++;
    }
}

static void
bad(const char *text) {
    char err[160];
    VNT_CASE(text);
    if(vn_scan_wellformed(text, err, sizeof err)) {
        fprintf(stderr, "FAIL: expected malformed but accepted: %s\n", text);
        vnt_failures++;
    }
}

int
main(void) {
    ok("TRUE");
    ok("{ }");
    ok("{ a 1, b TRUE }");
    ok("{\n    a 1,\n    b { c '00FF'H }\n}");
    ok("alt : { x 1 }");
    ok("{ 1, 2, 3 }");
    ok("\"a string with , and { inside\"");
    ok("'0110'B");
    ok("{ 2 23 143 1 }");
    ok("green -- (1) --");
    ok("{ a 1 -- comment -- , b 2 }");

    bad("{ a 1,");            /* unclosed brace */
    bad("{ a 1 }}");          /* extra close */
    bad("{ a 1,, b 2 }");     /* double comma */
    bad("{ a 1, }");          /* trailing comma */
    bad("{ , a 1 }");         /* leading comma */
    bad("\"unterminated");    /* unterminated cstring */
    bad("'00FF");             /* unterminated hstring */
    bad("alt :");             /* choice with no value */

    /* scalar extraction, in document order */
    VNT_CASE("scalar extraction");
    {
        char *vals[8];
        size_t n = 0, i;
        char err[160];
        int rc = vn_scan_scalars("{ a 1, b TRUE, c { d '0A'H, e \"x\" } }",
                                 vals, 8, &n, err, sizeof err);
        VNT_TRUE(rc == 1);
        VNT_TRUE(n == 4);
        if(n == 4) {
            VNT_STREQ(vals[0], "1");
            VNT_STREQ(vals[1], "TRUE");
            VNT_STREQ(vals[2], "'0A'H");
            VNT_STREQ(vals[3], "\"x\"");
        }
        for(i = 0; i < n; i++) free(vals[i]);
    }

    return vnt_report("t_scan");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make check`
Expected: link failure for `vn_scan_wellformed` and `vn_scan_scalars`.

- [ ] **Step 3: Implement the scanner**

Write `tests/vnscan.c` as a single-pass tokeniser over the text. Requirements, each mapping to a case above:

- Track brace depth; a close with depth 0 or a non-zero depth at end of input is malformed.
- Skip `--` comments to the next `--` or newline, and never interpret their contents.
- Skip `"…"` cstrings, treating `""` as an escaped quote; unterminated is malformed.
- Skip `'…'H` and `'…'B` strings; unterminated is malformed.
- Between values inside braces, exactly one comma is permitted; a comma directly after `{`, directly before `}`, or immediately following another comma is malformed.
- After a `:` there must be a value token before the enclosing `}` or end of input.
- A *scalar* is any value token that is not `{`: a number, an identifier, `TRUE`/`FALSE`/`NULL`, a cstring, an hstring or a bstring. When a token sequence inside braces contains no commas and consists only of numbers, treat it as an OID arc list and emit each arc as its own scalar, matching `{ 2 23 143 1 }` in the test.
- `vn_scan_scalars` returns copies via `strdup`; the caller frees them.

Keep it under about 200 lines. It is a test fixture, not a parser: it decides well-formedness and pulls out scalars, nothing more.

- [ ] **Step 4: Run test to verify it passes**

Run: `make check`
Expected: `t_scan: ok`.

- [ ] **Step 5: Write the golden-file test**

```c
/* tests/t_golden.c */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vnscan.h"
#include "Nested.h"
#include "Wrapper.h"

/* Build a fully populated Nested value; static storage keeps it simple. */
static Nested_t *
make_nested(void) {
    static Nested_t n;
    static const unsigned char name[] = {0xde, 0xad, 0xbe, 0xef};
    static const unsigned char tag[] = {0x01, 0x02};
    static long opt = 42;
    memset(&n, 0, sizeof n);
    n.name.buf = (uint8_t *)name; n.name.size = sizeof name;
    n.inner.id = 5;
    n.inner.tag.buf = (uint8_t *)tag; n.inner.tag.size = sizeof tag;
    n.col = 2;
    n.opt = &opt;
    return &n;
}

static void
check_mode(const char *label, vn_mode_e mode, const char *path,
           const asn_TYPE_descriptor_t *td, const void *sptr) {
    vn_options_t o;
    char reason[200], err[200];
    char *out, *want = 0;
    long len;
    FILE *f;

    VNT_CASE(label);
    memset(&o, 0, sizeof o);
    o.mode = mode;
    out = vnt_encode(td, sptr, &o, reason, sizeof reason);
    if(!out) {
        fprintf(stderr, "FAIL [%s]: encode failed: %s\n", label, reason);
        vnt_failures++;
        return;
    }

    /* Whatever we emit must be well-formed value notation in every mode. */
    if(!vn_scan_wellformed(out, err, sizeof err)) {
        fprintf(stderr, "FAIL [%s]: output is malformed: %s\n  %s\n", label,
                err, out);
        vnt_failures++;
    }

    f = fopen(path, "rb");
    if(!f) { /* first run: record the golden file, then review it by hand */
        fprintf(stderr, "NOTE [%s]: writing new golden file %s -- review it "
                        "against X.680 before committing\n", label, path);
        f = fopen(path, "wb");
        if(f) { fwrite(out, 1, strlen(out), f); fclose(f); }
        free(out);
        vnt_failures++; /* never let a self-written golden count as a pass */
        return;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    want = (char *)malloc((size_t)len + 1);
    if(want && fread(want, 1, (size_t)len, f) == (size_t)len) want[len] = '\0';
    fclose(f);

    VNT_STREQ(out, want);
    free(out);
    free(want);
}

int
main(void) {
    const Nested_t *n = make_nested();
    check_mode("nested pretty", VN_MODE_PRETTY,
               "tests/golden/nested.pretty.vn", &asn_DEF_Nested, n);
    check_mode("nested canonical", VN_MODE_CANONICAL,
               "tests/golden/nested.canonical.vn", &asn_DEF_Nested, n);
    check_mode("nested annotated", VN_MODE_ANNOTATED,
               "tests/golden/nested.annotated.vn", &asn_DEF_Nested, n);
    return vnt_report("t_golden");
}
```

- [ ] **Step 6: Generate and review the golden files**

Run: `make check` (fails, writing three files under `tests/golden/`)
Then **read each file and check it against X.680 by hand.** Specifically: are field names right, is every brace balanced, is the hex uppercase, does canonical use two spaces, do annotated comments open and close with `--`? Fix the encoder if any answer is no; only then treat the files as golden.

- [ ] **Step 7: Run test to verify it passes**

Run: `make check`
Expected: `t_golden: ok`.

- [ ] **Step 8: Commit**

```bash
git add tests Makefile
git commit -m "test: golden files per mode and an independent VN scanner

The scanner checks brace balance, comma placement and string termination,
covering the structural errors a value-only comparison cannot see. A
freshly written golden file always counts as a failure so it cannot be
mistaken for a reviewed one."
```

---

## Task 12: XER cross-check and property-based testing

The semantic oracle. See "Deviation from spec §7" above for why this compares scalar sequences rather than paths.

**Files:**
- Create: `tests/xerscan.h`, `tests/xerscan.c`, `tests/t_xercheck.c`, `tests/schemas/kitchen.asn1`
- Modify: `Makefile` (`SCHEMAS += kitchen`, `TESTS += t_xercheck`, `t_xercheck_SCHEMA := kitchen`)

**Interfaces:**
- Consumes: `vn_scan_scalars` from Task 11.
- Produces: `int xer_scan_scalars(const char *xer, char **out, size_t max, size_t *count, char *err, size_t errlen)`, and `char *vn_norm_scalar(const char *s)` normalising either dialect's scalar to a comparable form.

- [ ] **Step 1: Write the kitchen-sink schema**

```asn1
-- tests/schemas/kitchen.asn1 -- every supported type family in one PDU
Kitchen DEFINITIONS AUTOMATIC TAGS ::= BEGIN

Colour ::= ENUMERATED { red(0), green(1), blue(2) }

Leaf ::= SEQUENCE {
    id   INTEGER,
    tag  OCTET STRING
}

Top ::= SEQUENCE {
    flag    BOOLEAN,
    void    NULL,
    small   INTEGER,
    col     Colour,
    data    OCTET STRING,
    bits    BIT STRING,
    oid     OBJECT IDENTIFIER,
    text    UTF8String,
    ia5     IA5String,
    leaf    Leaf,
    numbers SEQUENCE OF INTEGER,
    leaves  SEQUENCE OF Leaf,
    pick    CHOICE { nothing NULL, flag BOOLEAN, leaf Leaf },
    opt     INTEGER OPTIONAL
}

END
```

- [ ] **Step 2: Write the failing test**

```c
/* tests/t_xercheck.c
 *
 * Semantic oracle: asn1c's XER encoder and ours walk the same structure in
 * descriptor order, so the ordered sequence of scalar values must match.
 * asn1c's XER shares no code with this encoder, which is what makes it an
 * independent check.
 */
#include <stdlib.h>
#include <string.h>
#include "vntest.h"
#include "vnscan.h"
#include "xerscan.h"
#include <asn_random_fill.h>
#include "Top.h"

#define MAX_SCALARS 512

static int
xer_consume(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

/* Returns 0 on mismatch, having reported it. */
static int
compare_one(const void *sptr, int iteration) {
    vnt_str_t xer;
    char *vn = 0;
    char *xs[MAX_SCALARS], *vs[MAX_SCALARS];
    size_t xn = 0, vn_count = 0, i;
    char reason[256], err[256];
    int ok = 1;

    memset(&xer, 0, sizeof xer);
    if(xer_encode(&asn_DEF_Top, sptr, XER_F_BASIC, xer_consume, &xer).encoded
       < 0) {
        fprintf(stderr, "FAIL iter %d: asn1c's own XER encoder failed\n",
                iteration);
        vnt_failures++;
        free(xer.buf);
        return 0;
    }

    vn = vnt_encode(&asn_DEF_Top, sptr, 0, reason, sizeof reason);
    if(!vn) {
        fprintf(stderr, "FAIL iter %d: vn_encode failed: %s\n", iteration,
                reason);
        vnt_failures++;
        free(xer.buf);
        return 0;
    }

    if(!vn_scan_wellformed(vn, err, sizeof err)) {
        fprintf(stderr, "FAIL iter %d: VN malformed: %s\n%s\n", iteration, err,
                vn);
        vnt_failures++;
        ok = 0;
    }

    if(!xer_scan_scalars(xer.buf, xs, MAX_SCALARS, &xn, err, sizeof err)
       || !vn_scan_scalars(vn, vs, MAX_SCALARS, &vn_count, err, sizeof err)) {
        fprintf(stderr, "FAIL iter %d: scan failed: %s\n", iteration, err);
        vnt_failures++;
        ok = 0;
    } else if(xn != vn_count) {
        fprintf(stderr, "FAIL iter %d: %zu XER scalars vs %zu VN scalars\n"
                        "--- XER ---\n%s\n--- VN ---\n%s\n",
                iteration, xn, vn_count, xer.buf, vn);
        vnt_failures++;
        ok = 0;
    } else {
        for(i = 0; i < xn; i++) {
            char *a = vn_norm_scalar(xs[i]);
            char *b = vn_norm_scalar(vs[i]);
            if(!a || !b || strcmp(a, b) != 0) {
                fprintf(stderr, "FAIL iter %d: scalar %zu differs: "
                                "XER |%s| -> |%s| vs VN |%s| -> |%s|\n",
                        iteration, i, xs[i], a ? a : "?", vs[i], b ? b : "?");
                vnt_failures++;
                ok = 0;
            }
            free(a);
            free(b);
        }
    }

    for(i = 0; i < xn; i++) free(xs[i]);
    for(i = 0; i < vn_count; i++) free(vs[i]);
    free(xer.buf);
    free(vn);
    return ok;
}

int
main(int argc, char **argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 200;
    int i, built = 0;

    VNT_CASE("random values agree with asn1c's XER");
    for(i = 0; i < rounds; i++) {
        void *st = 0;
        /* vary the budget so both tiny and deep values occur */
        size_t budget = 16 + (size_t)(i % 96);
        if(asn_random_fill(&asn_DEF_Top, &st, budget) != ARFILL_OK) continue;
        built++;
        compare_one(st, i);
        ASN_STRUCT_FREE(asn_DEF_Top, st);
    }

    VNT_CASE("random fill produced values at all");
    VNT_TRUE(built > rounds / 4);
    if(built <= rounds / 4)
        fprintf(stderr, "only %d of %d rounds produced a value\n", built,
                rounds);

    return vnt_report("t_xercheck");
}
```

This needs a small growable-string helper shared with the harness. Add to `tests/vntest.h` / `tests/vntest.c`:

```c
typedef struct { char *buf; size_t len, cap; } vnt_str_t;
int vnt_append(vnt_str_t *s, const void *data, size_t size);
```

`vnt_append` is the same growth logic already used by `vnt_consume`; refactor `vnt_consume` to call it rather than duplicating it.

- [ ] **Step 3: Run test to verify it fails**

Run: `make check`
Expected: link failure for `xer_scan_scalars`, `vn_norm_scalar`, `vnt_append`.

- [ ] **Step 4: Implement the XER scanner and normaliser**

`tests/xerscan.c` walks the XER text and emits one scalar per element that has text content or is an empty element used as a value:

- `<tag>text</tag>` → emit `text`.
- `<tag/>` → emit `tag` (this is how asn1c writes BOOLEAN and ENUMERATED: `<true/>`, `<green/>`).
- A tag containing only child elements emits nothing of its own.
- `<tag>` immediately followed by `</tag>` emits the empty string — this is how an empty OCTET STRING and an empty UTF8String appear.

`vn_norm_scalar` maps either dialect onto one comparable form. Every rule below is required by something the spike showed:

| Input | Normalised |
| --- | --- |
| `TRUE`, `true` | `B:1` |
| `FALSE`, `false` | `B:0` |
| `NULL` | `N:` |
| `'00AABB'H` | `H:00AABB` |
| `'0110'B` | `H:` + hex of the bits when the count divides by 4, else `Z:0110` |
| `45 00 F8 8A` (XER hex, space- and newline-separated) | `H:4500F88A` |
| `00011000011101` (XER bits) | `Z:00011000011101`, or `H:` when divisible by 4 |
| `"text"` | `S:text`, with `""` collapsed to `"` |
| `text` (XER element content) | `S:text` |
| `2.23.143.1` (XER OID) | `O:2.23.143.1` |
| `2`, `23`, `143`, `1` (consecutive VN arcs) | joined to `O:2.23.143.1` |
| `-12345` | `I:-12345` |
| `green` | `S:green` |

Two cases need care, both found in the spike:

- **XER wraps long hex and bit values across lines with spaces.** Strip all whitespace inside a scalar before interpreting it.
- **BIT STRING crosses forms.** VN emits `'AB'H` for 8 bits but `'0110'B` for 4; XER always emits bits. Normalise both to bits first, then to `H:` when the count divides by 4, so the two always meet.

An unrecognised scalar shape must return `NULL`, which the test reports as a failure. Never fall through to "equal".

For the OID arc joining, `vn_scan_scalars` already emits arcs individually (Task 11); have `vn_norm_scalar`'s caller join runs of bare integers that came from a brace group with no commas. Simplest correct approach: give `vn_scan_scalars` an out-parameter marking arc-run members, and join them in `compare_one` before normalising. Implement that rather than guessing from the value shape, since `{ 1, 2, 3 }` as a SEQUENCE OF INTEGER must stay three scalars.

- [ ] **Step 5: Run test to verify it passes**

Run: `make check`
Expected: `t_xercheck: ok`. Expect genuine encoder bugs to surface here — that is the point. Fix the encoder, not the normaliser, unless the normaliser is demonstrably wrong about the two dialects.

- [ ] **Step 6: Run a longer campaign**

Run: `./tests/bin/t_xercheck 5000`
Expected: `ok`. Investigate every failure; note that `asn_random_fill` uses `rand()` without seeding, so runs are reproducible.

- [ ] **Step 7: Commit**

```bash
git add tests Makefile
git commit -m "test: XER cross-check driven by asn_random_fill

Both encoders visit members in descriptor order, so the ordered scalar
sequence must match. asn1c's XER encoder shares no code with ours, making
it an independent oracle; asn_random_fill supplies thousands of values
without hand-written fixtures.

Compares scalar sequences rather than the (path, scalar) pairs the spec
described: XER names list elements by type while value notation gives
them no name, so comparable paths would require schema knowledge in a
harness meant to be schema-free. Structural coverage comes from the VN
well-formedness scanner instead."
```

---

## Task 13: CLI tool and documentation

**Files:**
- Create: `tools/asn1vn.c`, `README.md`
- Modify: `docs/design/01-encoder.md` (correct the XER hex format and record the oracle change)

**Interfaces:**
- Consumes: `vn_fprint`, `vn_options_t`.
- Produces: the `asn1vn` binary.

- [ ] **Step 1: Write the CLI**

```c
/*
 * asn1vn.c -- read DER from stdin, decode it as the -DPDU root type, and
 * write ASN.1 value notation to stdout.
 *
 * Same shape as asn1c's converter-example, but with vn_encode as the output
 * stage. The root type arrives at compile time via -DPDU=<TypeName>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asn_application.h>
#include "vn_encoder.h"

#define VN_CAT_(a, b) a##b
#define VN_CAT(a, b) VN_CAT_(a, b)
#define VN_PDU_DEF VN_CAT(asn_DEF_, PDU)

extern asn_TYPE_descriptor_t VN_PDU_DEF;

static void
usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [-c|-a] [-l WIDTH] [-L] < input.der\n"
            "  -c        canonical output (deterministic, for diffing)\n"
            "  -a        annotated output (adds X.680 comments)\n"
            "  -l WIDTH  wrap hex at WIDTH columns (0 disables)\n"
            "  -L        lenient: emit questionable values instead of failing\n",
            argv0);
}

int
main(int argc, char **argv) {
    vn_options_t opts;
    char reason[256] = "";
    unsigned char *buf;
    size_t cap = 1 << 16, len = 0, n;
    void *st = 0;
    asn_dec_rval_t rv;
    int i;

    memset(&opts, 0, sizeof opts);
    opts.mode = VN_MODE_PRETTY;
    opts.errbuf = reason;
    opts.errlen = sizeof reason;

    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-c") == 0)      opts.mode = VN_MODE_CANONICAL;
        else if(strcmp(argv[i], "-a") == 0) opts.mode = VN_MODE_ANNOTATED;
        else if(strcmp(argv[i], "-L") == 0) opts.flags |= VN_F_LENIENT;
        else if(strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            opts.line_width = atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }

    buf = (unsigned char *)malloc(cap);
    if(!buf) { perror("malloc"); return 2; }
    while((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if(len == cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, cap * 2);
            if(!nb) { perror("realloc"); free(buf); return 2; }
            buf = nb;
            cap *= 2;
        }
    }
    if(ferror(stdin)) { perror("read"); free(buf); return 2; }

    rv = asn_decode(0, ATS_BER, &VN_PDU_DEF, &st, buf, len);
    free(buf);
    if(rv.code != RC_OK) {
        fprintf(stderr, "%s: BER/DER decode failed (code %d) after %zu bytes\n",
                argv[0], (int)rv.code, rv.consumed);
        if(st) ASN_STRUCT_FREE(VN_PDU_DEF, st);
        return 1;
    }

    if(vn_fprint(stdout, &VN_PDU_DEF, st, &opts) < 0) {
        fprintf(stderr, "%s: cannot render value notation: %s\n", argv[0],
                reason[0] ? reason : "unknown error");
        ASN_STRUCT_FREE(VN_PDU_DEF, st);
        return 1;
    }
    fputc('\n', stdout);

    ASN_STRUCT_FREE(VN_PDU_DEF, st);
    return 0;
}
```

- [ ] **Step 2: Build and run it against a real generated directory**

```bash
make asn1vn GEN_DIR=tests/gen/kitchen PDU=Top
```

Produce a DER fixture to feed it — the simplest route is a throwaway program using `asn_random_fill` plus `der_encode` — then:

```bash
./asn1vn < /tmp/top.der
./asn1vn -c < /tmp/top.der
./asn1vn -a < /tmp/top.der
```

Expected: three renderings of the same value, differing only in indentation, wrapping and comments. Confirm the exit status is 0 and that a truncated input yields exit 1 with a decode error on stderr rather than partial output.

- [ ] **Step 3: Write the README**

Cover, in this order: what the project is and that it is output-only; that value notation is the one syntax asn1c lacks; the addon-not-fork rationale; **ABI pinning to asn1c 0.9.29** with the tested tree `v0.9.29-7-g8a274c3f` and a note that `td->op` dispatch is the version-sensitive part; building `libvn.a` and `asn1vn` with `GEN_DIR` and `PDU`; the `-D_DARWIN_C_SOURCE` requirement on macOS; the three output modes with a short example of each; the `VN_F_*` flags; running `make check` and that `asn1c` must be on `PATH`.

Then a **Deviations from X.680** section, stating plainly:

- Bare `ANY` is emitted as a hex string. The runtime keeps no type information for it, so no correct value notation exists. `VN_F_STRICT_ANY` turns this into an error. Table-constrained open types are unaffected and render fully.
- BIT STRING named bit lists and INTEGER named numbers are not emitted; asn1c does not retain them in the generated descriptors. The numeric and hex forms used instead are equally valid X.680.
- REAL is not supported and fails loudly.
- Control characters in strings fail loudly; X.680's character-defs form is not implemented. `VN_F_LENIENT` emits them raw, which produces text outside the standard.
- SET OF elements are never reordered.

- [ ] **Step 4: Correct the spec**

In `docs/design/01-encoder.md` §7, change the XER example from `<o>00AABB</o>` to the real space-separated form, and add a short note that the implemented oracle compares scalar sequences plus a well-formedness scan, with the reason. Keep the spec honest about what was built.

- [ ] **Step 5: Run the full suite once more**

Run: `make clean && make check && ./tests/bin/t_xercheck 5000`
Expected: every suite `ok`, no compiler warnings.

- [ ] **Step 6: Commit**

```bash
git add tools README.md docs Makefile
git commit -m "feat: asn1vn CLI, README and spec corrections

The CLI reads DER on stdin and writes value notation on stdout, with the
root type supplied at compile time via -DPDU. README documents the ABI
pin and every deviation from X.680."
```

---

## Task 14 (optional): Continuous integration

Strike this task if CI is not wanted; nothing depends on it.

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write the workflow**

Matrix over `ubuntu-latest` and `macos-latest`. Each job: check out this repo; check out `vlm/asn1c` at tag `v0.9.29`; build and install it (`test -f configure || autoreconf -iv; ./configure; make; sudo make install`); then `make check` and `./tests/bin/t_xercheck 2000` in this repo. Cache the asn1c build keyed on the tag.

- [ ] **Step 2: Verify**

Push a branch and confirm both jobs pass. A macOS failure that Linux does not show is most likely the `-D_DARWIN_C_SOURCE` guard.

- [ ] **Step 3: Commit**

```bash
git add .github
git commit -m "ci: build asn1c from source and run the suite on Linux and macOS"
```

---

## Self-Review

**Spec coverage.** Spec §2 dispatch → Task 3. §2.2 ABI pin → Tasks 1, 13. §3 ABI limits → documented in Task 13, enforced by Task 8 (no named bits). §4 API → Task 1; §4.1 modes → Tasks 2, 6, 11. §5.1 mappings → Tasks 3, 4, 5, 6, 7, 8, 9. §5.2 bignum → Task 4; BIT STRING form → Task 8; bare ANY and open types → Task 10; strings and transcoding → Task 9; absent/default → Task 6; SET OF order → Task 7 (preserved by construction, asserted in Task 12); REAL → Task 3's negative test. §5.3 failure behaviour → Tasks 3, 4, 7, 9, 10. §6 layout → Task 1. §7 testing → Tasks 11, 12, with the recorded deviation. §8 order → this plan's task order. §9 non-goals → nothing implements them.

**Gap found and closed.** The spec requires DEFAULT-valued members always to be emitted. Task 6's `Nested` has `deflt INTEGER DEFAULT 7`, but no test asserted the behaviour. asn1c represents a DEFAULT member as a pointer, so an unset one is indistinguishable from an absent OPTIONAL and *will* be omitted — which contradicts the spec. Resolution: Task 6 gains a case asserting that a DEFAULT member holding an explicit value is emitted, and Task 13's README states that a DEFAULT member the decoder left unset is omitted, since the runtime cannot distinguish it. This is a documentation fix to the spec's §5.2 claim, not an encoder feature.

**Placeholder scan.** No TBD or "handle edge cases" remains. Task 11's scanner and Task 12's normaliser are specified as behaviour tables with a test per row rather than full code, because both are test fixtures whose exact structure follows from their test; every required behaviour is enumerated.

**Type consistency.** `vn_writer_t`, `vn_options_t`, `vn_handler_f`, `vn_encode_value`, `vn_member_ptr`, `vn_put_hex`, `vn_h_*` are used with the same signatures throughout. `vnt_str_t`/`vnt_append` are introduced in Task 12 and refactored out of Task 1's `vnt_consume` there, which is the only backward edit in the plan.
