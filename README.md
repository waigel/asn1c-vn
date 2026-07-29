# asn1c-vn

An **ASN.1 value notation codec** for [vlm/asn1c](https://github.com/vlm/asn1c).

Converts between a decoded asn1c structure and the textual value notation defined
by **ITU-T X.680 (02/2021) = ISO/IEC 8824-1:2021**, in both directions. That is
the one transfer syntax asn1c does not ship: it handles BER, DER, XER, OER and
PER, but not value notation.

The X.68x series is freely available from the ITU, unlike most of its
recommendations: <https://www.itu.int/rec/T-REC-X.680>.

```
$ asn1vn < profile_element.der
{
    flag FALSE,
    small -65536,
    col red,
    data 'FFFF4CFEFBFFFEBB1D45'H,
    oid { 1 12 },
    leaf {
        id 128,
        tag ''H
    },
    pick leaf : {
        id 127,
        tag '0182B7'H
    }
}
```

Both directions, which is what makes the correctness claim checkable without any
reference file: **DER → value notation → DER is byte-identical** for all 112
`ProfileElement` values in the four GSMA test profiles.

## An addon, not a fork

This module depends only on asn1c's **runtime ABI**: the generated
`asn_TYPE_descriptor_t` values and the skeleton headers such as
`asn_application.h`. It does not touch `libasn1compiler`. So:

- it lives in its own repository instead of being a fork to rebase forever,
- it works with *any* asn1c output without regenerating anything,
- `vn_encode()` walks type descriptors generically, exactly as `xer_encoder.c`
  does for XER.

## ABI pinning

asn1c 0.9.29 gives every built-in type its own operation table — `asn_OP_SEQUENCE`,
`asn_OP_UTF8String`, and so on, 35 in all. The type dispatch is an exact pointer
comparison of `td->op` against those globals, which is the one
version-sensitive part of this code.

Pinned and tested against **asn1c 0.9.29**, specifically the tree
`v0.9.29-7-g8a274c3f`. An unrecognised operation table is a hard error, never a
guess, so a version mismatch surfaces as a clear failure rather than as wrong
output.

Two consequences of how asn1c packages its output are worth knowing, because they
shape the build:

- asn1c copies only the skeletons a schema actually uses. A schema without
  `BOOLEAN` yields no `BOOLEAN.c` **and no `BOOLEAN.h`**. The library is therefore
  compiled against asn1c's *installed* skeleton directory, which has the complete
  header set, and `src/vn_optabs.c` supplies a **weak definition** of every
  operation table and helper function so the link survives the missing ones. A
  real skeleton's strong definition always overrides the weak one.
- That mechanism needs GCC or clang. Weak *references* do not work for this on
  Mach-O, where they apply only to dynamic libraries; weak *definitions* do, on
  both ELF and Mach-O.

## Building

Requires GNU make, a C99 compiler with weak symbol support, and `asn1c` on
`PATH`. On macOS `-D_DARWIN_C_SOURCE` is added automatically, because
`GeneralizedTime.c` needs `struct tm` and `timegm`.

The library, compiled once and reusable across any asn1c 0.9.29 output:

```sh
make libvn.a
```

The example CLI, linked against a directory of asn1c output:

```sh
make asn1vn GEN_DIR=$HOME/git/waigel/esim-gen PDU=ProfileElement
./asn1vn < profile_element.der
```

- `GEN_DIR` — a directory holding asn1c's generated `*.c` and `*.h`
- `PDU` — the name of the root type, so `asn_DEF_<PDU>` is used

If asn1c is installed somewhere unusual, set `SKELDIR` to the directory
containing `constr_TYPE.h`.

### CLI options

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

`-r` reverses the tool: value notation on stdin, DER on stdout. It accepts bare
values and `valueN <Type> ::= …` assignments one after another, which is how
reference tooling writes them, so a file of assignments becomes the concatenated
encodings — what an SGP.22 profile package is.

Exit status is 0 on success, 1 on a decode or encode failure, 2 on a usage error.

### Reading reference value notation

Against the GSMA test profiles, `-r` parses all four of GSMA's own `.txt` files
completely and the resulting DER differs from GSMA's own `.der` by a constant 524
bytes, in exactly one construct: `sqnInit`, a `SEQUENCE OF` with a DEFAULT.

GSMA's text states that default explicitly, GSMA's DER omits it, and asn1c keeps
neither the value nor a comparator for a `SEQUENCE OF` default — so the encoder
cannot know the value equals the default and has to emit it. Every other
construct in those files round-trips byte-identically: all 26 to 30 elements per
profile, every type, the named numbers, and the defaults asn1c does retain.

That makes `SEQUENCE OF` defaults the single remaining obstacle to byte-identical
interoperability with reference value notation, rather than the rare curiosity
they looked like when counting diff lines.

The tool decodes **every** value in the input, not just the first. A single DER
value is the common case, but some formats concatenate them — an SGP.22 profile
package is a sequence of `ProfileElement` TLVs one after another — and stopping
after one would silently ignore almost the whole file. With more than one value
the output is a sequence of X.680 values rather than a single value; annotated
mode labels each with its byte offset. Input left undecoded is always an error,
never ignored.

### A note on generated headers shadowing system ones

The CLI passes `GEN_DIR` with `-idirafter`, not `-I`. A schema may define an ASN.1
type whose generated header collides with a system header: the PKIX modules used
by eSIM profiles define `Time`, producing `Time.h`, which on a case-insensitive
filesystem captures the `#include <time.h>` inside asn1c's own
`GeneralizedTime.c` and leaves `struct tm` incomplete. `-idirafter` searches
`GEN_DIR` after the system directories, so system headers win while the schema's
own headers are still found.

## API

```c
#include <vn_encoder.h>

typedef enum {
    VN_MODE_PRETTY = 0, /* for reading: 4-space indent, hex wrapped */
    VN_MODE_CANONICAL,  /* for diffing: fixed 2-space indent, no wrapping */
    VN_MODE_ANNOTATED   /* pretty plus X.680 comments */
} vn_mode_e;

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

`vn_options_t` adds an optional `errbuf`/`errlen` pair that receives the reason in
plain text, so "fail loudly" also means "fail comprehensibly":

```c
char reason[256];
vn_options_t o = {0};
o.mode = VN_MODE_CANONICAL;
o.errbuf = reason;
o.errlen = sizeof reason;
if(vn_fprint(stdout, &asn_DEF_MyType, value, &o) < 0)
    fprintf(stderr, "cannot render: %s\n", reason);
```

Flags: `VN_F_LENIENT` emits questionable values instead of failing;
`VN_F_ENUM_WITH_VALUE` adds the numeric enum value in a comment;
`VN_F_STRICT_ANY` turns a bare `ANY` into an error.

## Output modes

All three emit valid X.680 value notation, comments included — X.680 comments are
lexical items, so annotated output stays parseable.

**PRETTY** breaks constructed values across lines and keeps scalars on their
field's line, wrapping hex at `line_width`. **CANONICAL** is deterministic: fixed
two-space indent, no wrapping, no comments, and it ignores `indent_width` and
`line_width` so two callers cannot produce differing "canonical" text for the same
value. **ANNOTATED** is pretty plus comments naming types, marking absent optional
members and flagging bare `ANY`.

## Deviations from X.680

One case where output departs from the standard:

- **A bare `ANY` is emitted as a hex string.** asn1c compiles `ANY` to an OCTET
  STRING with subvariant `ASN_OSUBV_ANY` and keeps no type information, so the
  type of the value inside is simply unknown and no correct value notation
  exists for it. `VN_F_STRICT_ANY` makes it an error instead. **Table-constrained
  open types are unaffected**: they resolve to a real descriptor and render as
  ordinary value notation.

Cases that fail loudly rather than deviate:

- **REAL** is not supported.
- **Control characters in strings.** A cstring cannot carry them; X.680 provides a
  separate character-defs form, which is not implemented. `VN_F_LENIENT` emits
  them raw, which produces text outside the standard.
- An **unknown ENUMERATED value** under strict enumeration, since X.680 admits
  only the identifier. `VN_F_LENIENT` emits the number.

Readability limits imposed by the runtime ABI, all of which still produce valid
X.680:

- **BIT STRING named bit lists** and **INTEGER named numbers** are not emitted,
  because asn1c does not retain them *at runtime*. `Flags ::= BIT STRING
  { keyCert(0) }` compiles to a descriptor pointing at the *generic*
  `asn_SPC_BIT_STRING_specs`, and `Level ::= INTEGER { low(0) }` compiles to
  `0 /* No specifics */`. The numeric and hex forms used instead are equally
  valid: `'0110'B` is as legal as `{ keyCert, crlSign }`. ENUMERATED identifiers
  *are* retained, which matters because there the identifier is the only legal
  form.

  The names do survive as C enums in the generated headers
  (`PINKeyReferenceValue_pinAppl1 = 1`, `Flags_keyCert = 0`), so a future
  annotation table could recover them from `GEN_DIR` without an ASN.1 parser.
  Unlike the DEFAULT comments above, these are real declarations and are
  trustworthy.
- **DEFAULT values are emitted only for natively stored types.** asn1c keeps the
  default of an INTEGER, BOOLEAN or ENUMERATED member as a setter on the member
  (`default_value_set`), so an absent one is reconstructed and printed. For
  buffer-backed types — OCTET STRING, BIT STRING, the string types — it discards
  the value entirely: `lcsi [10] OCTET STRING (SIZE (1)) DEFAULT '05'H` compiles
  to `0, 0, /* No default value */`, so nothing can be recovered and the member
  stays omitted. A SEQUENCE OF default is likewise unavailable.

  asn1c does leave the value in a comment in the generated header
  (`OCTET_STRING_t *lcsi /* DEFAULT '05'HH */`), but those comments are not a
  usable source: a long or oddly spaced literal in the schema makes asn1c's
  comment emitter run past the end and corrupt the comments of neighbouring
  members, silently.
- **SET OF** elements are never reordered; the decoded order is preserved.

## Testing

```sh
make check
```

Four layers, in increasing strength:

1. **Golden files** pin the exact output of all three modes. A missing golden is
   written out but still counted as a failure — a golden no human has checked
   against X.680 would freeze whatever the encoder happened to produce and call
   it correct.
2. **A well-formedness scanner** independently checks brace balance, comma
   placement, string termination and alternative syntax on everything the encoder
   emits.
3. **An XER cross-check** is the semantic oracle. The same structure is emitted
   twice — once as value notation by this encoder, once as XER by asn1c — and the
   ordered sequences of scalar values must agree. asn1c's XER encoder shares no
   code with this one, which is what makes it independent.
4. **Property-based testing** via asn1c's `asn_random_fill`, pushing thousands of
   generated values through layer 3. `./tests/bin/t_xercheck 50000` runs a longer
   campaign.

What layer 3 does not check: the choice between equivalent lexical forms, because
XER text carries no type — `129` is an integer or two octets, and an empty element
is an empty string, an empty list, an empty octet string or a NULL. Exact forms
are pinned by layers 1 and 2 and by the per-type tests instead.

`asn_random_fill` also produces values asn1c's own XER encoder rejects, such as a
CHOICE with `present = 0`. Those are not skipped: the two encoders are required to
agree on whether a value is encodable at all.

### Comparing against another implementation

```sh
make check-reference GEN_DIR=<gen> PDU=<Type> REFDIR=<dir>
```

`REFDIR` holds `<name>.der` files next to `<name>.txt` (or `.vn`/`.val`)
references produced by a different tool. Our output is generated with `-A` so it
carries value assignments, the form reference tooling uses, making the comparison
a direct diff.

Exact agreement is not reachable while the DEFAULT and named-number gaps above
remain, so `tests/reference-baseline.txt` records the current differing-line count
per file. The target fails on any increase and reports any decrease, which turns
the remaining distance into a tracked number instead of a claim.

Measured against the GSMA test profiles shipped with
[euicc-profile-tool](https://github.com/waigel/euicc-profile-tool), every
remaining difference is attributable to those two gaps; there is no case where
this encoder emits a wrong value or loses a field that is present in the DER.

## Status

Every type family in the spec is implemented: BOOLEAN, NULL, INTEGER (including
values beyond `intmax_t`), ENUMERATED, OCTET STRING, BIT STRING, OBJECT
IDENTIFIER, RELATIVE-OID, all restricted string types, GeneralizedTime, UTCTime,
SEQUENCE, SET, SEQUENCE OF, SET OF, CHOICE, open types and bare `ANY`.

## License

BSD-2-Clause, matching asn1c. See `LICENSE`.
