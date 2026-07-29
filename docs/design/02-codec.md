# asn1c-vn as a value notation codec — design

**Status:** decided; sidecar implemented first, reader to follow
**Date:** 2026-07-29
**Normative reference:** ITU-T X.680 (02/2021)
**Predecessor:** `01-encoder.md`

## Goal

Turn the output-only encoder into a codec: add `vn_decode()`, so value notation
can be read back. The point is not feature parity with any other tool but a
correctness property that needs no external reference:

> **DER → VN → DER must be byte-identical, for all 112 ProfileElement values in
> the four GSMA test profiles.**

That replaces comparison against foreign reference files, which proved
unreliable: the references ship with SAIP 2.3 while the schema compiled is
3.4.1, and the resulting differences are not attributable to encoder faults.

## Decisions

| Question | Decision |
| --- | --- |
| Restartability | Full, mirroring asn1c's contract (`RC_WMORE`) |
| Named numbers on input | Sidecar annotation table first, then the reader |
| Value assignments (`valueN T ::= …`) | Not the codec's business; the tool handles them |
| Success criterion | Byte-identical DER round trip |
| Parser core | Mirror asn1c's XER design: chunk-aware tokeniser, resume state in `_asn_ctx` |

### Restartability, precisely

asn1c is only *partly* restartable, and says so in `NativeInteger.c`:

```c
/* Cannot restart from the middle; there is no place to save state
 * in the native type. Request a continuation from the very beginning. */
rval.consumed = 0;
```

The contract we mirror:

- **Constructed types** resume at the start of their own value.

  > **Corrected after implementation.** The design said they would resume
  > byte-exactly via `_asn_ctx.phase/step`. What was built is simpler: every level
  > reports the position at which *its* value began, so the value is re-parsed
  > from there. It is correct and passes the incremental test, and `_asn_ctx.ptr`
  > is still used to own an in-progress list element, but `phase`/`step` are not.
  > The cost is that a stream fed in small chunks re-parses the open value on each
  > presentation, which is quadratic in the worst case. Files, the actual use case,
  > never hit it.
- **Native primitives** (`long`, `double`; no `_asn_ctx`) return `consumed = 0` on
  `RC_WMORE`; the caller re-presents the value from its start.
- **Buffer-backed primitives** have an `_asn_ctx` and could resume; v1 treats them
  like natives, which is simpler and still correct. Upgradable without an API
  break.
- Callers must set `VN_RF_EOF` on the last presentation.

  > **Added after implementation.** A bare token at the end of a buffer is
  > ambiguous: `TRUE` may be complete or the start of a longer identifier. Without
  > an explicit end-of-input signal the reader asks for more for ever, which is
  > how the first CHOICE round trip failed.

- A single token must be complete within one presentation. A 10 KB hstring needs
  a 10 KB buffer. asn1c's XER decoder has the same property.
- `*struct_ptr` belongs to the caller after `RC_WMORE` **and** after `RC_FAIL`,
  released with `ASN_STRUCT_FREE`.

### Memory ownership

One invariant, from which everything else follows:

> Never hold an allocation only in a local variable across a return point.

Everything allocated must be reachable from the root so `ASN_STRUCT_FREE` collects
it. asn1c's own pattern, verified in `constr_SET_OF.c`: an in-progress list
element lives in `ctx->ptr` until `ASN_SET_ADD` succeeds, and the type's free
function releases `ctx->ptr` if it is still set (`constr_SET_OF.c:852`).

Concretely:

- `ATF_POINTER` members: allocate and store into the parent immediately, then
  parse into place.
- List elements: parse into `ctx->ptr`, `ASN_SET_ADD` on success, clear
  `ctx->ptr`. On failure the free function collects it.
- CHOICE: set `present` before parsing the alternative, so the free function knows
  what to release.

## API

```c
typedef struct { const char *name; long value; } vn_named_value_t;

typedef struct {
    const char *type_name;              /* matches asn_TYPE_descriptor_t.name */
    const vn_named_value_t *values;
    size_t count;
    int is_bit_string;                  /* named bits rather than named numbers */
} vn_type_names_t;

typedef struct { const vn_type_names_t *types; size_t count; } vn_annotations_t;

typedef struct vn_read_options_s {
    unsigned flags;                     /* VN_RF_* */
    const vn_annotations_t *annotations;
    char *errbuf;
    size_t errlen;
} vn_read_options_t;

asn_dec_rval_t vn_decode(const asn_codec_ctx_t *opt_codec_ctx,
                         const asn_TYPE_descriptor_t *td, void **struct_ptr,
                         const vn_read_options_t *opts,
                         const void *buf, size_t size);
```

