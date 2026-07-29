# Contributing

## Getting a build

```sh
make check
```

That needs `asn1c` on `PATH` and nothing else. The suite generates its own code
from `tests/schemas/*.asn1`, so there is no fixture to keep in sync by hand.

To work against a real schema:

```sh
make asn1vn GEN_DIR=<asn1c output dir> PDU=<root type>
```

## What a change needs

**A test that fails without it.** Not "a test exists" — one that actually catches
the thing. Every bug found in this codebase so far was caught by widening
verification, never by adding features: a Linux build found a `strdup` that
truncated a pointer, a real-schema cross-check found a segfault in asn1c's own
`random_fill`, and libFuzzer found a heap overflow in the reader after 300k
executions. Features found nothing.

If a test passes the moment you write it, check that it can fail. Break the code
deliberately and watch it go red.

**Both compilers.** `make check` under clang and under GCC. They disagree in ways
that matter: `strdup` is POSIX and undeclared under glibc with `-std=c99`, so GCC
assumed an `int` return and truncated the pointer on 64-bit — clang never
complained. If you have Docker:

```sh
docker run --rm -v "$PWD":/src -w /src debian:bookworm-slim \
  sh -c 'apt-get update -qq && apt-get install -y -qq gcc make >/dev/null && make check CC=gcc'
```

(asn1c has to be installed in the container too; see `.github/workflows/ci.yml`
for the full recipe.)

**No new warnings.** `-Wall -Wextra` on our own sources, clean. asn1c's generated
code is compiled separately with warnings off, because it is not ours to fix — do
not weaken our flags to quiet it.

**Fuzzing, for anything touching the reader.** It is the only part that takes input
it did not produce.

```sh
make fuzz-read && ./fuzz-read tests/fuzz-corpus -max_total_time=120
```

Seeds from past findings live in `tests/fuzz-corpus/`; add yours when a fuzzer
finds something, so the finding outlives the fix.

**Sanitizers for anything touching memory.**

```sh
make check CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
```

One UBSan report is expected and not ours: `OCTET_STRING.c:618` does `buf + size`
with a null `buf`, in asn1c's XER encoder, which the test harness calls. Upstream
has a PR open for it.

## Invariants worth knowing before you edit

**Never guess a type.** Dispatch compares `td->op` against asn1c's operation-table
globals. An unrecognised table must be a hard error with a named reason, never a
fallback. A caller must not be able to mistake a partial encode for a complete one.

**Never hold an allocation only in a local across a return.** Everything the reader
allocates has to be reachable from `*struct_ptr` before anything can fail, so the
caller's `ASN_STRUCT_FREE` collects it. asn1c's own pattern: an in-progress list
element lives in the list's `_asn_ctx.ptr` until `ASN_SET_ADD` takes it, and the
free function releases it if it is still there.

**Weak definitions, not weak references.** `src/vn_optabs.c` weakly *defines* every
operation table and skeleton helper, because asn1c ships only the skeletons a schema
uses. Weak references would be the obvious approach and do not work on Mach-O,
where they apply only to dynamic libraries. Keep that file a separate translation
unit: a weak definition in the same unit as its reference can be bound at compile
time and lose the override.

**Generated code goes in as object files, never an archive.** The linker pulls an
archive member only to resolve an *undefined* symbol, and every `asn_OP_*` is
already weakly defined — so a skeleton needed solely for its operation table would
never be pulled in and the weak dummy would win.

**`GEN_DIR` is passed with `-idirafter`.** A schema can define a type whose
generated header shadows a system one; the PKIX modules define `Time`, and
`Time.h` captures asn1c's own `#include <time.h>` on a case-insensitive filesystem.

**A golden file you generated is not a golden file.** `t_golden` writes a missing
one and still fails, on purpose. Read it against X.680 before trusting it.

## Style

C99, four spaces, no tabs, roughly 80 columns; `.editorconfig` covers it. Follow
the surrounding code — it is written in asn1c's idiom on purpose, so the two read
as one codebase.

Comments explain *why*, not what. Where behaviour is surprising, say what would
otherwise be assumed: most comments in this codebase exist because something took
a debugging round to find, and the note is there so the next person skips it.

## Commits

Explain the reasoning, not the diff. If a change corrects an earlier assumption,
say what the assumption was — that is usually the useful part. Keep to one logical
change per commit.

## Reporting a problem

A schema fragment that reproduces it is worth more than a description. If it
involves a real encoding, the DER helps; if it involves an interop difference, the
other tool's output helps. Include the asn1c version (`asn1c -v`) — the type
dispatch is pinned to a specific ABI, and a mismatch is the first thing to rule
out.
