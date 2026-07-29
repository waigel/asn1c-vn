# Integrating asn1c-vn into an asn1c converter tool

`ept` in [euicc-profile-tool](https://github.com/waigel/euicc-profile-tool) is
asn1c's `converter-example.c` built with `ASN_PDU_COLLECTION`. Encoding there goes
through a single call, `asn_encode(NULL, osyntax, ...)`, which dispatches on
`enum asn_transfer_syntax`. Value notation is not in that enum, so it needs a
branch of its own — that is the whole of `ept-vn.patch`, 81 lines including
comments.

The same patch applies to any tool built from `converter-example.c`.

## Why bother, given `asn1vn` already exists

`converter-example.c` is built with `ASN_PDU_COLLECTION`, so it selects the type
at **runtime** via `-p <TypeName>`. The standalone `asn1vn` is fixed to one PDU at
compile time. Integrating gives value notation for every type in the schema.

## What the patch adds

```
-ovn     Output as ASN.1 value notation (X.680)
-ovnc    ... canonical: deterministic, for diffing
-ovna    ... annotated: adds X.680 comments
```

Existing output formats are untouched; `-oxer`, `-oder` and `-onull -c` were
verified unchanged.

## Makefile

Two flags are required that are easy to get wrong:

1. **`-I$(SKELETONS)`** must be present. asn1c copies only the skeletons a schema
   actually uses, so a generated directory may lack, say, `RELATIVE-OID.h`, while
   this library needs the complete header set. See the ABI pinning section of the
   main README.
2. **`-idirafter $(DIST)`, never `-I`.** A schema may define an ASN.1 type whose
   generated header shadows a system one: the PKIX modules define `Time`,
   producing `Time.h`, which on a case-insensitive filesystem captures the
   `#include <time.h>` inside asn1c's own `GeneralizedTime.c` and leaves
   `struct tm` incomplete.

```make
# A submodule keeps this reproducible for anyone cloning the project;
# a plain relative path works for a side-by-side checkout.
VN_DIR ?= asn1c-vn

$(DIST)/converter-example.c: $(ASN1_SOURCES)
	mkdir -p $(DIST)
	$(ASN1C) -S $(SKELETONS) -pdu=auto -fcompound-names -D $(DIST) $(ASN1_SOURCES)
	patch -p1 -N -d $(DIST) < $(VN_DIR)/contrib/ept-vn.patch

$(TOOL): $(DIST)/converter-example.c
	cd $(DIST) && cc -D_DARWIN_C_SOURCE -DASN_PDU_COLLECTION -DPDU=ProfileElement \
		-I$(abspath $(VN_DIR))/include -I$(abspath $(SKELETONS)) -idirafter . \
		$$(ls *.c | grep -v '^converter-example\.c$$') converter-example.c \
		$(abspath $(VN_DIR))/src/*.c \
		-o ../$(TOOL) -lm
```

The patch is applied immediately after generation, because `asn1c -D $(DIST)`
rewrites the file every time. `-N` keeps `patch` from prompting; if asn1c's
`converter-example.c` ever changes enough that the hunks no longer apply, the
build fails loudly rather than silently dropping value notation support.

## Alternative: own the main

Instead of patching, copy `converter-example.c` once into your own repository as
`ept.c` and stop regenerating it. Then the code is yours and nothing overwrites
it — at the price of maintaining 1020 lines of asn1c example code. The patch is
the smaller intervention as long as you stay on asn1c 0.9.29.

## Verified

- builds warning-free against a generated directory plus `asn1c-vn/src`
- `-ovnc` output is identical to the standalone `asn1vn`: the same 573 of 3655
  differing lines against the GSMA reference value notation
- `-p ProfileElement` selects the type at runtime as expected
- `-oxer`, `-oder` and `-onull -c` behave as before