Read options are separate from `vn_options_t` because `mode`, `indent_width` and
`line_width` mean nothing on input. Both structures carry an `annotations`
pointer: one duplicated field, no mixed semantics. `annotations == NULL` keeps
today's behaviour — numeric forms only.

`vn_decode` omits the `opt_mname` parameter that `xer_type_decoder_f` carries,
because value notation has no element wrapper around a value.

## Sidecar

asn1c does not retain INTEGER named numbers or BIT STRING named bits in the
runtime descriptors, and must not: `asn1c_C.c` notes that a value2enum map on an
INTEGER is *prohibited for XER* by X.693 §8.3.4, and `INTEGER__dump` would indeed
emit `<pukAppl1/>` instead of `1`. Patching asn1c here would be wrong.

The names do survive as C enums in the generated headers, which are real
declarations rather than comments and therefore trustworthy:

```c
typedef enum PINKeyReferenceValue { PINKeyReferenceValue_pinAppl1 = 1, … };
typedef long PINKeyReferenceValue_t;      /* named numbers */
typedef BIT_STRING_t Flags_t;             /* named bits */
```

`tools/vn-annotate.c` scans a generated directory's headers and emits a C table.
The trailing typedef gives the base type, which is what separates named bits from
named numbers. ENUMERATED types produce the same header shape as an INTEGER with
named numbers, so they cannot be told apart there — this does not matter: the
encoder consults the annotations only where the runtime map is absent.

Header **comments** are not a usable source. asn1c does write the default value
into one (`OCTET_STRING_t *lcsi /* DEFAULT '05'HH */`), but a long or oddly spaced
literal makes its comment emitter overrun and corrupt the comments of neighbouring
members, silently.

## Per-type input semantics

Mirrors the encoder. Accepted forms:

| Type | Input |
| --- | --- |
| BOOLEAN | `TRUE`, `FALSE` |
| NULL | `NULL` |
| INTEGER | signed decimal; an identifier when annotations supply one |
| ENUMERATED | identifier via the runtime map; a number only under `VN_RF_LENIENT` |
| OCTET STRING | `'..'H` and `'..'B` |
| BIT STRING | `'..'B`, `'..'H`, and `{ name, name }` when annotations supply bits |
| OBJECT IDENTIFIER, RELATIVE-OID | `{ 2 23 143 1 }` |
| SEQUENCE, SET | `{ field value, … }` |
| SEQUENCE OF, SET OF | `{ value, … }` |
| CHOICE, OPEN TYPE | `alternative : value` |
| restricted strings | cstring, `""` folded to `"`; BMP/Universal transcoded from UTF-8 |
| GeneralizedTime, UTCTime | cstring, stored as raw bytes |
| bare ANY | `'..'H`, matching the encoder's documented deviation |
| REAL | rejected |

Strictness, decided rather than configurable:

- SEQUENCE components must appear in declaration order, as X.680 requires.
- An unknown field name, a duplicate field, or a missing mandatory member is an
  error.
- An absent DEFAULT member is materialised through `default_value_set` when asn1c
  retained it. This keeps the round trip byte-identical: DER omits components
  equal to their default via `default_value_cmp`, so a value that arrives
  explicitly and one that is defaulted both re-encode identically.
- Comments and whitespace are skipped wherever X.680 permits them.
- Content after the value is the caller's business; `consumed` reports where the
  value ended.

## Testing

1. **Sidecar unit tests** — generator output for a fixture header, and encoder
   output with and without annotations.
2. **Round trip on real data** — DER → VN → DER over all four GSMA profiles, 112
   values, byte-compared. This is the acceptance criterion.
3. **Restartability** — feed the same input one byte at a time and require the
   same result as a single presentation. This is the only way the `RC_WMORE`
   paths get exercised at all.
4. **Fuzzing the input** — the encoder only ever saw trusted structures; the
   reader takes hostile text. Non-optional, under ASan and UBSan.
5. **Negative tests** — unknown field, duplicate field, wrong order, missing
   mandatory member, truncated token, unresolvable identifier.

## Non-goals

- Feature parity with commercial ASN.1 toolchains. asn1c already covers BER, DER,
  XER, OER and PER; value notation is the only gap, and this closes it in both
  directions.
- Byte-identity with reference files whose schema version is unknown.
- SEQUENCE OF defaults (`sqnInit`): asn1c retains neither the value nor a
  comparator, and the construct is rare.
- REAL.
