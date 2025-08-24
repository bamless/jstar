VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION_PATCH = 0
VERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)

CFLAGS ?= \
	-std=gnu99                    \
	-Wall -Wextra -Werror=switch  \
	-fomit-frame-pointer -fno-plt

BUILDDIR ?= build
PREFIX   ?= /usr/local

# ---- OPTIONS
# Enable Optimizations, strip executables and don't include debug information
ifneq ($(RELEASE),)
	CFLAGS  += -O3 -DNDEBUG
	LDFLAGS += -s
else
	CFLAGS += -ggdb
endif
# Use link-time optimizations during linking
ifneq ($(USE_LTO),)
	CFLAGS += -flto
	ifeq ($(CC), clang)
		LDFLAGS += -fuse-ld=lld
	endif
endif
ifeq ($(shell uname),Darwin)
	SO = dylib
else
	SO = so
endif
# ---- END OPTIONS

# ---- TOP-LEVEL TARGETS
.PHONY: all libjstar jstar jstarc

all: argparse replxx includes
	@$(MAKE) --no-print-directory      \
	    $(BUILDDIR)/lib/libjstar.$(SO) \
	    $(BUILDDIR)/lib/libjstar.a     \
	    $(BUILDDIR)/bin/jstar          \
	    $(BUILDDIR)/bin/jstarc

libjstar: includes
	@$(MAKE) --no-print-directory $(BUILDDIR)/lib/libjstar.$(SO) $(BUILDDIR)/lib/libjstar.a

jstarc: argparse includes
	@$(MAKE) --no-print-directory $(BUILDDIR)/bin/jstarc

jstar: argparse replxx includes
	@$(MAKE) --no-print-directory $(BUILDDIR)/bin/jstar
# ---- END TOP-LEVEL TARGETS

SOURCES = \
    parse/ast.c          \
    parse/lex.c          \
    parse/parser.c       \
    builtins/builtins.c  \
    builtins/core/core.c \
    builtins/core/std.c  \
    builtins/core/excs.c \
    builtins/core/iter.c \
	builtins/sys.c       \
	builtins/io.c        \
	builtins/math.c      \
	builtins/debug.c     \
	builtins/re.c        \
    buffer.c             \
    code.c               \
    compiler.c           \
    disassemble.c        \
    gc.c                 \
    import.c             \
    int_hashtable.c      \
    jstar.c              \
    object.c             \
    opcode.c             \
    profile.c            \
    serialize.c          \
    value.c              \
    value_hashtable.c    \
    vm.c

JSTAR_BLTINS = \
    builtins/core/core.jsc \
    builtins/core/std.jsc  \
    builtins/core/excs.jsc \
    builtins/core/iter.jsc \
	builtins/sys.jsc       \
	builtins/io.jsc        \
	builtins/math.jsc      \
	builtins/debug.jsc     \
	builtins/re.jsc

JSTAR_CFLAGS  = -I./include/
JSTAR_LDFLAGS = -L$(BUILDDIR)/lib -l:libjstar.so.$(VERSION_MAJOR)
OBJECTS       = $(SOURCES:%.c=$(BUILDDIR)/src/%.o)
INCLUDES      = $(JSTAR_BLTINS:%.jsc=src/%.jsc.inc)
DEPS          = $(OBJECTS:.o=.d)

ifeq ($(shell $(CC) --version 2>&1 | head -n1 | grep -ic gcc),1)
	VM_CFLAGS = -fno-crossjumping
else
	VM_CFLAGS =
endif

$(BUILDDIR)/src/vm.o: src/vm.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(VM_CFLAGS) -MP -MMD -I./include/jstar -I./src -fPIC -c $< -o $@

$(BUILDDIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MP -MMD -I./include/jstar -I./src -fPIC -c $< -o $@

src/%.jsc.inc: src/%.jsc $(BUILDDIR)/bin2incl
	$(BUILDDIR)/bin2incl $< $@

$(BUILDDIR)/lib/libjstar.$(SO).$(VERSION): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) -shared -lm $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILDDIR)/lib/libjstar.a: $(OBJECTS)
	$(AR) cr $@ $(OBJECTS)

$(BUILDDIR)/lib/libjstar.$(SO).$(VERSION_MAJOR): $(BUILDDIR)/lib/libjstar.$(SO).$(VERSION)
	ln -fs ./libjstar.$(SO).$(VERSION) $(BUILDDIR)/lib/libjstar.$(SO).$(VERSION_MAJOR)

