# ToasterCG for AROS
#
#   make                  the AROS x86_64 program (build/aros-x86_64/)
#   make aros-i386        the same for i386 (build/aros-i386/)
#   make host             host tools (build/host/): uitest, cgscript, ...
#   make test             the host regression suite (tests/run.sh)
#   make dist             the AROS drawers and archives (build/dist/)
#
# SDK_x86_64 / SDK_i386: an AROS Developer directory (includes and
# libraries: freetype2, reqtools); CROSS_x86_64 / CROSS_i386: the
# cross compiler prefixes. Give them on the command line or in local.mk,
# which is not in the repository.
-include local.mk
SDK_x86_64   ?=
SDK_i386     ?=
CROSS_x86_64 ?= x86_64-aros-
CROSS_i386   ?= i386-aros-

CORE = src/cgbook.c src/cgbook_save.c src/cgrender.c src/cgilbm.c src/cgtfont.c src/cgedit.c \
       src/cgcmd.c src/cgpage.c src/cgplay.c
UI   = src/ui/cgui.c src/ui/canvas.c src/ui/cgartfile.c src/ui/cgtrace.c src/ui/trace.c src/ui/vecart.c
HDRS = $(wildcard include/*.h src/ui/*.h)

AROS_CFLAGS = -std=gnu99 -O2 -Wall -Wextra -Werror -Iinclude -Isrc/ui
AROS_LIBS   = -lfreetype2 -lreqtoolsstubs -lrexxsyslib -lkeymap -lcybergraphics -licon
# what is built is stripped the way AROS strips its own programs
STRIPFLAGS  = --strip-unneeded -R.comment

HOST_CFLAGS = -std=c99 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -Isrc/ui \
              $(shell pkg-config --cflags freetype2 libpng)
HOST_LIBS   = $(shell pkg-config --libs freetype2 libpng) -lm

.PHONY: all aros-x86_64 aros-i386 host test dist clean

all: aros-x86_64

define aros_target
aros-$(1): build/aros-$(1)/CG build/aros-$(1)/cgrx

build/aros-$(1)/CG: src/aros/cg.c $(CORE) $(UI) $(HDRS)
	@test -e $(SDK_$(1))/include/exec/types.h || { echo "SDK_$(1) is not an AROS SDK: '$(SDK_$(1))'"; exit 1; }
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(AROS_CFLAGS) -I$(SDK_$(1))/include -I$(SDK_$(1))/include/freetype \
	    -o $$@ src/aros/cg.c $(CORE) $(UI) -L$(SDK_$(1))/lib $(AROS_LIBS)
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@

build/aros-$(1)/cgrx: tools/aros/cgrx.c include/version.h
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(AROS_CFLAGS) -I$(SDK_$(1))/include -o $$@ tools/aros/cgrx.c \
	    -L$(SDK_$(1))/lib -lrexxsyslib
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
endef
$(eval $(call aros_target,x86_64))
$(eval $(call aros_target,i386))

HOST_TOOLS = build/host/uitest build/host/cgscript build/host/pagerender build/host/frametest

host: $(HOST_TOOLS)

build/host/uitest: tools/uitest.c $(CORE) $(UI) $(HDRS)
	@mkdir -p $(@D)
	cc $(HOST_CFLAGS) -o $@ tools/uitest.c $(CORE) $(UI) $(HOST_LIBS)

build/host/%: tools/%.c $(CORE) $(HDRS)
	@mkdir -p $(@D)
	cc $(HOST_CFLAGS) -o $@ $< $(CORE) $(HOST_LIBS)

test:
	tests/run.sh

dist: aros-x86_64 aros-i386
	tools/dist.sh

clean:
	rm -rf build/aros-x86_64 build/aros-i386 build/dist
