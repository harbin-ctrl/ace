CC        := ccache gcc
STANDARDS := -D_POSIX_C_SOURCE=200908L -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
# TINE is an ACE command.  Its executable is still a host process (ACE runs
# it through LNX), but pathname semantics come from ACE's live DOS session.
# Keep the resolver in ACE rather than growing a second broker client here.
ACE_DIR   ?= $(abspath ../..)
ACE_BUILD ?= $(ACE_DIR)/build
ACE_CFLAGS := -I$(ACE_DIR)/src -I$(ACE_DIR)/compat
CFLAGS    := $(STANDARDS) $(CURSES) $(ACE_CFLAGS) -Os
DESTDIR   ?= /usr/local
CCACHE_PREFIX ?= distcc
export CCACHE_PREFIX

ACE_PATH_OBJS := $(ACE_BUILD)/ace-amiga-posix.o $(ACE_BUILD)/broker_client.o

all: tine
	strip tine

clean:
	rm -rf *.o tine

tine: buffer.o command.o editor.o mode.o parser.o tine_console.o util.o \
      $(ACE_PATH_OBJS)

$(ACE_PATH_OBJS):
	$(MAKE) -C "$(ACE_DIR)" "$(patsubst $(ACE_DIR)/%,%,$@)"

install: all
	mkdir -p "$(DESTDIR)/bin" "$(DESTDIR)/share/man/man1"
	cp tine "$(DESTDIR)/bin/tine"
	cp tine.1 "$(DESTDIR)/share/man/man1/tine.1"