$(BUILDDIR)/lib/libjstar.$(SO): $(BUILDDIR)/lib/libjstar.$(SO).$(VERSION_MAJOR)
	ln -fs ./libjstar.$(SO).$(VERSION) $(BUILDDIR)/lib/libjstar.$(SO)

.PHONY: includes
includes: cwalk
	@$(MAKE) --no-print-directory $(INCLUDES)

# ---- Apps
RPATH = -Wl,-rpath,$(shell pwd)/$(BUILDDIR)/lib

# -- jstar repl
JSTAR_SOURCES = \
	apps/jstar/completion.c   \
	apps/jstar/console_print.c\
	apps/jstar/highlighter.c  \
	apps/jstar/import.c       \
	apps/jstar/main.c
JSTAR_OBJECTS = $(JSTAR_SOURCES:%.c=$(BUILDDIR)/%.o)
DEPS += $(JSTAR_OBJECTS:.o=.d)

$(BUILDDIR)/apps/jstar/%.o: apps/jstar/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MP -MMD $(JSTAR_CFLAGS) $(COMMON_CFLAGS) $(ARGPARSE_CFLAGS) $(REPLXX_CFLAGS) -fPIC -c $< -o $@

$(BUILDDIR)/bin/jstar: $(JSTAR_OBJECTS)                                \
                       $(BUILDDIR)/lib/libjstar.$(SO).$(VERSION_MAJOR) \
                       $(BUILDDIR)/argparse/libargparse.a              \
                       $(BUILDDIR)/replxx/libreplxx.a                  \
                       $(BUILDDIR)/libcommon.a
	@mkdir -p $(dir $@)
	$(CXX) -o $(BUILDDIR)/bin/jstar -MP -MMD $(CFLAGS) $(LDFLAGS) $(RPATH) $(JSTAR_OBJECTS) \
		-ldl                \
		$(JSTAR_LDFLAGS)    \
		$(COMMON_LDFLAGS)   \
		$(ARGPARSE_LDFLAGS) \
		$(REPLXX_LDFLAGS)

# -- jstarc
JSTARC_SOURCES  = apps/jstarc/main.c
DEPS           += $(JSTARC_OBJECTS:.o=.d)

$(BUILDDIR)/bin/jstarc: $(JSTARC_SOURCES)                               \
                        $(BUILDDIR)/lib/libjstar.$(SO).$(VERSION_MAJOR) \
                        $(BUILDDIR)/argparse/libargparse.a              \
                        $(BUILDDIR)/libcommon.a
	@mkdir -p $(dir $@)
	$(CC) -o $(BUILDDIR)/bin/jstarc -MP -MD $(CFLAGS) $(LDFLAGS) $(RPATH) $(JSTARC_SOURCES) \
		$(JSTAR_CFLAGS) $(JSTAR_LDFLAGS)  \
		$(COMMON_CFLAGS) $(COMMON_LDFLAGS) \
		$(ARGPARSE_CFLAGS) $(ARGPARSE_LDFLAGS)

# -- libcommon
COMMON_CFLAGS  = -I./apps/common
COMMON_LDFLAGS = -L$(BUILDDIR) -l:libcommon.a -L$(BUILDDIR)/cwalk -l:libcwalk.a
COMMON_SOURCES = extlib.c path.c
COMMON_OBJECTS = $(COMMON_SOURCES:%.c=$(BUILDDIR)/apps/common/%.o)
DEPS           += $(COMMON_OBJECTS:.o=.d)

$(BUILDDIR)/apps/common/%.o: apps/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MP -MMD -I./extern/cwalk/include -I./src -fPIC -c $< -o $@

$(BUILDDIR)/libcommon.a: $(COMMON_OBJECTS) $(BUILDDIR)/cwalk/libcwalk.a
	$(AR) cr $@ $(COMMON_OBJECTS)

# -- bin2incl
$(BUILDDIR)/bin2incl: apps/bin2incl/main.c $(BUILDDIR)/libcommon.a
	$(CC) -o $@ -MP -MMD $(CFLAGS) $(LDFLAGS) $< $(COMMON_CFLAGS) $(COMMON_LDFLAGS)

-include $(DEPS)
# ----

