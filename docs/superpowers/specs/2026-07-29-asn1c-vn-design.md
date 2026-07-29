# asn1c-vn — ASN.1 Value Notation encoder for asn1c

**Status:** design approved, not yet implemented
**Date:** 2026-07-29
**Normative reference:** ITU-T X.680 (02/2021) = ISO/IEC 8824-1:2021, *Abstract Syntax
Notation One (ASN.1): Specification of basic notation*. Freely available from the ITU at
<https://www.itu.int/rec/T-REC-X.680>; the X.68x series is not paywalled.

## 1. Purpose

`asn1c-vn` serialises a decoded [vlm/asn1c](https://github.com/vlm/asn1c) structure into
ASN.1 value notation as defined by X.680. Value notation is the one transfer syntax asn1c
does not ship: it can do BER, DER, XER, OER and PER, but not VN.

Scope is **output only** — structure to VN text. Reading VN back into a structure needs a
schema-driven parser and is deliberately out of scope; asn1c's XER support (`-ixer -oder`)
already covers the round-trip need.

## 2. Approach: runtime addon, not a fork

The module depends only on asn1c's **runtime ABI** — the generated `asn_TYPE_descriptor_t`
values and the skeleton headers such as `asn_application.h`. It does not touch
`libasn1compiler`. Consequences:

- it lives in its own repository instead of being a fork to rebase forever,
- it works with *any* asn1c output without regenerating anything,
- `vn_encode()` walks type descriptors generically, exactly as `xer_encoder.c` does for XER.

### 2.1 Type dispatch

asn1c 0.9.29 gives every built-in type its own operation table: `asn_OP_SEQUENCE`,
`asn_OP_UTF8String`, `asn_OP_BIT_STRING`, and so on — 35 in total, each a distinct global.
Dispatch is therefore an exact pointer comparison on `td->op` against a static table mapping
op table to handler.

Alternatives considered and rejected:

- **Tag heuristics via `td->tags`** — more tolerant of ABI drift, but ambiguous under
  `AUTOMATIC TAGS`, where member tags are context-specific.
- **Inspecting `td->op->xer_encoder`** — the same pointer comparison one level of
  indirection further away, with no added robustness.

An unrecognised op table is a hard error, never a guess. See §5.

### 2.2 ABI pinning

The dispatch depends on the descriptor layout, which differs across asn1c versions (shared
`asn_OP_*` tables in recent versions versus direct function pointers on the descriptor in
older ones). Pinned and tested against **asn1c 0.9.29**, specifically the tree at
`v0.9.29-7-g8a274c3f`. The README states this; a version bump is a deliberate,
test-verified change.

## 3. What the runtime ABI does and does not retain

This bounds what the encoder can possibly emit, so it is recorded here rather than
rediscovered later. Verified empirically by compiling a probe schema and reading the
generated descriptors.

| Retained in descriptors | Present only in the `.asn1` source |
| --- | --- |
| Type names, member names, CHOICE alternative names | BIT STRING named bit lists |
| ENUMERATED identifiers (`value2enum` map) | INTEGER named numbers |
| Tags, OPTIONAL/DEFAULT flags, default values | Source comments |
| Open type tables (`asn_ioc_set_t`, `type_selector`) | Subtype constraints (only partly, as PER/OER numbers) |

Concretely, `Flags ::= BIT STRING { keyCert(0), crlSign(1) }` compiles to a descriptor
pointing at the *generic* `asn_SPC_BIT_STRING_specs` — the bit names are gone.
`Level ::= INTEGER { low(0), medium(5) }` compiles to `0 /* No specifics */`. But
`Colour ::= ENUMERATED { red(0), green(1) }` keeps a full
`{ 0, 3, "red" }, { 1, 5, "green" }` map.

**This costs readability, not validity.** X.680 permits the named forms as alternatives,
never as requirements: `'0110'B` is as valid as `{ keyCert, crlSign }`, and `5` as valid as
`medium`. The one case where the identifier *is* mandatory is ENUMERATED — and that is
precisely the one the ABI preserves.

Recovering the lost names would mean parsing the `.asn1` source into a side-car annotation
table, which would make the addon schema-dependent. Out of scope for v1.

## 4. Public API

```c
typedef enum {
    VN_MODE_PRETTY,     /* human reading */
    VN_MODE_CANONICAL,  /* diffing, golden files */
    VN_MODE_ANNOTATED   /* pretty + X.680 comments */
} vn_mode_e;

typedef struct vn_options_s {
    vn_mode_e mode;
    int       indent_width;  /* pretty/annotated; default 4 */
    int       line_width;    /* hex wrap column; 0 = never wrap */
    unsigned  flags;         /* VN_F_* below */
    char     *errbuf;        /* optional human-readable failure reason */
    size_t    errlen;
} vn_options_t;

/* flags */
#define VN_F_LENIENT          0x01  /* emit questionable values instead of failing */
#define VN_F_ENUM_WITH_VALUE  0x02  /* `mode2 -- (2) --` instead of `mode2` */
#define VN_F_STRICT_ANY       0x04  /* fail on bare ANY instead of emitting hex */

asn_enc_rval_t vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                         const vn_options_t *opts,   /* NULL = pretty defaults */
                         asn_app_consume_bytes_f *cb, void *key);

int vn_fprint(FILE *, const asn_TYPE_descriptor_t *, const void *, const vn_options_t *);
```

Naming follows the asn1c family (`xer_encode`, `der_encode`, `oer_encode`) so the module
stays upstreamable. Returning `asn_enc_rval_t` keeps asn1c's encoder contract: `encoded` is
the byte count or `-1`, with `failed_type` and `structure_ptr` identifying the failure site.
`errbuf` adds the reason in plain text, so "fail loudly" also means "fail
comprehensibly".

Internally a `vn_writer_t` carries the callback, key, indent level, mode and error state
through the recursion instead of threading six parameters through every handler.

### 4.1 Output modes

- **PRETTY** — a constructed value (SEQUENCE, SET, SEQUENCE OF, SET OF) always breaks across
  lines with its members indented by `indent_width`; a scalar always stays on the same line
  as its field name. Hex longer than `line_width` wraps at a 2-digit boundary. Defaults:
  `indent_width = 4`, `line_width = 76`.
- **CANONICAL** — deterministic: one value per line, fixed two-space indent, no wrapping, no
  comments. `indent_width` and `line_width` are ignored, so two callers cannot produce
  differing canonical output for the same structure. Optimised for diffing and golden files,
  which is why it is *deterministic* rather than *minimal-whitespace* — one value per line
  diffs far better than a dense single line.
- **ANNOTATED** — PRETTY plus comments carrying type names, numeric enum values, absent
  optional members, and bare-`ANY` markers. Comments use the inline `-- text --` form when
  followed by more content on the line and the `-- text` end-of-line form otherwise; both
  are X.680-legal, so output stays parseable in every mode.

Enum identifiers are never annotated with a bare parenthesised number: X.680 defines
`EnumeratedValue ::= identifier`, so `green (1)` would not be value notation. The number is
carried in a comment instead — `green -- (1) --`.

## 5. Type semantics

### 5.1 Direct mappings

| Type | Output |
| --- | --- |
| BOOLEAN | `TRUE` / `FALSE` |
| NULL | `NULL` |
| INTEGER | signed decimal |
| ENUMERATED | identifier from `value2enum` |
| OCTET STRING | `'00AABB'H`, uppercase hex digits; empty → `''H` |
| OBJECT IDENTIFIER, RELATIVE-OID | `{ 2 23 143 1 }` — arcs space-separated, no commas |
| SEQUENCE, SET | `{ field value, field value }` |
| SEQUENCE OF, SET OF | `{ value, value }` |
| CHOICE | `alternative : value` |
| GeneralizedTime, UTCTime | cstring of the raw bytes: `"20260729124800Z"` |

### 5.2 Decisions where X.680 leaves a choice

**Large INTEGER.** asn1c itself gives up beyond `intmax_t` and prints `AA:BB:CC`
(`INTEGER.c:179-198`), which is not valid VN. We implement arbitrary-precision
binary-to-decimal conversion (repeated division by 10^9 over the byte array). A VN library
that fails on large integers would be half broken.

**BIT STRING.** X.680 allows both `bstring` and `hstring`. Deterministic rule: bit count
(`len * 8 - bits_unused`) non-zero and divisible by 4 → `'AB'H`, otherwise `'0110'B`; empty
→ `''B`. Named bit lists are not in the runtime ABI (§3) and are out of scope.

**Bare `ANY`.** A member typed `ANY` compiles to `asn_DEF_ANY` — an OCTET STRING with
subvariant `ASN_OSUBV_ANY` and no type information whatsoever. We emit hex `'..'H`. This is
**the one deliberate deviation from X.680** and is documented as such in the README;
ANNOTATED marks it with a comment, and `VN_F_STRICT_ANY` turns it into an error. Failing by
default would make the tool useless on real eSIM profiles.

**Table-constrained open types are a different case** and are *not* opaque: a member typed
`MY-CLASS.&Type ({Set1}{@id})` gets `asn_OP_OPEN_TYPE` plus an `asn_ioc_set_t` table and a
`type_selector` function. asn1c has already decoded it into the correct structure, so we
recurse normally and emit real value notation.

**Strings.** An embedded `"` is doubled, per X.680 11.14. UTF8String passes through as raw
UTF-8. BMPString and UniversalString are stored as UTF-16BE / UTF-32BE (indicated by
`subvariant` in the OCTET STRING specifics) and are transcoded to UTF-8. Control characters
cannot appear in a cstring — X.680 provides the character-defs form for those, which we do
not implement; they are an error by default, emitted raw under `VN_F_LENIENT`.

**Absent and default values.** An unset OPTIONAL member is omitted; ANNOTATED may note
`-- absent`.

> **Corrected during implementation.** This section originally claimed that members carrying
> their DEFAULT value are always emitted. That is not implementable against this ABI: asn1c
> represents a DEFAULT member as a pointer, so an unset one is indistinguishable at runtime
> from an absent OPTIONAL member and is omitted. A DEFAULT member holding an explicit value
> is emitted normally. Recorded in the README under "Deviations from X.680".

**SET OF** element order is preserved as decoded, never re-sorted.

**REAL** is out of scope for v1 and fails loudly.

### 5.3 Failure behaviour

Every failure path sets `encoded = -1`, fills `failed_type` and `structure_ptr`, and writes
a reason into `errbuf` when provided. Specifically: unknown op table, REAL, unknown
ENUMERATED value under `strict_enumeration`, control character in a restricted string
(without `VN_F_LENIENT`), and bare `ANY` under `VN_F_STRICT_ANY`. Silent or invented output
is never acceptable — a caller must not be able to mistake a partial encode for a complete
one.

## 6. Repository layout

```
asn1c-vn/
  Makefile              # POSIX make, C99, no dependencies beyond asn1c's skeletons
  LICENSE               # BSD-2-Clause, matching asn1c
  README.md
  include/vn_encoder.h  # public API
  src/vn_writer.c       # output sink: callback, indentation, mode, error state
  src/vn_encoder.c      # central type dispatch
  src/vn_primitive.c    # INTEGER, BOOLEAN, NULL, OCTET/BIT STRING, OID, strings, times
  src/vn_constructed.c  # SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE, OPEN TYPE
  tools/asn1vn.c        # example CLI: DER on stdin, VN on stdout
  tests/
    schemas/            # one .asn1 per type family, plus a kitchen-sink schema
    golden/             # expected output per schema per mode
    <drivers>.c
  docs/superpowers/specs/
```

Each source file has one job and can be read on its own: the writer knows about bytes and
indentation but no ASN.1 types; the dispatcher knows types but not their syntax; the two
handler files know syntax but not where output goes.

The CLI takes the root type by token pasting (`-DPDU=ProfileElement` →
`asn_DEF_ProfileElement`) and is linked against a generated directory via `GEN_DIR`, as in:

```sh
make GEN_DIR=$HOME/git/waigel/esim-gen PDU=ProfileElement
./asn1vn < profile_element.der
```

`-D_DARWIN_C_SOURCE` is required on macOS, where `GeneralizedTime.c` needs `struct tm` and
`timegm`.

## 7. Testing

Pure C, no Python. Four layers, in increasing strength:

1. **Golden files.** A DER fixture is encoded in all three modes and byte-compared against
   `tests/golden/<schema>.<mode>.vn`. Catches formatting regressions; says nothing about
   correctness.

2. **XER cross-check** — the actual semantic oracle. The same decoded structure is emitted
   twice: once as VN by us, once as XER by asn1c. Two small schema-agnostic tokenisers
   reduce both texts to a sequence of (path, scalar) pairs, normalise the scalar
   representations, and compare.

   | XER | VN |
   | --- | --- |
   | `<b><true/></b>` | `b TRUE` |
   | `<c><green/></c>` | `c green` |
   | `<o>FF FF 4C</o>` | `o 'FFFF4C'H` |
   | `<oid>2.23.143.1</oid>` | `oid { 2 23 143 1 }` |

   asn1c's XER encoder is well-exercised and shares no code with ours, which is what makes
   it an independent oracle. The normaliser **fails on any scalar shape it does not
   recognise** rather than skipping it; otherwise the comparison table becomes the place
   where bugs hide.

> **Corrected during implementation.** Two things here were wrong. First, asn1c writes
> OCTET STRING hex as space-separated pairs wrapped across lines, not the contiguous
> `00AABB` shown originally. Second, comparing **(path, scalar) pairs** is not achievable
> without schema knowledge, because XER names SEQUENCE OF elements by their type while value
> notation gives them no name at all. What was built compares ordered **scalar sequences**
> plus an independent well-formedness scan; see the plan's deviation note and the README's
> Testing section for exactly what that does and does not cover.

   asn1c is not usable as a syntax oracle, incidentally: its own parser treats value
   assignment bodies as opaque and accepts `<<<BROKEN>>>`, unknown field names and `'ZZ'H`
   with exit status 0. Verified during design.

3. **Property-based via `asn_random_fill`.** The op table exposes a per-type random value
   generator, so thousands of generated values can be pushed through layer 2 without
   hand-written fixtures. This is what finds empty strings, extreme values, deep nesting and
   `bits_unused != 0`.

4. **Negative tests.** REAL, an unknown op table, and an unknown ENUMERATED value under
   strict enumeration must each yield `-1` plus a reason — proving the encoder does not
   silently emit nonsense.

Where layer 2 legitimately diverges (bare `ANY`, time formats), there is a short, explicitly
maintained per-type exception list. No blanket skips.

`make check` runs `asn1c` on each test schema into `tests/gen/<schema>/`, builds a driver and
executes it. `asn1c` must be on `PATH`; the README says so.

CI (GitHub Actions, Linux + macOS, asn1c built from source) is an optional final step.

## 8. Implementation order

Each step ends with passing tests, so the tree is never broken:

1. Repository skeleton, `Makefile`, `LICENSE`, `README`, linkage proof against a generated
   directory.
2. `vn_writer.c` plus its tests — indentation, modes, error propagation, no ASN.1 yet.
3. Dispatch skeleton with the op table map; every type still an explicit "unsupported"
   error. Negative tests pass from here on.
4. Primitives: NULL, BOOLEAN, INTEGER (including bignum), ENUMERATED, OCTET STRING.
5. Test harness layers 1 and 2, running against what exists so far.
6. Constructed types: SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE.
7. Remaining primitives: BIT STRING, OID/RELATIVE-OID, all restricted strings,
   GeneralizedTime/UTCTime, transcoding.
8. OPEN TYPE and bare `ANY`.
9. Property-based layer 3.
10. `tools/asn1vn.c`, README polish, and the README's "Deviations from X.680" section: the
    one case where output departs from the standard (bare `ANY` as hex), plus the coverage
    gaps that fail loudly rather than deviate (REAL, control characters in strings) and the
    readability limits imposed by the runtime ABI (no named bits or named numbers).
11. Optional: GitHub Actions.

## 9. Explicit non-goals for v1

- Reading value notation back into a structure.
- REAL.
- Named bit lists and INTEGER named numbers (§3).
- The X.680 character-defs form for control characters in strings.
- Re-sorting SET OF.
- Any modification to asn1c itself. Upstreaming is a possible later step, not a v1 goal.
