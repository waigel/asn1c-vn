# asn1c-vn

[![ci](https://github.com/waigel/asn1c-vn/actions/workflows/ci.yml/badge.svg)](https://github.com/waigel/asn1c-vn/actions/workflows/ci.yml)

An **ASN.1 value notation codec** for [vlm/asn1c](https://github.com/vlm/asn1c),
in both directions.

Value notation is the syntax for ASN.1 *values* that a person can read. ITU-T
X.680 (02/2021), which is ISO/IEC 8824-1:2021, defines it. It is the one transfer
syntax that asn1c does not ship. asn1c handles BER, DER, XER, OER and PER. None
of those turns a DER file into text that reads well. None of them turns text
that a person wrote back into DER.

asn1c-vn closes that gap and does not fork asn1c. It depends on the runtime ABI
of asn1c alone, so it works with any asn1c output and regenerates nothing.

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

You can check the correctness claim without a reference file. **DER to value
notation and back to DER gives the same bytes.** This holds for every one of the
112 `ProfileElement` values in the four GSMA eSIM test profiles.

The ITU publishes the X.68x series without charge, unlike most of its
recommendations: <https://www.itu.int/rec/T-REC-X.680>.

## Build

You need GNU make and a C99 compiler with weak symbol support. GCC and clang
both work. You also need `asn1c` on `PATH`. On macOS the build adds
`-D_DARWIN_C_SOURCE`, because `GeneralizedTime.c` in asn1c needs `struct tm` and
`timegm`.

```sh
make check                             # the test suite
make libvn.a                           # the library
make asn1vn GEN_DIR=<dir> PDU=<Type>   # the example command
```

- `GEN_DIR` is a directory of asn1c output, with the `*.c` and `*.h` files.
- `PDU` is the name of the root type, so that `asn_DEF_<PDU>` is used.

If asn1c is in an unusual place, set `SKELDIR` to the directory that holds
`constr_TYPE.h`. Some projects generate into a subdirectory. Point `GEN_DIR` at
the directory that holds the `.c` files, and not at the root of the project.

## Command line

```
asn1vn [-c|-a] [-A] [-l WIDTH] [-i WIDTH] [-L] [-S] [-C] < input.der
asn1vn -r [-C] < input.vn > output.der

  -c        canonical output: deterministic, for a diff
  -a        annotated output: adds X.680 comments
  -A        write `valueN <Type> ::= <value>` assignments, the form that
            reference tools use, so that a diff against them works
  -l WIDTH  wrap hex at WIDTH columns. 0 turns wrapping off
  -i WIDTH  indent width, 4 by default
  -L        lenient: write a questionable value instead of a failure
  -S        strict: fail on a bare ANY instead of writing hex
  -C        check the subtype constraints of the schema. See "Known limits"
  -r        reverse: read value notation, write DER
```

The exit status is 0 after success, 1 after a read or write failure, and 2 after
a usage error.

Both directions process **every** value in the input, and not the first one
alone. One DER value is the common case. Some formats concatenate values. An
SGP.22 profile package is a sequence of `ProfileElement` values, one after the
other. A tool that stops after the first value ignores almost the whole file.
Input that stays undecoded is an error, never a silence.

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

The names follow the asn1c family: `xer_encode`, `der_encode`, `oer_encode`.
`asn_enc_rval_t` keeps the encoder contract of asn1c. The field `encoded` holds
the byte count or `-1`, and `failed_type` with `structure_ptr` marks the place of
the failure. If `opts` is `NULL`, the defaults give pretty output.

`vn_options_t` adds an `errbuf` and `errlen` pair. It receives the reason as
plain text, so that a loud failure is also a clear one:

```c
char reason[256];
vn_options_t o = {0};
o.mode = VN_MODE_CANONICAL;
o.errbuf = reason;
o.errlen = sizeof reason;
if(vn_fprint(stdout, &asn_DEF_MyType, value, &o) < 0)
    fprintf(stderr, "cannot render: %s\n", reason);
```

Three flags change the output. `VN_F_LENIENT` writes a questionable value
instead of a failure. `VN_F_ENUM_WITH_VALUE` adds the numeric value of an
enumerator in a comment. `VN_F_STRICT_ANY` turns a bare `ANY` into an error.

### Reading

```c
asn_dec_rval_t vn_decode(const asn_codec_ctx_t *opt_codec_ctx,
                         const asn_TYPE_descriptor_t *td, void **struct_ptr,
                         const vn_read_options_t *opts,
                         const void *buf, size_t size);
```

The shape follows `xer_type_decoder_f` of asn1c, without its `opt_mname`: value
notation puts no element wrapper around a value. The reader consumes exactly one
value. The field `rval.consumed` says where that value ended, and the rest of
the buffer belongs to the caller. A value assignment (`valueN <Type> ::= …`) is
module syntax and not value syntax, so the codec knows nothing about it.
`asn1vn -r` handles that form.

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

**`VN_RF_EOF` matters.** A bare token at the end of a buffer is ambiguous.
`TRUE` can be complete, or it can be the start of a longer identifier that
arrives in the next chunk. A caller that holds the whole text sets this flag,
and that is the normal case. A caller that feeds a stream sets it on the last
presentation alone, or the reader asks for more input for ever.

The caller owns `*struct_ptr` after `RC_WMORE` and after `RC_FAIL`. Release it
with `ASN_STRUCT_FREE`.

Restart follows the contract of asn1c, and that contract is partial. The reader
presents a value again from its start and does not resume inside a token. One
token must be complete within one presentation, so a hstring of 10 KB needs a
buffer of 10 KB. The XER decoder of asn1c behaves the same way.

### The identifiers that asn1c does not keep

asn1c keeps the identifiers of an ENUMERATED. It does not keep the named numbers
of an INTEGER, and it does not keep the named bit list of a BIT STRING. For the
INTEGER it *must not*: X.693 §8.3.4 forbids the identifier form in XER, and
`INTEGER__dump` of asn1c would write `<pukAppl1/>` where the standard demands
`1`. A patch to the compiler there breaks conformance.

The names survive as C enums in the generated headers. `vn-annotate` recovers
them into a table:

```sh
make vn-annotate
./vn-annotate <gen-dir> > vn_annotations.c   # then link it
```

`make asn1vn` does this for you. With the table you get `keyReference pukAppl1`
and `{ key-cert, crl-sign }`. Without it you get `keyReference 1` and `'0110'B`,
which is equally valid X.680. The table is also what lets the reader accept the
identifiers that reference tools write.

An inline definition has no descriptor to look up. `algorithmID INTEGER {
milenage(1), … }`, written in place instead of as its own type, points at the
shared `asn_DEF_NativeInteger`. The table keys it by the path that reaches it,
`AlgoParameter__algorithmID`, which is the name that asn1c gives the enum. The
path accumulates through anonymous types: a nested one is `Outer__inner__x`, and
a list element is `Outer__ring__Member__y`.

The representation that asn1c picked does not matter. Three representations
exist: a plain `long`, the `unsigned long` for `(0..MAX)`, and the
buffer-backed `INTEGER_t` for a wide range. All three consult the table. They
must, or the reader accepts an identifier that the writer never writes.

Set the table through `vn_options_t.annotations` or
`vn_read_options_t.annotations`. You can also link the generated file and let
the weak default find it.

## Output modes

All three modes write valid X.680, comments included. An X.680 comment is a
lexical item, so annotated output stays readable by a parser.

- **PRETTY** breaks a constructed value across lines. It keeps a scalar on the
  line of its field and wraps hex at `line_width`.
- **CANONICAL** is deterministic. The indent is two spaces, there is no
  wrapping, and there are no comments. It ignores `indent_width` and
  `line_width`, so two callers cannot write different "canonical" text for one
  value.
- **ANNOTATED** is pretty output plus comments. The comments name types, mark an
  absent optional member, and flag a bare `ANY`.

## Where the output departs from X.680

One case departs from the standard:

- **A bare `ANY` becomes a hex string.** asn1c compiles `ANY` into an OCTET
  STRING with the subvariant `ASN_OSUBV_ANY` and keeps no type information. The
  type inside is therefore unknown, and no correct value notation for it exists.
  `VN_F_STRICT_ANY` makes it an error instead. **A table-constrained open type is
  not affected.** It resolves to a real descriptor and renders normally.

On input the reader is the more tolerant side, as §11.8 asks. A NON-BREAKING
HYPHEN (U+2011) in an identifier is the same name as an ordinary hyphen. This
holds for a member name, an alternative, an enumerator, a named number and a
named bit. The output always uses the ordinary hyphen.

Three cases fail loudly instead of departing:

- **REAL** is not supported.
- **A control character in a string.** A cstring cannot carry one, and the
  separate character-defs form of X.680 is not implemented here. `VN_F_LENIENT`
  writes the character raw, and that text is outside the standard.
- **An unknown ENUMERATED value** under strict enumeration. X.680 admits the
  identifier alone. `VN_F_LENIENT` writes the number.

## Known limits

These follow from the runtime ABI of asn1c and are not choices:

- **A DEFAULT value survives for a natively stored type alone.** asn1c keeps
  the default of an INTEGER, a BOOLEAN or an ENUMERATED member as a setter on
  the member. An absent member is therefore reconstructed and written. For
  OCTET STRING, BIT STRING and the string types asn1c discards the value:
  `lcsi [10] OCTET STRING (SIZE (1)) DEFAULT '05'H` compiles to
  `0, 0, /* No default value */`.

  `contrib/asn1c-A-octet-bit-string-defaults.patch` corrects that in asn1c. It
  is a bug fix and not a feature: asn1c parses the value, `asn1c -E` prints it
  back, and the runtime hook exists already. The emission alone was missing.
  Everything here works without the patch.

- **A `SEQUENCE OF` DEFAULT stays unavailable even with that patch.** asn1c
  keeps neither the value nor a comparator for it. This is the one remaining
  obstacle to byte-identical output against reference value notation. See
  "Reading the output of another tool".

- **BIT STRING named bits and INTEGER named numbers need the annotation table.**
  X.680 §22.7 needs it too. A trailing zero bit is insignificant only when the
  type has a named bit list. The table is the one place where that fact
  survives.

- **A subtype constraint is checked on request alone.** Neither direction
  enforces a `SIZE`, a range or a permitted alphabet by itself. The reader
  therefore accepts a ten-octet `iccid` written as four. Pass `-C`, or call
  `vn_check_constraints()`, and the error is caught and located. What both
  directions do enforce is the shape that the schema gives a value. A mandatory
  member cannot be missing, a member cannot repeat, and an unknown member is an
  error.

  ```
  $ asn1vn -r -C < edited.vn > profile.der
  asn1vn: value 1 violates the schema: header.iccid (OCTET STRING): constraint failed
  ```

  The walk is ours and not `asn_check_constraints()`, which under-reports.
  `SEQUENCE_constraint` returns at the first member that carries no constraint
  of its own. For the SAIP header that member is `major-version`, four members
  ahead of `iccid`. `contrib/asn1c-B-constraint-loop.patch` corrects asn1c.
  `vn_check_constraints()` does not need the patch.

  A constraint is one part of what makes a profile valid. A rule such as
  "exactly one header, and it comes first" is prose in the profile
  specification. ASN.1 cannot state it, and nothing here checks it.

- **An unset DEFAULT member and an absent OPTIONAL member look alike** where
  asn1c dropped the value, so the writer omits the member.

- **`SET OF` elements keep their order.** The writer never reorders them.

## Tests

```sh
make check          # 20 test binaries
```

Seven layers, in increasing strength:

1. **Golden files** pin the exact output of all three modes. A missing golden
   file is written out and still counts as a failure. A golden file that no
   person checked against X.680 freezes whatever the encoder produced and calls
   it correct.
2. **A scanner for well-formedness** checks brace balance, comma placement,
   string termination and alternative syntax on everything written. It shares no
   code with the encoder.
3. **A cross-check against XER** is the semantic oracle. The same structure goes
   out as value notation from this code and as XER from asn1c, and the ordered
   sequences of scalars must agree. The XER encoder of asn1c shares no code with
   this one.
4. **The round trip** is the acceptance criterion: DER to value notation to DER,
   compared byte for byte.
5. **Value notation that somebody else wrote.** Every other layer feeds the
   reader text that this codec produced. Agreement then shows one thing alone:
   that the two halves share their assumptions. `check-vn-corpus` reads a
   directory of
   foreign value notation and demands two things of each file. The file must
   parse. The DER from their text and the DER from our rendering of it must be
   identical. TCA publishes its reference ProfileElements in this form, and 395
   of the 404 in the 3.4 set pass. Of the nine that fail, two use members that
   the 3.4.1 schema does not have, and seven are malformed. Six lack a comma
   between components (§25.18, §26.3) and one lacks a colon before a CHOICE
   alternative. The corpus is not vendored here, because it belongs to its
   publisher. Point the target at it.
6. **The examples of the standard.** Annex G of X.680 is transcribed into
   `tests/schemas/annexg.asn1` and `tests/t_annexg.c`. Each case carries the
   subclause it came from. Where the annex states that two spellings denote one
   value, `{sunday, monday, wednesday}` and `'1101000'B` under §22.7, the test
   states the same. The annex also states that two spellings denote different
   values: `'1101'B` and `'1101000'B` without a named bit list, under the note
   to G.2.5.1. The test states that too. Nothing here is our reading of the standard. It is the
   worked material of the standard.
7. **Fuzzing** the reader, which is the one part that takes input it did not
   produce.

Against real data:

```sh
make check-roundtrip GEN_DIR=<gen> PDU=<Type> DERDIR=<dir with *.der>
make check-vn-corpus GEN_DIR=<gen> PDU=<Type> VNDIR=<dir with *.asn1>
make check-xer       GEN_DIR=<gen> PDU=<Type> [DERDIR=<dir> | ROUNDS=20000]
make check-reference GEN_DIR=<gen> PDU=<Type> REFDIR=<dir with *.der and *.txt>
make fuzz-read && ./fuzz-read -max_total_time=60
```

`check-xer` accepts `DERDIR` because `asn_random_fill` does not work on every
schema. `constr_SET_OF.c:1329` calls `random_fill` without a NULL check, and
neither `ANY` nor `OPEN_TYPE` provides one, so a `SEQUENCE OF ANY` crashes inside
asn1c. A real encoding also carries realistic values.

The clang of Apple ships without libFuzzer. `fuzz-read` looks for a real clang,
which the `llvm` package of Homebrew provides. If it finds none, it says so.
Regression seeds from past findings are in `tests/fuzz-corpus/`.

### Reading the output of another tool

`check-reference` compares our output against reference value notation from
another implementation. `tests/reference-baseline.txt` guards against a
regression.

Measured against the GSMA eSIM test profiles, `-r` parses all four `.txt` files
of GSMA completely. The DER that comes out differs from the DER of GSMA by a
constant 524 bytes, in one construct: `sqnInit`, a `SEQUENCE OF` with a DEFAULT.
The text of GSMA states that default and the DER of GSMA omits it. asn1c keeps
neither the value nor a comparator, so the encoder cannot know that the value
equals the default. Every other construct matches byte for byte: 26 to 30
elements per profile, every type, the named numbers, and the defaults that asn1c
does keep.

A comparison of *rendered* output against such a reference proves less than it
looks. Those files came from SAIP 2.3, and the schema compiled here is 3.4.1, so
some differences are version skew and not faults. The round trip needs no
reference at all, and that is why it is the acceptance criterion.

## The pinned ABI

asn1c 0.9.29 gives every built-in type its own operation table:
`asn_OP_SEQUENCE`, `asn_OP_UTF8String`, 35 tables in all. Type dispatch is an
exact pointer comparison of `td->op` against those globals, and that is the one
version-sensitive part of this code. An unrecognized table is a hard error and
never a guess, so a version mismatch appears as a clear failure and not as wrong
output.

Pinned against **asn1c 0.9.29**, and tested against `v0.9.29-7-g8a274c3f`.

Two properties of the asn1c output shape the build:

- **asn1c copies the skeletons that a schema uses, and no others.** A schema
  without `BOOLEAN` yields no `BOOLEAN.c` *and no `BOOLEAN.h`*. The library is
  therefore compiled against the installed skeleton directory of asn1c, which
  holds the complete header set. `src/vn_optabs.c` supplies a **weak
  definition** of every operation table and helper function, so that the link
  survives the missing ones. A strong definition from a real skeleton always
  wins.

  This needs GCC or clang. A weak *reference* does not work here on Mach-O,
  where it applies to dynamic libraries alone. A weak *definition* works on both
  ELF and Mach-O.

- **A generated header can hide a system header.** The PKIX modules that eSIM
  profiles use define an ASN.1 type `Time`, which produces `Time.h`. On a
  case-insensitive filesystem that header captures the `#include <time.h>`
  inside `GeneralizedTime.c` of asn1c and leaves `struct tm` incomplete.
  `GEN_DIR` is therefore passed with `-idirafter` and never with `-I`, so a
  system header wins while the headers of the schema are still found.

## Other tools in this repository

`vn-tree` writes the element tree of a schema as JSON, read from the type
descriptors of asn1c. A documentation generator needs it, because an element
name in XER is not always a field name: XER names the members of a `SEQUENCE OF`
after their type.

```sh
make vn-tree GEN_DIR=<dir> PDU=<Type>
./vn-tree > tree.json
```

[asn1-docs](https://github.com/waigel/asn1-docs) reads that file. The [eUICC
Profile Reference](https://euicc.waigel.com) is built this
way.

## Adding value notation to an asn1c converter

`contrib/` holds a patch of 81 lines. It adds `-ovn`, `-ovnc` and `-ovna` to
`converter-example.c` of asn1c. A tool built from that file then has value
notation beside its other output formats. If the tool is built with
`ASN_PDU_COLLECTION`, it gets value notation for any type through `-p`. Read
`contrib/README.md`.

## Design notes

`docs/design/` records why things are the way they are. It includes the findings
that changed the design partway through. A correction is marked in place and not
edited away, because the reason for a reversal is usually more useful than the
decision.

## Contributing

Read `CONTRIBUTING.md`. In short: `make check` must pass under GCC and under
clang, new behavior needs a test that fails without it, and a change to the
reader gets fuzzed.

## Licence

BSD-2-Clause, the same as asn1c. See `LICENSE`.