# ---- Dependencies
CMAKE_BUILD_TYPE = Debug
ifneq ($(RELEASE),)
	CMAKE_BUILD_TYPE = Release
endif
.PHONY: cwalk argparse replxx

CWALK_CFLAGS  = -I./extern/cwalk/include
CWALK_LDFLAGS = -L$(BUILDDIR)/cwalk -l:libcwalk.a
cwalk:
	@[ -d $(BUILDDIR)/cwalk ] || (mkdir -p $(BUILDDIR)/cwalk; cmake -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -S ./extern/cwalk/ -B $(BUILDDIR)/cwalk)
	@$(MAKE) --no-print-directory -C $(BUILDDIR)/cwalk

ARGPARSE_CFLAGS  = -I./extern/argparse/
ARGPARSE_LDFLAGS = -L$(BUILDDIR)/argparse -l:libargparse.a
argparse:
	@[ -d $(BUILDDIR)/argparse ] || (mkdir -p $(BUILDDIR)/argparse; cmake -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -S ./extern/argparse/ -B $(BUILDDIR)/argparse)
	@$(MAKE) --no-print-directory -C $(BUILDDIR)/argparse

REPLXX_CFLAGS  = -I./extern/replxx/include/
REPLXX_LDFLAGS = -L$(BUILDDIR)/replxx/ -l:libreplxx.a
replxx:
	@[ -d $(BUILDDIR)/replxx ] || (mkdir -p $(BUILDDIR)/replxx; cmake -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DREPLXX_BUILD_EXAMPLES=0 -DREPLXX_BUILD_PACKAGE=0 -S ./extern/replxx/ -B $(BUILDDIR)/replxx)
	@$(MAKE) --no-print-directory -C $(BUILDDIR)/replxx
	@if [ -f $(BUILDDIR)/replxx/libreplxx-d.a ]; then \
		cp $(BUILDDIR)/replxx/libreplxx-d.a $(BUILDDIR)/replxx/libreplxx.a; \
	fi
# ----

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)

ORIGIN = $$ORIGIN
ifeq ($(shell uname),Darwin)
	ORIGIN = @executable_path
endif

.PHONY: install
install:
	@echo "Installing to '$(PREFIX)'..."
	install -d $(PREFIX)/bin
	install -m755 $(BUILDDIR)/bin/jstar $(PREFIX)/bin
	install -m755 $(BUILDDIR)/bin/jstarc $(PREFIX)/bin
	cp -r $(BUILDDIR)/lib $(PREFIX)
	cp -r ./include $(PREFIX)
	mkdir -p $(PREFIX)/pkgconfig
	awk -v prefix="$(shell realpath $(PREFIX))" \
        -v libdir="lib" \
        -v includedir="include" \
        -v Name="jstar" \
        -v Description="A lightweight embeddable scripting language" \
        -v URL="https://bamless.github.io/jstar/" \
        -v Version="$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)" \
        -v ExtraLibs="m -ldl" \
        '{ \
           gsub(/@CMAKE_INSTALL_PREFIX@/, prefix); \
           gsub(/@CMAKE_INSTALL_LIBDIR@/, libdir); \
           gsub(/@CMAKE_INSTALL_INCLUDEDIR@/, includedir); \
           gsub(/@PROJECT_NAME@/, Name); \
           gsub(/@CMAKE_PROJECT_DESCRIPTION@/, Description); \
           gsub(/@CMAKE_PROJECT_HOMEPAGE_URL@/, URL); \
           gsub(/@JSTAR_VERSION@/, Version); \
           gsub(/@EXTRA_LIBS@/, ExtraLibs); \
           print \
         }' ./cmake/jstar.pc.in > $(PREFIX)/pkgconfig/jstar.pc
	patchelf --set-rpath '$(ORIGIN)/../lib' --force-rpath $(PREFIX)/bin/jstar
	patchelf --set-rpath '$(ORIGIN)/../lib' --force-rpath $(PREFIX)/bin/jstarc
	mkdir -p $(PREFIX)/share/licenses/jstar
	cp ./LICENSE $(PREFIX)/share/licenses/jstar
	cp ./extern/argparse/LICENSE $(PREFIX)/share/licenses/jstar/argparse
	cp ./extern/replxx/LICENSE.md $(PREFIX)/share/licenses/jstar/replxx
	cp ./extern/cwalk/LICENSE.md $(PREFIX)/share/licenses/jstar/cwalk
