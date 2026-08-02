# asn1c-vn -- ASN.1 value notation encoder for vlm/asn1c.
#
# Requires GNU make, a C99 compiler with weak symbol support (GCC or clang),
# and asn1c on PATH for the test suite.

ASN1C  ?= asn1c
CC     ?= cc
CFLAGS ?= -O2 -g
STD    := -std=c99
WARN   := -Wall -Wextra -Wno-unused-parameter

# asn1c's installed skeleton directory holds the complete header set. The
# library is compiled against it rather than against a generated directory,
# because asn1c copies only the skeletons a given schema happens to use.
SKELDIR ?= $(shell d=$$(command -v $(ASN1C) 2>/dev/null) && \
                   cd "$$(dirname "$$d")/../share/asn1c" 2>/dev/null && pwd)

VN_SRCS := src/vn_writer.c src/vn_encoder.c src/vn_optabs.c \
           src/vn_token.c src/vn_reader.c src/vn_rd_constructed.c \
           src/vn_primitive.c src/vn_constructed.c src/vn_check.c
VN_OBJS := $(VN_SRCS:.c=.o)
VN_INC  := -Iinclude -Isrc

EXTRA :=
ifeq ($(shell uname -s),Darwin)
# GeneralizedTime.c needs struct tm and timegm.
EXTRA += -D_DARWIN_C_SOURCE
endif

ALL_CFLAGS = $(STD) $(WARN) $(CFLAGS) $(EXTRA) $(VN_INC) -I$(SKELDIR)

check-skeldir:
	@test -n "$(SKELDIR)" -a -f "$(SKELDIR)/constr_TYPE.h" || { \
	    echo "cannot locate asn1c's skeleton directory." >&2; \
	    echo "set SKELDIR=<dir containing constr_TYPE.h>" >&2; exit 1; }

# ---- library ---------------------------------------------------------------

libvn.a: check-skeldir $(VN_OBJS)
	ar rcs $@ $(VN_OBJS)

# Both headers are listed rather than tracked with -MMD: only two exist, and
# every source includes at least one, so editing either has to rebuild the lot.
# Without this an edit to vn_encoder.h leaves stale objects in libvn.a.
VN_HDRS := include/vn_encoder.h src/vn_internal.h

%.o: %.c $(VN_HDRS)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# ---- CLI ------------------------------------------------------------------
# GEN_DIR is a directory of asn1c output; PDU is the root type's name.

GEN_DIR ?=
PDU     ?=

