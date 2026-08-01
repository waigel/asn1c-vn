# asn1c-vn

[![ci](https://github.com/waigel/asn1c-vn/actions/workflows/ci.yml/badge.svg)](https://github.com/waigel/asn1c-vn/actions/workflows/ci.yml)

An **ASN.1 value notation codec** for [vlm/asn1c](https://github.com/vlm/asn1c) —
in both directions.

Value notation is the human-readable syntax for ASN.1 *values*, defined by
**ITU-T X.680 (02/2021) = ISO/IEC 8824-1:2021**. It is the one transfer syntax
asn1c does not ship: asn1c handles BER, DER, XER, OER and PER, but not this. So a
DER file can be turned into XML, but not into something a person reads
comfortably, and text a person wrote cannot be turned back into DER.

This closes that gap without forking asn1c. It is an addon depending only on
asn1c's runtime ABI, so it works with any asn1c output without regenerating
anything.

```
$ asn1vn < profile_element.der
{
    major-version 2,
    minor-version 3,
    profileType "GSMA Generic eUICC Test Profile",
    iccid '89000123456789012341'H,
    eUICC-Mandatory-services {
        usim NULL,
        ber-tlv NULL
    },
    eUICC-Mandatory-GFSTEList {
        { 2 23 143 1 2 1 },
        { 2 23 143 1 2 3 }
    }
}

$ asn1vn -r < profile_element.vn > roundtripped.der
```

The correctness claim is checkable without any reference file: **DER → value
notation → DER is byte-identical** for all 112 `ProfileElement` values in the four
GSMA eSIM test profiles.

The X.68x series is freely available from the ITU, unlike most of its
recommendations: <https://www.itu.int/rec/T-REC-X.680>.

## Building

Needs GNU make, a C99 compiler with weak symbol support (GCC or clang), and
`asn1c` on `PATH`. On macOS `-D_DARWIN_C_SOURCE` is added automatically, because
asn1c's `GeneralizedTime.c` needs `struct tm` and `timegm`.

```sh
make check                             # the test suite
make libvn.a                           # the library
make asn1vn GEN_DIR=<dir> PDU=<Type>   # the example CLI
```

- `GEN_DIR` — a directory of asn1c output (`*.c` and `*.h`)
- `PDU` — the root type's name, so `asn_DEF_<PDU>` is used

If asn1c lives somewhere unusual, set `SKELDIR` to the directory holding
`constr_TYPE.h`. Some projects generate into a subdirectory; point `GEN_DIR` at
the directory that actually holds the `.c` files, not the project root.

## Command line

```
asn1vn [-c|-a] [-A] [-l WIDTH] [-i WIDTH] [-L] [-S] < input.der
asn1vn -r < input.vn > output.der

  -c        canonical output: deterministic, for diffing
  -a        annotated output: adds X.680 comments
  -A        emit `valueN <Type> ::= <value>` assignments, the form reference
            tooling uses, so its output can be diffed directly
  -l WIDTH  wrap hex at WIDTH columns; 0 disables wrapping
  -i WIDTH  indent width, default 4
  -L        lenient: emit questionable values instead of failing
  -S        strict: fail on a bare ANY rather than emitting hex
  -r        reverse: value notation in, DER out
```

Exit status is 0 on success, 1 on a decode or encode failure, 2 on a usage error.

Both directions process **every** value in the input, not just the first. A single
DER value is the common case, but some formats concatenate them — an SGP.22
profile package is a sequence of `ProfileElement` TLVs one after another — and
stopping after one would silently ignore almost the whole file. Input left
undecoded is always an error, never ignored.

## API

```c
#include <vn_encoder.h>
```

### Writing

```c
asn_enc_rval_t vn_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
                         const vn_options_t *opts,
                         asn_app_consume_bytes_f *cb, void *key);

int vn_fprint(FILE *stream, const asn_TYPE_descriptor_t *td, const void *sptr,
              const vn_options_t *opts);
```

Naming follows the asn1c family — `xer_encode`, `der_encode`, `oer_encode` — and
`asn_enc_rval_t` keeps asn1c's encoder contract: `encoded` is the byte count or
`-1`, with `failed_type` and `structure_ptr` marking the failure site. Passing
`opts` as `NULL` means pretty defaults.

`vn_options_t` adds an `errbuf`/`errlen` pair that receives the reason in plain
text, so "fail loudly" also means "fail comprehensibly":

```c
char reason[256];
vn_options_t o = {0};
o.mode = VN_MODE_CANONICAL;
o.errbuf = reason;
o.errlen = sizeof reason;
if(vn_fprint(stdout, &asn_DEF_MyType, value, &o) < 0)
    fprintf(stderr, "cannot render: %s\n", reason);
```

Flags: `VN_F_LENIENT` emits questionable values instead of failing,
`VN_F_ENUM_WITH_VALUE` adds the numeric enum value in a comment, `VN_F_STRICT_ANY`
turns a bare `ANY` into an error.

### Reading

```c
asn_dec_rval_t vn_decode(const asn_codec_ctx_t *opt_codec_ctx,
                         const asn_TYPE_descriptor_t *td, void **struct_ptr,
                         const vn_read_options_t *opts,
                         const void *buf, size_t size);
```

Shaped like asn1c's `xer_type_decoder_f` minus its `opt_mname`, because value
notation has no element wrapper around a value. Exactly one value is consumed;
`rval.consumed` says where it ended, and anything after it is the caller's
business. Value assignments (`valueN <Type> ::= …`) are module syntax rather than
value syntax, so the codec knows nothing about them — `asn1vn -r` handles those.

```c
char reason[256];
void *st = 0;
vn_read_options_t ro = {0};
ro.flags = VN_RF_EOF;        /* the buffer holds all the input there will be */
ro.errbuf = reason;
ro.errlen = sizeof reason;

asn_dec_rval_t rv = vn_decode(0, &asn_DEF_MyType, &st, &ro, text, len);
if(rv.code != RC_OK)
    fprintf(stderr, "cannot read: %s\n", reason);  /* carries line and column */
ASN_STRUCT_FREE(asn_DEF_MyType, st);               /* correct after a failure too */
```

**`VN_RF_EOF` matters.** A bare token at the end of a buffer is ambiguous: `TRUE`
may be complete, or the start of a longer identifier arriving in the next chunk.
Callers holding the whole text — the normal case — set this flag; one feeding a
stream sets it only on the final presentation, or the reader asks for more for
ever.

`*struct_ptr` belongs to the caller after `RC_WMORE` and after `RC_FAIL` alike;
release it with `ASN_STRUCT_FREE`.

Restartability follows asn1c's own contract, which is only partial: a value is
re-presented from its start rather than resumed mid-token, and a single token must
be complete within one presentation, so a 10 KB hstring needs a 10 KB buffer.
asn1c's XER decoder behaves the same way.

### Identifiers asn1c does not keep

asn1c retains ENUMERATED identifiers, but not INTEGER named numbers or BIT STRING
named bit lists — and for INTEGER it *must not*: X.693 §8.3.4 prohibits the
identifier form in XER, and asn1c's own `INTEGER__dump` would emit `<pukAppl1/>`
instead of `1`. Patching the compiler there would break conformance.

The names do survive as C enums in the generated headers, so `vn-annotate`
recovers them into a table:

```sh
make vn-annotate
./vn-annotate <gen-dir> > vn_annotations.c   # then link it
```

`make asn1vn` does this for you. With the table you get `keyReference pukAppl1` and
`{ key-cert, crl-sign }`; without it, `keyReference 1` and `'0110'B`, which is
equally valid X.680. The table is also what lets the reader accept the identifiers
reference tooling writes.

An inline definition — `algorithmID INTEGER { milenage(1), … }` written in place
rather than as its own type — has no descriptor of its own to look up by, since
asn1c points it at the shared `asn_DEF_NativeInteger`. It is keyed instead by the
path that reaches it, `AlgoParameter__algorithmID`, which is what asn1c names the
enum. The path accumulates through anonymous types, so a nested one is
`Outer__inner__x`, or `Outer__ring__Member__y` for a list element.

Which representation asn1c picked does not matter: a plain `long`, the
`unsigned long` it uses for `(0..MAX)`, and the buffer-backed `INTEGER_t` it
falls back to for a range it will not hold natively all consult the table. They
have to, or the reader would accept an identifier the writer never emits.

Set it through `vn_options_t.annotations` / `vn_read_options_t.annotations`, or
link the generated file and let the weak default pick it up.

## Output modes

All three emit valid X.680, comments included — X.680 comments are lexical items,
so annotated output stays parseable.

- **PRETTY** breaks constructed values across lines, keeps scalars on their
  field's line, wraps hex at `line_width`.
- **CANONICAL** is deterministic: fixed two-space indent, no wrapping, no
  comments. It ignores `indent_width` and `line_width`, so two callers cannot
  produce differing "canonical" text for the same value.
- **ANNOTATED** is pretty plus comments naming types, marking absent optional
  members and flagging bare `ANY`.

## Deviations from X.680

One case where output departs from the standard:

- **A bare `ANY` is emitted as a hex string.** asn1c compiles `ANY` to an OCTET
  STRING with subvariant `ASN_OSUBV_ANY` and keeps no type information, so the type
  inside is unknown and no correct value notation exists for it.
  `VN_F_STRICT_ANY` makes it an error instead. **Table-constrained open types are
  unaffected** — they resolve to a real descriptor and render normally.

On input the reader is the more forgiving side, as §11.8 asks: a NON-BREAKING
HYPHEN (U+2011) in any identifier — a member name, an alternative, an enumerator,
a named number, a named bit — is the same name as one written with an ordinary
hyphen. Output always uses the ordinary hyphen.

Cases that fail loudly rather than deviate:

- **REAL** is not supported.
- **Control characters in strings.** A cstring cannot carry them; X.680 provides a
  separate character-defs form, not implemented here. `VN_F_LENIENT` emits them
  raw, which produces text outside the standard.
- An **unknown ENUMERATED value** under strict enumeration, since X.680 admits only
  the identifier. `VN_F_LENIENT` emits the number.

## Known limits

These are properties of asn1c's runtime ABI rather than choices:

- **DEFAULT values survive only for natively stored types.** asn1c keeps the
  default of an INTEGER, BOOLEAN or ENUMERATED member as a setter on the member, so
  an absent one is reconstructed and printed. For OCTET STRING, BIT STRING and the
  string types it discards the value: `lcsi [10] OCTET STRING (SIZE (1)) DEFAULT
  '05'H` compiles to `0, 0, /* No default value */`.

  `contrib/asn1c-A-octet-bit-string-defaults.patch` fixes that in asn1c. It is a
  bug fix rather than a feature: asn1c parses the value — `asn1c -E` prints it back
  — and the runtime hook already exists, so only the emission was missing.
  Everything here works without it.

- **A `SEQUENCE OF` DEFAULT is unavailable even with that patch**, because asn1c
  retains neither the value nor a comparator. This is the single remaining obstacle
  to byte-identical interoperability with reference value notation; see below.

- **BIT STRING named bits and INTEGER named numbers** need the annotation table.
  So does X.680 §22.7: trailing zero bits are insignificant only when the type has
  a named bit list, and the table is the only place that fact survives.

- **Subtype constraints are not checked.** A `SIZE`, a value range or a permitted
  alphabet is enforced by neither direction: the reader takes a ten-octet `iccid`
  written as four, and the writer emits whatever the structure holds. What *is*
  enforced is the shape the schema gives a value — a mandatory member may not be
  missing, a member may not repeat, an unknown one is an error.

  asn1c compiles the constraints and `asn_check_constraints()` runs them, so
  calling it is the natural way to add this. Note that it under-reports until
  asn1c is patched: `SEQUENCE_constraint` stops at the first member that has no
  constraint of its own, which for the SAIP header is `major-version` — so
  `iccid`'s `SIZE (10)` is never reached. `contrib/asn1c-B-constraint-loop.patch`
  fixes that.

  Constraints are also only part of what makes a profile valid. Rules such as
  "exactly one header, and it comes first" are prose in the profile specification,
  outside anything ASN.1 can state.

- **An unset DEFAULT member cannot be told from an absent OPTIONAL one** where
  asn1c dropped the value, so it is omitted.

- **`SET OF` elements are never reordered**; the decoded order is preserved.

## Testing

```sh
make check          # 19 test binaries
```

Layered, in increasing strength:

1. **Golden files** pin the exact output of all three modes. A missing golden is
   written out but still counted as a failure — one no human has checked against
   X.680 would freeze whatever the encoder happened to produce and call it correct.
2. **A well-formedness scanner** independently checks brace balance, comma
   placement, string termination and alternative syntax on everything emitted.
3. **An XER cross-check** is the semantic oracle: the same structure is emitted as
   value notation by this code and as XER by asn1c, and the ordered scalar
   sequences must agree. asn1c's XER encoder shares no code with this one.
4. **Round trip**, the acceptance criterion: DER → value notation → DER,
   byte-compared.
5. **The standard's own examples.** X.680 Annex G is transcribed into
   `tests/schemas/annexg.asn1` and `tests/t_annexg.c`, each case labelled with the
   subclause it came from. Where the annex asserts that two spellings denote one
   value — `{sunday, monday, wednesday}` and `'1101000'B` under §22.7 — or that
   two denote different ones — `'1101'B` and `'1101000'B` without a named bit
   list, per the note to G.2.5.1 — the test asserts the same. Nothing here is our
   reading of the standard; it is the standard's own worked material.
6. **Fuzzing** the reader, the only part that takes input it did not produce.

Against real data:

```sh
make check-roundtrip GEN_DIR=<gen> PDU=<Type> DERDIR=<dir with *.der>
make check-xer       GEN_DIR=<gen> PDU=<Type> [DERDIR=<dir> | ROUNDS=20000]
make check-reference GEN_DIR=<gen> PDU=<Type> REFDIR=<dir with *.der and *.txt>
make fuzz-read && ./fuzz-read -max_total_time=60
```

`check-xer` accepts `DERDIR` because `asn_random_fill` cannot be used on every
schema: `constr_SET_OF.c:1329` calls `random_fill` without a NULL check and neither
`ANY` nor `OPEN_TYPE` provides one, so a `SEQUENCE OF ANY` segfaults inside asn1c.
Real encodings also carry realistic values.

Apple's clang ships without libFuzzer, so `fuzz-read` looks for a real clang
(Homebrew's `llvm` has one) and says so if it finds none. Regression seeds from past
findings live in `tests/fuzz-corpus/`.

### Reading another tool's output

`check-reference` diffs our output against reference value notation from a
different implementation, with `tests/reference-baseline.txt` guarding against
regression.

Measured against the GSMA eSIM test profiles: `-r` parses all four of GSMA's own
`.txt` files completely, and the resulting DER differs from GSMA's own `.der` by a
constant 524 bytes in exactly one construct — `sqnInit`, a `SEQUENCE OF` with a
DEFAULT. GSMA's text states that default, GSMA's DER omits it, and asn1c keeps
neither the value nor a comparator for it, so the encoder cannot know it equals the
default. Every other construct matches byte for byte: 26 to 30 elements per
profile, every type, the named numbers, and the defaults asn1c does retain.

Comparing *rendered* output against such a reference is less conclusive than it
looks, incidentally: those files were produced from SAIP 2.3 while the schema
compiled here is 3.4.1, so some differences are version skew rather than faults.
That is why the round trip, which needs no reference at all, is the acceptance
criterion.

## ABI pinning

asn1c 0.9.29 gives every built-in type its own operation table — `asn_OP_SEQUENCE`,
`asn_OP_UTF8String`, 35 in all. Type dispatch is an exact pointer comparison of
`td->op` against those globals, which is the one version-sensitive part of this
code. An unrecognised table is a hard error, never a guess, so a version mismatch
surfaces as a clear failure rather than wrong output.

Pinned and tested against **asn1c 0.9.29**, specifically `v0.9.29-7-g8a274c3f`.

Two consequences of how asn1c packages its output shape the build:

- **asn1c copies only the skeletons a schema uses.** A schema without `BOOLEAN`
  yields no `BOOLEAN.c` *and no `BOOLEAN.h`*. The library is therefore compiled
  against asn1c's installed skeleton directory, which has the complete header set,
  and `src/vn_optabs.c` supplies a **weak definition** of every operation table and
  helper function so the link survives the missing ones. A real skeleton's strong
  definition always overrides the weak one.

  This needs GCC or clang. Weak *references* do not work here on Mach-O, where they
  apply only to dynamic libraries; weak *definitions* do, on both ELF and Mach-O.

- **A generated header can shadow a system one.** The PKIX modules used by eSIM
  profiles define an ASN.1 type `Time`, producing `Time.h`, which on a
  case-insensitive filesystem captures the `#include <time.h>` inside asn1c's own
  `GeneralizedTime.c` and leaves `struct tm` incomplete. `GEN_DIR` is therefore
  passed with `-idirafter`, never `-I`, so system headers win while the schema's own
  headers are still found.

## Integrating into an asn1c converter tool

`contrib/` holds an 81-line patch adding `-ovn`, `-ovnc` and `-ovna` to asn1c's
`converter-example.c`, so a tool built from it gains value notation alongside its
other output formats — and, being built with `ASN_PDU_COLLECTION`, gets it for any
type via `-p`. See `contrib/README.md`.

## Design notes

`docs/design/` records why things are the way they are, including the findings that
changed the design partway through. Corrections are marked in place rather than
edited away, since the reason a decision was reversed is usually more useful than
the decision.

## Contributing

See `CONTRIBUTING.md`. The short version: `make check` must pass under both GCC and
clang, new behaviour needs a test that fails without it, and changes to the reader
get fuzzed.

## License

BSD-2-Clause, matching asn1c. See `LICENSE`.
