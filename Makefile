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
           src/vn_primitive.c src/vn_constructed.c
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

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# ---- CLI ------------------------------------------------------------------
# GEN_DIR is a directory of asn1c output; PDU is the root type's name.

GEN_DIR ?=
PDU     ?=

asn1vn: check-skeldir tools/asn1vn.c $(VN_SRCS)
	@test -n "$(GEN_DIR)" || { echo "set GEN_DIR=<asn1c output dir>" >&2; exit 1; }
	@test -n "$(PDU)"     || { echo "set PDU=<root type name>" >&2; exit 1; }
	$(CC) $(ALL_CFLAGS) -I$(GEN_DIR) -DPDU=$(PDU) \
	    tools/asn1vn.c $(VN_SRCS) \
	    $(filter-out $(GEN_DIR)/converter-example.c,$(wildcard $(GEN_DIR)/*.c)) \
	    -o $@ -lm

# ---- tests ----------------------------------------------------------------

SCHEMAS := prim
TESTS   := t_link t_writer t_dispatch

t_link_SCHEMA   := prim
t_writer_SCHEMA := prim
t_dispatch_SCHEMA := prim

# asn1c writes into the current directory, so generate inside the target.
tests/gen/%/.stamp: tests/schemas/%.asn1
	rm -rf tests/gen/$*
	mkdir -p tests/gen/$*
	cd tests/gen/$* && $(ASN1C) -fcompound-names -no-gen-example \
	    $(abspath $<) >/dev/null
	touch $@

TEST_SUPPORT := tests/vntest.c

define TEST_RULE
tests/bin/$(1): tests/$(1).c $$(TEST_SUPPORT) $$(VN_SRCS) \
                tests/gen/$$($(1)_SCHEMA)/.stamp
	@mkdir -p tests/bin
	$$(CC) $$(ALL_CFLAGS) -Itests -Itests/gen/$$($(1)_SCHEMA) \
	    tests/$(1).c $$(TEST_SUPPORT) $$(VN_SRCS) \
	    $$(wildcard tests/gen/$$($(1)_SCHEMA)/*.c) -o $$@ -lm
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

check: check-skeldir $(addprefix tests/bin/,$(TESTS))
	@rc=0; for t in $(addprefix tests/bin/,$(TESTS)); do \
	    ./$$t || rc=1; \
	done; exit $$rc

clean:
	rm -rf tests/bin tests/gen libvn.a asn1vn $(VN_OBJS)

.PHONY: check clean check-skeldir