# asn1c's generated sources, minus its example main so ours is used instead.
GEN_SRCS = $(filter-out $(GEN_DIR)/converter-example.c,$(wildcard $(GEN_DIR)/*.c))

asn1vn: check-skeldir tools/asn1vn.c $(VN_SRCS)
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	@test -d "$(GEN_DIR)" || { \
	    echo "GEN_DIR=$(GEN_DIR) is not a directory" >&2; exit 1; }
	@# An empty file list would otherwise reach the linker as a pile of
	@# undefined symbols instead of naming the actual problem.
	@test -n "$(strip $(GEN_SRCS))" || { \
	    echo "no *.c files in GEN_DIR=$(GEN_DIR)" >&2; \
	    echo "point GEN_DIR at the directory holding asn1c's output;" >&2; \
	    echo "some projects generate into a subdirectory such as dist/" >&2; \
	    exit 1; }
	@test -f "$(GEN_DIR)/$(PDU).h" || { \
	    echo "GEN_DIR=$(GEN_DIR) has no $(PDU).h, so PDU=$(PDU) is wrong" >&2; \
	    exit 1; }
	@# Generated code is asn1c's, not ours: compile it separately with warnings
	@# off, into our own tree so GEN_DIR is left untouched.
	@#
	@# -idirafter, never -I: a schema may define an ASN.1 type whose generated
	@# header shadows a system one. PKIX defines Time, yielding Time.h, which
	@# on a case-insensitive filesystem captures the #include <time.h> in
	@# GeneralizedTime.c and leaves struct tm incomplete. -idirafter searches
	@# GEN_DIR after the system directories, so system headers win while the
	@# schema's own headers are still found.
	@# Emptied first: the link below globs build/gen/*.o, so objects left by a
	@# previous GEN_DIR would be linked into this binary as well.
	rm -rf build/gen
	@mkdir -p build/gen
	cd build/gen && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w \
	    -idirafter $(abspath $(GEN_DIR)) -c $(abspath $(GEN_SRCS))
	@# Link the identifier table too: it recovers the INTEGER named numbers and
	@# BIT STRING named bits asn1c drops, which the reader needs to accept
	@# reference tooling's output at all. Costs nothing when the schema has none.
	$(MAKE) --no-print-directory vn-annotate
	./vn-annotate $(GEN_DIR) > build/vn_annotations.c
	$(CC) $(ALL_CFLAGS) -idirafter $(GEN_DIR) -DPDU=$(PDU) \
	    tools/asn1vn.c $(VN_SRCS) build/vn_annotations.c build/gen/*.o \
	    -o $@ -lm

# ---- element tree ---------------------------------------------------------
# Prints the schema's element tree as JSON, for documentation tooling. Uses the
# same GEN_DIR/PDU pair as asn1vn, but links none of the vn sources: it only
# reads asn1c's descriptors and never encodes anything.
#
# Its own object directory, because the asn1vn target empties build/gen.

vn-tree: check-skeldir tools/vn-tree.c
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	@test -f "$(GEN_DIR)/$(PDU).h" || { \
	    echo "GEN_DIR=$(GEN_DIR) has no $(PDU).h, so PDU=$(PDU) is wrong" >&2; \
	    exit 1; }
	rm -rf build/gen-tree
	@mkdir -p build/gen-tree
	cd build/gen-tree && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w \
	    -idirafter $(abspath $(GEN_DIR)) -c $(abspath $(GEN_SRCS))
	$(CC) $(ALL_CFLAGS) -idirafter $(GEN_DIR) -DPDU=$(PDU) \
	    tools/vn-tree.c build/gen-tree/*.o -o $@ -lm

# ---- annotation sidecar ---------------------------------------------------
# Recovers the identifiers asn1c parses but does not keep in the runtime
# descriptors -- INTEGER named numbers and BIT STRING named bits -- from the
# generated headers, where they survive as C enums.
#
#   make vn-annotate
#   ./vn-annotate <gen-dir> > vn_annotations.c

vn-annotate: tools/vn-annotate.c
	$(CC) $(STD) $(WARN) $(CFLAGS) $(EXTRA) $(VN_INC) -I$(SKELDIR) $< -o $@

# ---- tests ----------------------------------------------------------------

SCHEMAS := prim constructed strings opentype kitchen annotate annexg constrained
TESTS   := t_link t_writer t_dispatch t_integer t_octet t_sequence t_collection t_bits_oid t_strings t_opentype t_scan t_golden t_xercheck t_norm t_annotate t_roundtrip t_read_negative t_check

t_link_SCHEMA   := prim
t_writer_SCHEMA := prim
t_dispatch_SCHEMA := prim
t_integer_SCHEMA  := prim
t_octet_SCHEMA    := prim
t_sequence_SCHEMA := constructed
t_collection_SCHEMA := constructed
t_bits_oid_SCHEMA := prim
t_strings_SCHEMA  := strings
t_opentype_SCHEMA := opentype
t_scan_SCHEMA     := prim
t_golden_SCHEMA   := constructed
t_xercheck_SCHEMA := kitchen
t_norm_SCHEMA     := prim
t_annotate_SCHEMA := prim
t_roundtrip_SCHEMA := constructed
t_read_negative_SCHEMA := constructed
t_check_SCHEMA    := constrained

# asn1c writes into the current directory, so generate inside the target.
tests/gen/%/.stamp: tests/schemas/%.asn1
	rm -rf tests/gen/$*
	mkdir -p tests/gen/$*
	cd tests/gen/$* && $(ASN1C) -fcompound-names -no-gen-example \
	    $(abspath $<) >/dev/null
	touch $@

# Generated code is asn1c's output, not ours, so it is compiled with warnings
# off. Our own sources keep the full warning set.
#
# The result stays as loose object files rather than an archive on purpose. A
# static archive would break the weak fallbacks in vn_optabs.c: the linker pulls
# an archive member only to resolve an *undefined* symbol, and every asn_OP_*
# table is already weakly defined, so a skeleton needed solely for its operation
# table would never be pulled in and the weak dummy would win. Object files named
# on the command line are always included, so their strong definitions override.
tests/gen/%/.built: tests/gen/%/.stamp
	cd tests/gen/$* && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w -I. -c *.c
	touch $@

TEST_SUPPORT := tests/vntest.c tests/vnscan.c tests/xerscan.c

define TEST_RULE
tests/bin/$(1): tests/$(1).c $$(TEST_SUPPORT) $$(VN_SRCS) \
                tests/gen/$$($(1)_SCHEMA)/.built
	@mkdir -p tests/bin
	$$(CC) $$(ALL_CFLAGS) -Itests -Itests/gen/$$($(1)_SCHEMA) \
	    tests/$(1).c $$(TEST_SUPPORT) $$(VN_SRCS) \
	    tests/gen/$$($(1)_SCHEMA)/*.o -o $$@ -lm
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

# t_annogen checks vn-annotate itself, end to end: the table it generates from
# the annotate schema's headers is compiled in, so it needs a rule of its own.
# t_annexg needs the same treatment for the Annex G schema's identifiers.
tests/gen/%_table.c: vn-annotate tests/gen/%/.stamp
	./vn-annotate tests/gen/$* > $@

define ANNOTATED_TEST_RULE
tests/bin/$(1): tests/$(1).c tests/gen/$(2)_table.c \
                $$(TEST_SUPPORT) $$(VN_SRCS) tests/gen/$(2)/.built
	@mkdir -p tests/bin
	$$(CC) $$(ALL_CFLAGS) -Itests -Itests/gen/$(2) \
	    tests/$(1).c tests/gen/$(2)_table.c $$(TEST_SUPPORT) $$(VN_SRCS) \
	    tests/gen/$(2)/*.o -o $$@ -lm
endef
$(eval $(call ANNOTATED_TEST_RULE,t_annogen,annotate))
$(eval $(call ANNOTATED_TEST_RULE,t_annexg,annexg))

ANNOTATED_TESTS := t_annogen t_annexg

check: check-skeldir $(addprefix tests/bin/,$(TESTS) $(ANNOTATED_TESTS))
	@rc=0; for t in $(addprefix tests/bin/,$(TESTS) $(ANNOTATED_TESTS)); do \
	    ./$$t || rc=1; \
	done; exit $$rc

# ---- comparison against another tool's output ------------------------------
#
# Diffs our output against reference value notation produced by a different
# implementation. REFDIR holds <name>.der files next to <name>.txt (or .vn/.val)
# references. Uses -A so we emit value assignments, the form reference tooling
# uses, making the comparison a direct diff.
#
# Exact agreement is not reachable yet: asn1c discards DEFAULT values for
# buffer-backed types and INTEGER named numbers, so a reference tool that reads
# the schema prints information we cannot recover. See README "Deviations from
# X.680". The recorded baseline therefore guards against regression rather than
# asserting equality.

REFDIR   ?=
BASELINE := tests/reference-baseline.txt

# Aim the property-based cross-check at a real-world schema instead of the
# kitchen-sink one. Far more type combinations, nesting depth and string types
# than a hand-written test schema reaches.
#   make check-xer GEN_DIR=<gen> PDU=<Type> [ROUNDS=20000]
ROUNDS ?= 5000

check-xer: check-skeldir
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	@test -n "$(strip $(GEN_SRCS))" || { \
	    echo "no *.c files in GEN_DIR=$(GEN_DIR)" >&2; exit 1; }
	rm -rf build/gen
	@mkdir -p build/gen tests/bin
	cd build/gen && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w \
	    -idirafter $(abspath $(GEN_DIR)) -c $(abspath $(GEN_SRCS))
	$(CC) $(ALL_CFLAGS) -Itests -idirafter $(GEN_DIR) -DPDU=$(PDU) \
	    tests/t_xercheck.c $(TEST_SUPPORT) $(VN_SRCS) build/gen/*.o \
	    -o tests/bin/t_xercheck_ext -lm
	@# DERDIR drives the check from real encodings. asn_random_fill cannot be
	@# used on every schema: constr_SET_OF.c:1329 calls random_fill without a
	@# NULL check and neither ANY nor OPEN_TYPE provides one, so a
	@# SEQUENCE OF ANY crashes inside asn1c.
	@if [ -n "$(DERDIR)" ]; then \
	    set -- "$(DERDIR)"/*.der; \
	    [ -f "$$1" ] || { echo "no *.der in DERDIR=$(DERDIR)" >&2; exit 1; }; \
	    ./tests/bin/t_xercheck_ext "$$@"; \
	else \
	    ./tests/bin/t_xercheck_ext $(ROUNDS); \
	fi

check-reference: asn1vn
	@test -n "$(REFDIR)" || { \
	    echo "set REFDIR=<dir with .der files and reference .txt files>" >&2; \
	    exit 1; }
	@tmp=$$(mktemp -d); rc=0; : > "$$tmp/now"; \
	for der in "$(REFDIR)"/*.der; do \
	    [ -f "$$der" ] || continue; \
	    base=$${der%.der}; ref=""; \
	    for ext in txt vn val; do \
	        [ -f "$$base.$$ext" ] && ref="$$base.$$ext" && break; \
	    done; \
	    [ -n "$$ref" ] || { echo "SKIP $$(basename "$$base"): no reference file"; continue; }; \
	    ./asn1vn -A -c < "$$der" | sed '/^[[:space:]]*$$/d' > "$$tmp/ours" || { \
	        echo "FAIL $$(basename "$$base"): encoding failed"; rc=1; continue; }; \
	    tr -d '\r' < "$$ref" | sed '/^[[:space:]]*$$/d' > "$$tmp/ref"; \
	    n=$$(diff "$$tmp/ref" "$$tmp/ours" | grep -c '^[<>]' || true); \
	    total=$$(wc -l < "$$tmp/ref" | tr -d ' '); \
	    printf '%s\t%s\n' "$$(basename "$$base")" "$$n" >> "$$tmp/now"; \
	    if [ "$$n" -eq 0 ]; then echo "PASS $$(basename "$$base") ($$total lines)"; \
	    else echo "DIFF $$(basename "$$base"): $$n of $$total lines"; fi; \
	done; \
	if [ -f "$(BASELINE)" ]; then \
	    while IFS="$$(printf '\t')" read -r name was; do \
	        [ -n "$$name" ] || continue; \
	        now=$$(awk -F'\t' -v n="$$name" '$$1==n{print $$2}' "$$tmp/now"); \
	        [ -n "$$now" ] || continue; \
	        if [ "$$now" -gt "$$was" ]; then \
	            echo "REGRESSION $$name: $$was -> $$now differing lines" >&2; rc=1; \
	        elif [ "$$now" -lt "$$was" ]; then \
	            echo "IMPROVED $$name: $$was -> $$now; update $(BASELINE)"; \
	        fi; \
	    done < "$(BASELINE)"; \
	else \
	    cp "$$tmp/now" "$(BASELINE)"; \
	    echo "wrote $(BASELINE); review it, then commit"; \
	fi; \
	rm -rf "$$tmp"; exit $$rc

clean:
	rm -rf tests/bin tests/gen build libvn.a asn1vn asn1vn-named \
	    vn-annotate fuzz-read crash-* leak-* timeout-* $(VN_OBJS)

.PHONY: check clean check-skeldir check-reference check-xer check-roundtrip check-vn-corpus fuzz-read

# Round trip over real encodings: DER -> value notation -> DER, byte-compared.
# The acceptance criterion for the codec, and it needs no external reference.
#   make check-roundtrip GEN_DIR=<gen> PDU=<Type> DERDIR=<dir with *.der>
# Value notation written by a foreign producer -- the only corpus that can show
# the reader agrees with the standard rather than merely with our writer. TCA
# ships its reference ProfileElements this way; the files are theirs, so they are
# not vendored here, they are pointed at.
#   make check-vn-corpus GEN_DIR=<gen> PDU=<Type> VNDIR=<dir with *.asn1>
check-vn-corpus: check-skeldir
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	@test -n "$(VNDIR)"   || { echo "set VNDIR=<dir with value notation>" >&2; exit 1; }
	rm -rf build/gen
	@mkdir -p build/gen tests/bin
	cd build/gen && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w \
	    -idirafter $(abspath $(GEN_DIR)) -c $(abspath $(GEN_SRCS))
	$(MAKE) --no-print-directory vn-annotate
	./vn-annotate $(GEN_DIR) > build/vn_annotations.c
	$(CC) $(ALL_CFLAGS) -Itests -idirafter $(GEN_DIR) -DVC_PDU=$(PDU) -DVC_EXTERNAL \
	    tests/t_vncorpus.c $(TEST_SUPPORT) $(VN_SRCS) build/vn_annotations.c \
	    build/gen/*.o -o tests/bin/t_vncorpus -lm
	@find "$(VNDIR)" -name '*.asn1' -o -name '*.vn' | sort > build/vn_corpus.list
	@test -s build/vn_corpus.list || { \
	    echo "no *.asn1 or *.vn files under VNDIR=$(VNDIR)" >&2; exit 1; }
	@tr '\n' '\0' < build/vn_corpus.list | xargs -0 ./tests/bin/t_vncorpus

check-roundtrip: check-skeldir
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	@test -n "$(DERDIR)"  || { echo "set DERDIR=<dir with *.der>" >&2; exit 1; }
	rm -rf build/gen
	@mkdir -p build/gen tests/bin
	cd build/gen && $(CC) $(STD) $(CFLAGS) $(EXTRA) -w \
	    -idirafter $(abspath $(GEN_DIR)) -c $(abspath $(GEN_SRCS))
	$(CC) $(ALL_CFLAGS) -Itests -idirafter $(GEN_DIR) -DRT_PDU=$(PDU) -DRT_EXTERNAL \
	    tests/t_roundtrip.c $(TEST_SUPPORT) $(VN_SRCS) build/gen/*.o \
	    -o tests/bin/t_roundtrip_ext -lm
	@set -- "$(DERDIR)"/*.der; \
	 [ -f "$$1" ] || { echo "no *.der in DERDIR=$(DERDIR)" >&2; exit 1; }; \
	 ./tests/bin/t_roundtrip_ext "$$@"

# ---- fuzzing ---------------------------------------------------------------
# The reader is the only part that takes input it did not produce, so this is a
# requirement rather than a nicety. Needs clang; only meaningful with sanitizers.
#   make fuzz-read && ./fuzz-read -max_total_time=60

FUZZ_SAN ?= -fsanitize=fuzzer,address,undefined

# Apple's clang ships without libFuzzer, so a real clang is needed on macOS.
# Homebrew's llvm has it; on Linux the system clang is enough.
FUZZ_CC ?= $(shell for c in /opt/homebrew/opt/llvm/bin/clang \
                            /usr/local/opt/llvm/bin/clang clang; do \
                      command -v $$c >/dev/null 2>&1 && echo $$c && break; \
                  done)

fuzz-read: check-skeldir tests/gen/constructed/.built
	@test -n "$(FUZZ_CC)" || { echo "no clang found for -fsanitize=fuzzer" >&2; exit 1; }
	$(FUZZ_CC) $(STD) $(WARN) -O1 -g $(EXTRA) $(VN_INC) -I$(SKELDIR) \
	    -Itests -Itests/gen/constructed $(FUZZ_SAN) \
	    tests/fuzz_read.c $(VN_SRCS) tests/gen/constructed/*.o -o $@ -lm
