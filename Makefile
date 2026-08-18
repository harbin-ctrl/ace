ifeq ($(origin CC),default)
CC := ccache gcc
endif
# ccache handles the local cache; its prefix sends cache misses through
# distcc. Both remain overridable for local or cross builds.
CCACHE_PREFIX ?= distcc
export CCACHE_PREFIX
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-pointer-sign -O2

# Header dependency tracking.  Without this, editing a header in compat/
# rebuilds nothing that includes it, because the rules below list only their
# .c files.  That is not merely a stale build: compat/include/dos/dosextens.h
# defines struct Process, so a half-rebuilt tree links objects that disagree
# about where pr_CurrentDir and pr_CIS live, and the result deadlocks rather
# than failing to compile.  `override` so the tracking survives a CFLAGS
# override on the command line.
# -MF is explicit because distcc rewrites the compile command and, without
# it, drops the dependency file beside the source under its basename -- which
# lands stray .d files in the repository root rather than in build/.
override CFLAGS += -MMD -MP -MF $(BUILD)/$(@F).d
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
BLKID_CFLAGS := $(shell pkg-config --cflags blkid)
BLKID_LIBS := $(shell pkg-config --libs blkid)
GFX_CFLAGS := $(shell pkg-config --cflags cairo fontconfig)
GFX_LIBS := $(shell pkg-config --libs cairo fontconfig)
WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client)
WAYLAND_LIBS := $(shell pkg-config --libs wayland-client)
# ACE installs into the user's own prefix, not the system's. Every ACE
# program finds its companions -- the shell, the console, the broker, the AROS
# commands -- beside its own executable, so an install is a set that has to
# stay together, and two sets on one PATH drift silently: the older one keeps
# working, just old, while the newer one never runs. Defaulting here means the
# obvious command is the correct one and needs no sudo.
#
# A system-wide install is still an ordinary PREFIX override --
# `sudo make PREFIX=/usr/local AROS_ROOT="$$HOME/aros" install`, with AROS_ROOT
# passed explicitly because sudo resets HOME -- but it is deliberately not a
# target of its own. Nothing should reach it by accident.
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPLICATIONSDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor/512x512/apps
# What SYS: means on this host: the boot volume's root, laid out the Amiga way
# so C: really is SYS:C and S: really is SYS:S. The binaries themselves stay in
# BINDIR, where a Linux user's PATH can reach them and where every "look for
# the console beside me" lookup already resolves; SYS:C holds symlinks to them,
# so both views are true at once and neither is a copy of the other.
SYSDIR ?= $(DATADIR)/ace
INSTALL ?= install
CURL ?= curl
TAR ?= tar
SHA256SUM ?= sha256sum

COMPAT := $(CURDIR)/compat/include
BUILD := $(CURDIR)/build
AROS_ROOT ?= $(HOME)/aros
VIM_SRC ?=
HOST_ARCH ?= $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
AROS_CPU_ARCH ?= aarch64-all
else ifeq ($(HOST_ARCH),x86_64)
AROS_CPU_ARCH ?= x86_64-all
else ifeq ($(HOST_ARCH),i686)
AROS_CPU_ARCH ?= i386-all
else ifeq ($(HOST_ARCH),i386)
AROS_CPU_ARCH ?= i386-all
else ifneq (,$(filter arm%,$(HOST_ARCH)))
AROS_CPU_ARCH ?= arm-all
else
AROS_CPU_ARCH ?= x86_64-all
endif
AROS_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Echo.c
AROS_CD_SRC := src/cd.c
AROS_PATHPART_SRC := $(AROS_ROOT)/workbench/c/shellcommands/PathPart.c
AROS_WHICH_SRC := $(AROS_ROOT)/workbench/c/Which.c
AROS_FAULT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Fault.c
AROS_ASK_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Ask.c
AROS_IF_SRC := $(AROS_ROOT)/workbench/c/shellcommands/If.c
AROS_ELSE_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Else.c
AROS_ENDIF_SRC := $(AROS_ROOT)/workbench/c/shellcommands/EndIf.c
AROS_ENDSKIP_SRC := $(AROS_ROOT)/workbench/c/shellcommands/EndSkip.c
AROS_LAB_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Lab.c
AROS_SKIP_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Skip.c
ACE_QUIT_SRC := src/quit.c
ACE_EDIT_SRC := src/edit.c
ACE_ED_SRC := src/ed_tine.c
AROS_GET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Get.c
AROS_GETENV_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Getenv.c
AROS_SETENV_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Setenv.c
AROS_UNSETENV_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Unsetenv.c
AROS_SET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Set.c
AROS_UNSET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Unset.c
AROS_ALIAS_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Alias.c
AROS_UNALIAS_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Unalias.c
AROS_FAILAT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/FailAt.c
AROS_WHY_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Why.c
AROS_PROMPT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Prompt.c
INSTALL_LNX_SRC := src/lnx.c
AROS_MAKEDIR_SRC := $(AROS_ROOT)/workbench/c/MakeDir.c
AROS_MAKELINK_SRC := $(AROS_ROOT)/workbench/c/MakeLink.c
AROS_JOIN_SRC := $(AROS_ROOT)/workbench/c/Join.c
AROS_EVAL_SRC := $(AROS_ROOT)/workbench/c/Eval.c
AROS_EVAL_PARSER_SRC := $(AROS_ROOT)/workbench/c/evalParser.y
AROS_ENDCLI_SRC := $(AROS_ROOT)/workbench/c/shellcommands/EndCLI.c
AROS_NEWCLI_SRC := $(AROS_ROOT)/workbench/c/shellcommands/NewCLI.c
AROS_ASSIGN_SRC := $(AROS_ROOT)/workbench/c/Assign.c
AROS_RELABEL_SRC := $(AROS_ROOT)/workbench/c/Relabel.c
AROS_TYPE_SRC := $(AROS_ROOT)/workbench/c/Type.c
AROS_RENAME_SRC := $(AROS_ROOT)/workbench/c/Rename.c
AROS_STACK_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Stack.c
AROS_DOSPATH_DIR := $(AROS_ROOT)/rom/dos
AROS_DIR_SRC := $(AROS_ROOT)/workbench/c/Dir.c
AROS_DELETE_SRC := $(AROS_ROOT)/workbench/c/Delete.c
AROS_PROTECT_SRC := $(AROS_ROOT)/workbench/c/Protect.c
AROS_FILENOTE_SRC := $(AROS_ROOT)/workbench/c/Filenote.c
AROS_COPY_SRC := $(AROS_ROOT)/workbench/c/Copy.c
AROS_LIST_SRC := $(AROS_ROOT)/workbench/c/List.c
AROS_SORT_SRC := $(AROS_ROOT)/workbench/c/Sort.c
AROS_SEARCH_SRC := $(AROS_ROOT)/workbench/c/Search.c
AROS_TOUCH_SRC := $(AROS_ROOT)/workbench/c/Touch.c
AROS_BEEP_SRC := $(AROS_ROOT)/workbench/c/Beep.c
AROS_WAIT_SRC := $(AROS_ROOT)/workbench/c/Wait.c
LHA_AROS_VERSION := 1.14i.1
LHA_AROS_URL := https://github.com/sodero/lha/archive/refs/tags/$(LHA_AROS_VERSION).tar.gz
LHA_AROS_SHA256 := 6776004c56c5fcbbed746517ecf4882e6170d1e5ee553ef228f0e6ff5e4f304b
LHA_AROS_ARCHIVE := $(BUILD)/lha-aros-$(LHA_AROS_VERSION).tar.gz
LHA_AROS_DIR := $(BUILD)/lha-aros-$(LHA_AROS_VERSION)
LHA_AROS_SOURCE_STAMP := $(LHA_AROS_DIR)/.source-ready
LHA_AROS_CONFIG := $(CURDIR)/config/lha-aros/config.h
LHA_AROS_NAMES := append bitio crcio dhuf extract getopt_long header huf indicator \
                  larc lhadd lharc lhext lhlist maketbl maketree patmatch \
                  pm2 pm2hist pm2tree shuf slide support_utf8 util
LHA_AROS_OBJS := $(addprefix $(BUILD)/lha-aros-,$(addsuffix .o,$(LHA_AROS_NAMES)))
LHA_AROS_INCLUDES := -I$(CURDIR)/config/lha-aros -I$(LHA_AROS_DIR) \
                     -I$(LHA_AROS_DIR)/src -Isrc \
                     -I$(CURDIR)/compat/aros-real/include -I$(COMPAT) \
                     -I$(AROS_ROOT)/arch/all-pc/include \
                     -I$(AROS_ROOT)/arch/$(AROS_CPU_ARCH)/include \
                     -I$(AROS_ROOT)/compiler/arossupport/include \
                     -I$(AROS_ROOT)/compiler/include
LHA_AROS_CFLAGS := -D_AMIGA -D__AROS__ -DEXPAND_WILDCARDS \
                   -DHAVE_CONFIG_H -D_XOPEN_SOURCE=700 \
                   -Wno-return-mismatch -Wno-unused-parameter \
                   -Wno-pointer-sign -Wno-unused-variable \
                   -Wno-unused-but-set-variable \
                   -Wno-implicit-function-declaration \
                   -Wno-int-conversion -Wno-int-to-pointer-cast \
                   -Wno-sign-compare -Wno-missing-field-initializers \
                   -Wno-strict-aliasing -Wno-maybe-uninitialized \
                   -Wno-format -Wno-unused-function \
                   -Wno-implicit-fallthrough \
                   -Wno-dangling-pointer \
                   -Wno-parentheses \
                   -Wno-stringop-truncation \
                   -include dos/dos.h -include ace_amiga_posix.h \
                   -Dfopen=ace_amiga_posix_fopen \
                   -Dopen=ace_amiga_posix_open \
                   -Dmkstemp=ace_amiga_posix_mkstemp \
                   -Dstat=ace_amiga_posix_stat \
                   -Dlstat=ace_amiga_posix_lstat \
                   -Daccess=ace_amiga_posix_access \
                   -Dmkdir=ace_amiga_posix_mkdir \
                   -Dopendir=ace_amiga_posix_opendir \
                   -Drename=ace_amiga_posix_rename \
                   -Dunlink=ace_amiga_posix_unlink \
                   -Dremove=ace_amiga_posix_remove \
                   -Drmdir=ace_amiga_posix_rmdir \
                   -Dchmod=ace_amiga_posix_chmod \
                   -Dutime=ace_amiga_posix_utime \
                   -Dutimes=ace_amiga_posix_utimes \
                   -Dsymlink=ace_amiga_posix_symlink \
                   -Dreadlink=ace_amiga_posix_readlink
AROS_DOSPAT_DIR := $(AROS_ROOT)/rom/dos
AROS_DOSPAT_NAMES := patternmatching matchpattern parsepattern \
                     matchpatternnocase parsepatternnocase \
                     exall matchfirst matchnext matchend match_misc
AROS_DOSPAT_OBJS := $(addprefix $(BUILD)/aros-dos-,$(addsuffix .o,$(AROS_DOSPAT_NAMES)))
# -funsigned-char is a correctness flag here, not a warning relaxation. Real
# AROS declares STRPTR as UBYTE*, so a parsed pattern's tokens -- P_ANY and
# friends, 0x80..0x88 in dos/dosasl.h -- read back as the values
# patternmatching.c switches on. ACE's compat header declares STRPTR as plain
# char*, which is unsigned on ARM but signed on x86, and on a signed-char host
# those tokens read back negative and every one of those case labels becomes
# unreachable: the pattern matcher silently stops recognising "#?" at all.
# gcc catches it as -Werror=switch-outside-range. Making plain char unsigned
# for these translation units restores the type AROS wrote them against.
AROS_DOSPAT_CFLAGS := -funsigned-char \
                      -Wno-implicit-function-declaration \
                      -Wno-int-conversion -Wno-int-to-pointer-cast \
                      -Wno-sign-compare -Wno-missing-field-initializers \
                      -Wno-unused-but-set-variable -Wno-strict-aliasing \
                      -Wno-maybe-uninitialized \
                      -include ace_dos_intern.h
# AROS declares a command's arguments with the AROS_SHn macros, and AROS's own
# compiler/include/aros/shcommands.h is what expands them -- into the
# AllocDosObject(DOS_RDARGS)/ReadArgs()/FreeArgs() sequence in
# shcommands_notembedded.h. Compiling that header is what puts these commands
# on the real argument parser; ACE used to restate it, and the restatement
# parsed the templates itself.
#
# -I$(COMPAT) stays ahead of the AROS include directory in every rule below,
# so ACE's compat headers win wherever both trees have one and AROS's are
# reached only for what compat does not provide. ace_shcommand_host.h
# supplies the host process entry point the macro expansion assumes.
AROS_SHCOMMAND_CFLAGS := -I$(AROS_ROOT)/compiler/include \
                         -include ace_shcommand_host.h
AROS_SHCOMMAND_INCLUDES := -I$(AROS_ROOT)/workbench/c/shellcommands

AROS_BOOPSI_DIR := $(AROS_ROOT)/rom/intuition
AROS_ALIB_DIR := $(AROS_ROOT)/compiler/alib
AROS_BOOPSI_NAMES := rootclass makeclass freeclass addclass removeclass \
                     findclass newobjecta disposeobject setattrsa getattr \
                     nextobject
AROS_ALIB_NAMES := domethod dosupermethod coercemethod alib_util
AROS_BOOPSI_OBJS := $(addprefix $(BUILD)/aros-boopsi-,$(addsuffix .o,$(AROS_BOOPSI_NAMES))) \
                    $(addprefix $(BUILD)/aros-alib-,$(addsuffix .o,$(AROS_ALIB_NAMES)))
AROS_GRAPHICS_DIR := $(AROS_ROOT)/rom/devs/console
AROS_GRAPHICS_NAMES := stdconclass consoleclass support
AROS_GRAPHICS_OBJS := $(addprefix $(BUILD)/aros-console-,$(addsuffix .o,$(AROS_GRAPHICS_NAMES)))
AROS_ARSUPPORT_DIR := $(AROS_ROOT)/compiler/arossupport
AROS_ARSUPPORT_NAMES := libfindtagitem libnexttagitem
AROS_ARSUPPORT_OBJS := $(addprefix $(BUILD)/aros-arsupport-,$(addsuffix .o,$(AROS_ARSUPPORT_NAMES)))
AROS_GRAPHICS_INCLUDES := -I$(CURDIR)/compat/aros-real/include \
                          -I$(AROS_ROOT)/arch/all-pc/include \
                          -I$(AROS_ROOT)/arch/$(AROS_CPU_ARCH)/include \
                          -I$(AROS_ROOT)/compiler/arossupport/include \
                          -I$(AROS_ROOT)/compiler/include \
                          -I$(AROS_ROOT)/rom/devs/console/include \
                          -I$(AROS_ROOT)/rom/devs/console
AROS_GRAPHICS_CFLAGS := -Wno-implicit-function-declaration \
                        -Wno-int-conversion -Wno-int-to-pointer-cast \
                        -Wno-pointer-sign -Wno-sign-compare \
                        -Wno-missing-field-initializers \
                        -Wno-unused-but-set-variable -Wno-strict-aliasing \
                        -Wno-maybe-uninitialized -Wno-unused-variable \
                        -include ace_graphics_intern.h
AROS_CON_HANDLER_SRC := $(AROS_ROOT)/rom/filesys/console_handler/con_handler.c
AROS_CON_SUPPORT_SRC := $(AROS_ROOT)/rom/filesys/console_handler/support.c
AROS_CON_COMPLETION_SRC := $(AROS_ROOT)/rom/filesys/console_handler/completion.c
AROS_SHELL_NAMES := buffer cliEcho cliLen cliNan cliPrompt cliVarNum \
                    convertArg convertBackTicks convertLine convertLineDot \
                    convertRedir convertVar interpreter readLine redirection
AROS_SHELL_OBJS := $(addprefix $(BUILD)/aros-shell-,$(addsuffix .o,$(AROS_SHELL_NAMES)))
AROS_REAL_INCLUDES := -I$(CURDIR)/compat/aros-real/include \
                      -I$(AROS_ROOT)/arch/all-pc/include \
                      -I$(AROS_ROOT)/arch/$(AROS_CPU_ARCH)/include \
                      -I$(AROS_ROOT)/compiler/arossupport/include \
                      -I$(AROS_ROOT)/compiler/include \
                      -I$(AROS_ROOT)/rom/devs/console \
                      -I$(AROS_ROOT)/rom/filesys/console_handler
AROS_REAL_CFLAGS := -Wno-implicit-function-declaration \
                    -Wno-int-conversion -Wno-int-to-pointer-cast \
                    -Wno-pointer-sign -Wno-sign-compare \
                    -Wno-missing-field-initializers \
                    -Wno-unused-but-set-variable -Wno-strict-aliasing \
                    -Wno-maybe-uninitialized \
                    -DACE_NO_CONSOLE_MENU -DACE_NO_CONSOLE_APPWINDOW \
                    -DACE_NO_CONSOLE_COMPLETION \
                    -include ace_handler_types.h
# The BOOPSI sources are compiled against the ACE Intuition seam instead of the
# console handler seam, so they take the same warning relaxations but their own
# forced include.  See compat/aros-real/include/ace_boopsi_intern.h.
AROS_BOOPSI_CFLAGS := -Wno-implicit-function-declaration \
                      -Wno-int-conversion -Wno-int-to-pointer-cast \
                      -Wno-pointer-sign -Wno-sign-compare \
                      -Wno-missing-field-initializers \
                      -Wno-unused-but-set-variable -Wno-strict-aliasing \
                      -Wno-maybe-uninitialized \
                      -include ace_boopsi_intern.h
AROS_BOOPSI_INCLUDES := -I$(CURDIR)/compat/aros-real/include \
                        -I$(AROS_ROOT)/arch/all-pc/include \
                        -I$(AROS_ROOT)/arch/$(AROS_CPU_ARCH)/include \
                        -I$(AROS_ROOT)/compiler/arossupport/include \
                        -I$(AROS_ROOT)/compiler/include \
                        -I$(AROS_ALIB_DIR)
# The AmigaDOS commands: what a user types at the shell, and what SYS:C is a
# drawer of. C: is the loader's last resort, so a command reachable by name
AMIGA_COMMANDS := Echo CD Path PathPart Which Dir Delete Protect Filenote Fault Ask Get Getenv Set Unset Alias Unalias Beep \
                  FailAt Why Prompt Clip Cut MakeDir MakeLink Join Eval Edit ED Info Copy List Sort Search Touch EndCLI Assign Relabel Type Rename Stack Run LNX NewCLI \
                  If Else EndIf EndSkip Lab Quit Skip Execute Setenv Unsetenv Wait Status Break LhA
# The host side: a launcher, the console, the shell the console starts, and
# the broker with its control tool. These are entry points into ACE rather
# than commands within it, and they are not in SYS:C.
HOST_BINS := ace-shell ace-user-shell ace-console ace-broker ace-brokerctl acepaste
INSTALL_BINS := $(AMIGA_COMMANDS) $(HOST_BINS)

all: $(BUILD)/Echo $(BUILD)/CD $(BUILD)/Path $(BUILD)/PathPart $(BUILD)/Which $(BUILD)/Dir $(BUILD)/Delete $(BUILD)/Protect $(BUILD)/Filenote $(BUILD)/Fault $(BUILD)/Ask $(BUILD)/Get $(BUILD)/Getenv $(BUILD)/Set $(BUILD)/Unset $(BUILD)/Alias $(BUILD)/Unalias $(BUILD)/Beep $(BUILD)/FailAt $(BUILD)/Why $(BUILD)/Prompt $(BUILD)/Clip $(BUILD)/Cut $(BUILD)/MakeDir $(BUILD)/MakeLink $(BUILD)/Join $(BUILD)/Eval $(BUILD)/Edit $(BUILD)/ED $(BUILD)/Info $(BUILD)/Copy $(BUILD)/List $(BUILD)/Sort $(BUILD)/Search $(BUILD)/Touch $(BUILD)/EndCLI $(BUILD)/Assign $(BUILD)/Relabel $(BUILD)/Type $(BUILD)/Rename $(BUILD)/Stack $(BUILD)/Run $(BUILD)/LNX $(BUILD)/LhA $(BUILD)/ace-shell $(BUILD)/ace-user-shell $(BUILD)/ace-console $(BUILD)/NewCLI $(BUILD)/If $(BUILD)/Else $(BUILD)/EndIf $(BUILD)/EndSkip $(BUILD)/Lab $(BUILD)/Quit $(BUILD)/Skip $(BUILD)/Execute $(BUILD)/Setenv $(BUILD)/Unsetenv $(BUILD)/Wait $(BUILD)/Status $(BUILD)/Break $(BUILD)/ace-broker $(BUILD)/ace-brokerctl $(BUILD)/acepaste $(BUILD)/ace-amiga-posix.o $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o $(BUILD)/aros-con-handler.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o $(BUILD)/aros-console-editor.o $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS)

$(BUILD)/break-probe: tests/break_probe.c $(BUILD)/dos-runtime.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/broker-task-test: tests/broker_task_test.c $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) -pthread -Isrc $(filter-out %.h,$^) -o $@

.PHONY: break-signal-test
break-signal-test: $(BUILD)/break-probe $(BUILD)/ace-user-shell
	python3 tests/break_signal_test.py

# ET (Edified Tine) is a guest program, so its build is deliberately separate
# from the core command graph.  Install invokes it after ACE itself is built; this
# avoids two make processes racing to build ACE's shared broker objects.
TINE_DIR ?= $(CURDIR)/tools/tine
.PHONY: tine
tine:
	$(MAKE) -C "$(TINE_DIR)" ACE_DIR="$(CURDIR)" tine

$(BUILD):
	mkdir -p $@

lha-fetch: $(LHA_AROS_SOURCE_STAMP)

lha: $(BUILD)/LhA

$(LHA_AROS_ARCHIVE): | $(BUILD)
	$(CURL) --location --fail --silent --show-error --output "$@" "$(LHA_AROS_URL)"
	printf '%s  %s\n' "$(LHA_AROS_SHA256)" "$@" | $(SHA256SUM) --check --status -

$(LHA_AROS_SOURCE_STAMP): $(LHA_AROS_ARCHIVE) | $(BUILD)
	mkdir -p "$(LHA_AROS_DIR)"
	$(TAR) --extract --gzip --file "$<" --strip-components=1 --directory "$(LHA_AROS_DIR)"
	touch "$@"

$(BUILD)/native_dos.o: src/native_dos.c src/broker_protocol.h src/broker_client.h src/aros_dos_path.h src/aros_console_editor.h src/console_channel.h src/native_console_endpoint.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/ace-amiga-posix.o: src/ace_amiga_posix.c src/ace_amiga_posix.h \
                            src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

# The fetched source path does not exist until the stamp recipe runs, so keep
# the stamp as the Make prerequisite and name the generated .c file explicitly
# in the recipe below.
$(BUILD)/lha-aros-%.o: $(LHA_AROS_SOURCE_STAMP) $(LHA_AROS_CONFIG) \
                       src/ace_amiga_posix.h | $(BUILD)
	$(CC) $(CFLAGS) $(LHA_AROS_CFLAGS) $(LHA_AROS_INCLUDES) \
	    -c $(LHA_AROS_DIR)/src/$*.c -o $@

$(BUILD)/LhA: $(LHA_AROS_OBJS) $(BUILD)/ace-amiga-posix.o \
             $(BUILD)/dos-runtime.o $(BUILD)/native_dos.o \
             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
             $(BUILD)/broker_client.o $(AROS_DOSPAT_OBJS)
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/ace-vim-runtime.o: src/ace_vim_runtime.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_command.o: src/native_command.c src/broker_protocol.h src/ace_shell_break.h src/aros_exec_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/ace-launcher.o: src/ace_launcher.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_shcommand.o: src/native_shcommand.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_process.o: src/native_process.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/makedir.o: $(AROS_MAKEDIR_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/MakeLink.o: $(AROS_MAKELINK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Join.o: $(AROS_JOIN_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

# Edit has no AROS source to fetch: it is written here, to dos.library and
# exec.library alone, so the one file also builds with an Amiga or AROS
# compiler. On ACE it enters through the same seam as any command that keeps
# its own main() and calls ReadArgs() itself.
$(BUILD)/Edit.o: $(ACE_EDIT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

# bison writes both files in one run, so they are a grouped target (&:).
# Listing only the .c leaves the .h with no rule at all, which a parallel
# build reports as "No rule to make target" the moment it schedules Eval.o
# before the .c has been made.  A serial build hid this by happening to make
# the .c first, and a tree carrying a pre-generated .h hid it completely.
$(BUILD)/evalParser.tab.c $(BUILD)/evalParser.tab.h &: $(AROS_EVAL_PARSER_SRC) | $(BUILD)
	bison --defines=$(BUILD)/evalParser.tab.h -o $(BUILD)/evalParser.tab.c $<

$(BUILD)/Eval.o: $(AROS_EVAL_SRC) $(BUILD)/evalParser.tab.c $(BUILD)/evalParser.tab.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -I$(BUILD) -include strings.h -include stdlib.h \
	    -Dmalloc=ace_eval_malloc -Dfree=ace_eval_free \
	    -Dmain=ace_command_entry_main -c $< -o $@

# Copy predates the AROS_SHn command-entry macros and enters through an
# AROS process-start function. Its fetched source stays unmodified: expose
# that function from this translation unit and call it from src/copy_entry.c.
$(BUILD)/Copy.o: $(AROS_COPY_SRC) compat/include/dos/dosextens.h | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-variable \
	    -Wno-unused-but-set-variable -Wno-maybe-uninitialized \
	    -Wno-implicit-function-declaration -Wno-int-conversion \
	    -Wno-int-to-pointer-cast -Wno-sign-compare \
	    -I$(COMPAT) -I$(AROS_ROOT)/compiler/include -D__AROS__ -Dstatic= \
	    -DGetDeviceProc=ace_aros_GetDeviceProc \
	    -DFreeDeviceProc=ace_aros_FreeDeviceProc \
	    -include dos/dosextens.h -c $< -o $@

$(BUILD)/copy_entry.o: src/copy_entry.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/List.o: $(AROS_LIST_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-compare-distinct-pointer-types \
	    -I$(COMPAT) -I$(AROS_ROOT)/compiler/include \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Sort.o: $(AROS_SORT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-compare-distinct-pointer-types -Wno-sign-compare \
	    -I$(COMPAT) -I$(AROS_ROOT)/compiler/include \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Search.o: $(AROS_SEARCH_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-compare-distinct-pointer-types -Wno-sign-compare \
	    -I$(COMPAT) -I$(AROS_ROOT)/compiler/include \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Touch.o: $(AROS_TOUCH_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Wait.o: $(AROS_WAIT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/arch/all-pc/include \
	    -I$(AROS_ROOT)/arch/$(AROS_CPU_ARCH)/include \
	    -I$(AROS_ROOT)/compiler/include \
	    -DTICKS_PER_SECOND=50 -include dos/datetime.h \
	    -include aros_exec_runtime.h -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/status.o: src/status.c src/broker_client.h src/broker_protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/break.o: src/break.c src/broker_client.h src/broker_protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/Beep.o: $(AROS_BEEP_SRC) src/assign_compat.h \
                 compat/include/intuition/intuitionbase.h | $(BUILD)
	$(CC) $(CFLAGS) -D__AROS__ -I$(COMPAT) \
	    -I$(AROS_ROOT)/compiler/include -include src/assign_compat.h \
	    -c $< -o $@

$(BUILD)/beep_entry.o: src/beep_entry.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/wayward_beep.o: src/wayward_beep.c src/wayward_beep.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/native_command_entry.o: src/native_command_entry.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

# Protect refuses to change a whole volume or device rather than an object
# inside one, and asks AROS's own arossupport routine which it is.
$(BUILD)/aros-arsupport-isdosentrya.o: $(AROS_ARSUPPORT_DIR)/isdosentrya.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -include proto/exec.h -include src/assign_compat.h \
	    -Wno-implicit-function-declaration -c $< -o $@

# Delete and Protect declare their arguments by calling ReadArgs() directly
# rather than through the AROS_SHn macros, so they take the generic host entry
# point in src/native_command_entry.c. Both drive the real AROS pattern
# matcher Dir already uses.
$(BUILD)/Delete.o: $(AROS_DELETE_SRC) compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Protect.o: $(AROS_PROTECT_SRC) compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) \
	    -Dmain=ace_command_entry_main -c $< -o $@

# -D__AROS__ is what lets Filenote.c call IsDosEntryA(). Without it the file
# defines the call away to 0 for a non-AROS build, and Filenote would happily
# try to comment a whole volume.
$(BUILD)/Filenote.o: $(AROS_FILENOTE_SRC) compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) -D__AROS__ \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/endcli.o: $(AROS_ENDCLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

AROS_ASSIGN_CFLAGS := -Wno-implicit-function-declaration \
                      -Wno-int-conversion -Wno-int-to-pointer-cast \
                      -Wno-pointer-sign -Wno-sign-compare \
                      -Wno-missing-field-initializers -Wno-strict-aliasing \
                      -Wno-unused-but-set-variable
AROS_ASSIGN_INCLUDES := -I$(CURDIR)/compat/aros-real/include -I$(COMPAT) \
                        -I$(AROS_ROOT)/compiler/include -Isrc

AROS_DOSPATH_CFLAGS := -Wno-implicit-function-declaration \
                       -Wno-int-conversion -Wno-int-to-pointer-cast \
                       -Wno-pointer-sign -Wno-unused-variable \
                       -Wno-sign-compare -Wno-missing-field-initializers \
                       -Wno-unused-but-set-variable -Wno-strict-aliasing
AROS_DOSPATH_INCLUDES := -I$(COMPAT) -Isrc
DOS_RUNTIME_OBJ := $(BUILD)/dos-runtime.o

$(BUILD)/Assign.o: $(AROS_ASSIGN_SRC) src/assign_compat.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_ASSIGN_CFLAGS) -D__AROS__ $(AROS_ASSIGN_INCLUDES) \
	    -include src/assign_compat.h -c $< -o $@

$(BUILD)/assign_entry.o: src/assign_entry.c src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/assign_compat.o: src/assign_compat.c src/assign_compat.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-dos-getdeviceproc.o: $(AROS_DOSPATH_DIR)/getdeviceproc.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DGetDeviceProc=ace_aros_GetDeviceProc -include ace_dos_path_intern.h \
	    -c $< -o $@

$(BUILD)/aros-dos-freedeviceproc.o: $(AROS_DOSPATH_DIR)/freedeviceproc.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DFreeDeviceProc=ace_aros_FreeDeviceProc -include ace_dos_path_intern.h \
	    -c $< -o $@

$(BUILD)/aros-dos-readargs.o: $(AROS_DOSPATH_DIR)/readargs.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DReadArgs=ace_aros_ReadArgs -DFreeArgs=ace_aros_FreeArgs \
	    -DFindArg=ace_aros_FindArg -DStrToLong=ace_aros_StrToLong \
	    -include ace_dos_path_intern.h -c $< -o $@

$(BUILD)/aros-dos-readitem.o: $(AROS_DOSPATH_DIR)/readitem.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -include ace_dos_path_intern.h \
	    -c $< -o $@

$(BUILD)/aros-dos-freeargs.o: $(AROS_DOSPATH_DIR)/freeargs.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DFreeArgs=ace_aros_FreeArgs -include ace_dos_path_intern.h \
	    -c $< -o $@

$(BUILD)/aros-dos-findarg.o: $(AROS_DOSPATH_DIR)/findarg.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DFindArg=ace_aros_FindArg \
	    -include ace_dos_path_intern.h \
	    -c $< -o $@

$(BUILD)/aros-dos-strtolong.o: $(AROS_DOSPATH_DIR)/strtolong.c compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPATH_CFLAGS) $(AROS_DOSPATH_INCLUDES) \
	    -DStrToLong=ace_aros_StrToLong -include ace_dos_path_intern.h \
	    -c $< -o $@

$(DOS_RUNTIME_OBJ): $(BUILD)/assign_compat.o \
                    $(BUILD)/aros-dos-getdeviceproc.o \
                    $(BUILD)/aros-dos-freedeviceproc.o \
                    $(BUILD)/aros-dos-readargs.o \
                    $(BUILD)/aros-dos-readitem.o \
                    $(BUILD)/aros-dos-freeargs.o \
                    $(BUILD)/aros-dos-findarg.o \
                    $(BUILD)/aros-dos-strtolong.o \
                    $(BUILD)/aros-console-editor.o \
                    $(BUILD)/aros-con-support.o \
                    $(BUILD)/aros-exec-runtime.o \
                    $(BUILD)/clipboard-bridge.o \
                    $(BUILD)/notify-compat.o \
                    $(BUILD)/clipboard-device.o \
                    $(BUILD)/iffparse-clipboard.o \
                    $(BUILD)/native-list-compat.o \
                    $(BUILD)/ace-vim-runtime.o \
                    $(BUILD)/console_channel.o \
                    $(BUILD)/console_device.o $(BUILD)/con_handler.o \
                    $(BUILD)/native_console_endpoint.o | $(BUILD)
	$(CC) $(CFLAGS) -r $(filter-out %.h,$^) -o $@

$(BUILD)/native-list-compat.o: src/native_list_compat.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Assign: $(BUILD)/Assign.o $(BUILD)/assign_entry.o \
                 $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
                 $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
                 $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Relabel.o: $(AROS_RELABEL_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -include ace_dos_path_intern.h \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Relabel: $(BUILD)/Relabel.o $(BUILD)/native_command_entry.o \
                  $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
                  $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
                  $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Info.o: src/info.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -Dmain=ace_command_entry_main \
	    -c $< -o $@

$(BUILD)/Info: $(BUILD)/Info.o $(BUILD)/native_command_entry.o \
               $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
               $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
               $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/LNX.o: $(INSTALL_LNX_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/broker_client.o: src/broker_client.c src/broker_protocol.h src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

# The broker is where SYS: is established, so it is the one object that has to
# be told where this build was installed. An uninstalled broker finds neither
# this path nor an ACE_SYS_DIR in its environment and falls back to its own
# directory, which for a build tree is where the commands are.
$(BUILD)/broker.o: src/broker.c src/broker_protocol.h src/dos_devices.h src/clipboard_bridge.h | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -DACE_SYS_DIR='"$(SYSDIR)"' -Isrc -c $< -o $@

$(BUILD)/dos-devices.o: src/dos_devices.c src/dos_devices.h | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/brokerctl.o: src/brokerctl.c src/broker_protocol.h src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_shell.o: src/amiga_shell.c src/broker_protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_console.o: src/amiga_console.c src/console_channel.h src/console_device_bridge.h src/ace_appmenu_wayland.h compat/include/libraries/iffparse.h compat/include/proto/iffparse.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GTK_CFLAGS) $(GFX_CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/console_channel.o: src/console_channel.c src/console_channel.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/ace-appmenu-wayland.o: src/ace_appmenu_wayland.c src/ace_appmenu_wayland.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GTK_CFLAGS) $(WAYLAND_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console_device_bridge.o: src/console_device_bridge.c src/console_device_bridge.h compat/aros-real/include/ace_graphics_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(GFX_CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -Isrc -c $< -o $@

$(BUILD)/console_device.o: src/console_device.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/con_handler.o: src/con_handler.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/native_console_endpoint.o: src/native_console_endpoint.c \
                                    src/native_console_endpoint.h \
                                    src/con_handler.h src/console_device.h \
                                    src/console_channel.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/aros-con-handler.o: $(AROS_CON_HANDLER_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-con-support.o: $(AROS_CON_SUPPORT_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-con-completion.o: $(AROS_CON_COMPLETION_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-boopsi-%.o: $(AROS_BOOPSI_DIR)/%.c compat/aros-real/include/ace_boopsi_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_BOOPSI_CFLAGS) $(AROS_BOOPSI_INCLUDES) -c $< -o $@

$(BUILD)/aros-alib-%.o: $(AROS_ALIB_DIR)/%.c compat/aros-real/include/ace_boopsi_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_BOOPSI_CFLAGS) $(AROS_BOOPSI_INCLUDES) -c $< -o $@

$(BUILD)/aros-boopsi-runtime.o: src/aros_boopsi_runtime.c src/aros_boopsi_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_BOOPSI_CFLAGS) $(AROS_BOOPSI_INCLUDES) -c $< -o $@

$(BUILD)/boopsi-test.o: tests/boopsi_test.c src/aros_boopsi_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_BOOPSI_CFLAGS) $(AROS_BOOPSI_INCLUDES) -Isrc -c $< -o $@

$(BUILD)/boopsi-test: $(BUILD)/boopsi-test.o $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS)
	$(CC) $(CFLAGS) -pthread $(filter-out %.h,$^) -o $@

$(BUILD)/aros-console-%.o: $(AROS_GRAPHICS_DIR)/%.c compat/aros-real/include/ace_graphics_intern.h compat/aros-real/include/proto/exec.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -c $< -o $@

$(BUILD)/aros-arsupport-%.o: $(AROS_ARSUPPORT_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -c $< -o $@

$(BUILD)/aros-graphics-runtime.o: src/aros_graphics_runtime.c src/aros_graphics_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GFX_CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -c $< -o $@

$(BUILD)/graphics-test.o: tests/graphics_test.c src/aros_graphics_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GFX_CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -Isrc -c $< -o $@

$(BUILD)/console-device-bridge-test.o: tests/console_device_bridge_test.c src/console_device_bridge.h src/aros_graphics_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GFX_CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -Isrc -c $< -o $@

$(BUILD)/graphics-test: $(BUILD)/graphics-test.o $(BUILD)/aros-graphics-runtime.o \
                        $(AROS_GRAPHICS_OBJS) $(AROS_ARSUPPORT_OBJS) \
                        $(AROS_BOOPSI_OBJS) $(BUILD)/aros-boopsi-runtime.o
	$(CC) $(CFLAGS) -pthread $(filter-out %.h,$^) $(GFX_LIBS) -o $@

$(BUILD)/console-device-bridge-test: $(BUILD)/console-device-bridge-test.o $(BUILD)/console_device_bridge.o \
                                     $(BUILD)/aros-graphics-runtime.o $(AROS_GRAPHICS_OBJS) \
                                     $(AROS_ARSUPPORT_OBJS) $(AROS_BOOPSI_OBJS) \
                                     $(BUILD)/aros-boopsi-runtime.o
	$(CC) $(CFLAGS) -pthread $(filter-out %.h,$^) $(GFX_LIBS) -o $@

$(BUILD)/console-channel-test.o: tests/console_channel_test.c src/console_channel.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console-channel-test: $(BUILD)/console-channel-test.o $(BUILD)/console_channel.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/aros-exec-runtime.o: src/aros_exec_runtime.c src/aros_exec_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/clipboard-bridge.o: src/clipboard_bridge.c src/clipboard_bridge.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/notify-compat.o: src/notify_compat.c src/aros_exec_runtime.h \
                          src/clipboard_bridge.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/clipboard-device.o: src/clipboard_device.c src/clipboard_device.h src/clipboard_bridge.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/iffparse-clipboard.o: src/iffparse_clipboard.c src/aros_exec_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-console-editor.o: src/aros_console_editor.c src/aros_console_editor.h $(AROS_CON_SUPPORT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-console-editor-stubs.o: src/aros_console_editor_stubs.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/exec_compat.o: src/exec_compat.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/exec_compat_bindings.o: src/exec_compat_bindings.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/console-device-test: tests/console_device_test.c $(BUILD)/console_device.o $(BUILD)/con_handler.o
	$(CC) $(CFLAGS) -pthread -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/aros-exec-runtime-test: tests/aros_exec_runtime_test.c \
                                 $(BUILD)/aros-exec-runtime.o \
                                 $(BUILD)/clipboard-device.o \
                                 $(BUILD)/clipboard-bridge.o
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_INCLUDES) $(filter-out %.h,$^) -o $@

$(BUILD)/aros-task-signal-test: tests/aros_task_signal_test.c \
                                 $(BUILD)/aros-exec-runtime.o \
                                 $(BUILD)/clipboard-device.o \
                                 $(BUILD)/clipboard-bridge.o
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_INCLUDES) $(filter-out %.h,$^) -o $@

$(BUILD)/iffparse-clipboard-test: tests/iffparse_clipboard_test.c \
                                  $(BUILD)/aros-exec-runtime.o \
                                  $(BUILD)/clipboard-device.o \
                                  $(BUILD)/clipboard-bridge.o \
                                  $(BUILD)/iffparse-clipboard.o
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/acepaste.o: src/acepaste.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/acepaste: $(BUILD)/acepaste.o $(BUILD)/aros-exec-runtime.o \
                   $(BUILD)/clipboard-device.o $(BUILD)/clipboard-bridge.o \
                   $(BUILD)/iffparse-clipboard.o
	$(CC) $(CFLAGS) -pthread $(filter-out %.h,$^) -o $@

$(BUILD)/native-input-test: tests/native_input_test.c $(DOS_RUNTIME_OBJ) \
                            $(BUILD)/native_dos.o $(BUILD)/native_command.o \
                            $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/native-console-handle-test: tests/native_console_handle_test.c $(DOS_RUNTIME_OBJ) \
                                     $(BUILD)/native_dos.o $(BUILD)/native_command.o \
                                     $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/aros-console-editor-test: tests/aros_console_editor_test.c $(BUILD)/aros-console-editor.o $(BUILD)/aros-console-editor-stubs.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o $(BUILD)/clipboard-device.o $(BUILD)/clipboard-bridge.o $(BUILD)/ace-vim-runtime.o \
                                   $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS) \
                                   $(BUILD)/aros-graphics-runtime.o $(AROS_ARSUPPORT_OBJS)
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) $(filter-out %.h,$^) $(GFX_LIBS) -o $@

$(BUILD)/exec-compat-test: tests/exec_compat_test.c $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o
	$(CC) $(CFLAGS) -pthread -DAMIGA_EXEC_COMPAT_ENABLED -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

$(BUILD)/aros-newcli.o: $(AROS_NEWCLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/aros-shell-runtime.o: src/aros_shell_runtime.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/aros-real-shell.o: $(AROS_ROOT)/workbench/c/Shell/Shell.c | $(BUILD)
	$(CC) $(CFLAGS) -Dmain=ace_aros_shell_main -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/ace-user-shell.o: src/ace_user_shell.c src/broker_client.h src/native_host.h src/ace_shell_break.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-shell-%.o: $(AROS_ROOT)/workbench/c/Shell/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/Echo.o: $(AROS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/CD.o: $(AROS_CD_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_DOSPAT_CFLAGS) -c $< -o $@

$(BUILD)/PathPart.o: $(AROS_PATHPART_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Path.o: src/path.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Which.o: $(AROS_WHICH_SRC) compat/include/ace_dos_path_intern.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_DOSPATH_CFLAGS) \
	    -DGetDeviceProc=ace_aros_GetDeviceProc \
	    -DFreeDeviceProc=ace_aros_FreeDeviceProc \
	    -include ace_dos_path_intern.h -Dmain=ace_command_entry_main \
	    -c $< -o $@

$(BUILD)/Type.o: $(AROS_TYPE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-sign-compare -I$(COMPAT) \
	    -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Rename.o: $(AROS_RENAME_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_command_entry_main -c $< -o $@

$(BUILD)/Stack.o: $(AROS_STACK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/run_entry.o: src/run_entry.c src/native_host.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-dos-%.o: $(AROS_DOSPAT_DIR)/%.c compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Dir.o: $(AROS_DIR_SRC) compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Fault.o: $(AROS_FAULT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Ask.o: $(AROS_ASK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

# The conditionals need dos_commanderrors.h, which lives beside them and
# holds nothing but the two error codes they report.
$(BUILD)/If.o: $(AROS_IF_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/Else.o: $(AROS_ELSE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/EndIf.o: $(AROS_ENDIF_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/EndSkip.o: $(AROS_ENDSKIP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/Lab.o: $(AROS_LAB_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/Quit.o: $(ACE_QUIT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/Skip.o: $(AROS_SKIP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) $(AROS_SHCOMMAND_INCLUDES) -c $< -o $@

$(BUILD)/Get.o: $(AROS_GET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Getenv.o: $(AROS_GETENV_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Setenv.o: $(AROS_SETENV_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Unsetenv.o: $(AROS_UNSETENV_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Set.o: $(AROS_SET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Unset.o: $(AROS_UNSET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Alias.o: $(AROS_ALIAS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Unalias.o: $(AROS_UNALIAS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/FailAt.o: $(AROS_FAILAT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Why.o: $(AROS_WHY_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/Prompt.o: $(AROS_PROMPT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@

$(BUILD)/MakeDir: $(BUILD)/makedir.o $(BUILD)/native_command_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/MakeLink: $(BUILD)/MakeLink.o $(BUILD)/native_command_entry.o \
                   $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
                   $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
                   $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Join: $(BUILD)/Join.o $(BUILD)/native_command_entry.o \
              $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
              $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
              $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Eval: $(BUILD)/Eval.o $(BUILD)/native_command_entry.o \
              $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
              $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
              $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Edit: $(BUILD)/Edit.o $(BUILD)/native_command_entry.o \
              $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
              $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
              $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Copy: $(BUILD)/Copy.o $(BUILD)/copy_entry.o $(AROS_DOSPAT_OBJS) \
	             $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	             $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/List: $(BUILD)/List.o $(BUILD)/native_command_entry.o $(AROS_DOSPAT_OBJS) \
	             $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	             $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Sort: $(BUILD)/Sort.o $(BUILD)/native_command_entry.o \
	             $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	             $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Search: $(BUILD)/Search.o $(BUILD)/native_command_entry.o $(AROS_DOSPAT_OBJS) \
	             $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	             $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Touch: $(BUILD)/Touch.o $(BUILD)/native_command_entry.o \
	              $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	              $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	              $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Wait: $(BUILD)/Wait.o $(BUILD)/native_command_entry.o \
	             $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	             $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	             $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Status: $(BUILD)/status.o $(BUILD)/native_command_entry.o \
	               $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	               $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	               $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Break: $(BUILD)/break.o $(BUILD)/native_command_entry.o \
	              $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
	              $(BUILD)/native_command.o $(BUILD)/native_shcommand.o \
	              $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Beep: $(BUILD)/Beep.o $(BUILD)/beep_entry.o \
               $(BUILD)/wayward_beep.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/EndCLI: $(BUILD)/endcli.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/LNX: $(BUILD)/LNX.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/ed_tine.o: $(ACE_ED_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ED: $(BUILD)/ed_tine.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Echo: $(BUILD)/Echo.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/CD: $(BUILD)/CD.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Path: $(BUILD)/Path.o $(BUILD)/native_command_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Which: $(BUILD)/Which.o $(BUILD)/native_command_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/PathPart: $(BUILD)/PathPart.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Type: $(BUILD)/Type.o $(BUILD)/native_command_entry.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Rename: $(BUILD)/Rename.o $(BUILD)/native_command_entry.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Stack: $(BUILD)/Stack.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Run: $(BUILD)/run_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Dir: $(BUILD)/Dir.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Delete: $(BUILD)/Delete.o $(BUILD)/native_command_entry.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Protect: $(BUILD)/Protect.o $(BUILD)/native_command_entry.o $(BUILD)/aros-arsupport-isdosentrya.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Filenote: $(BUILD)/Filenote.o $(BUILD)/native_command_entry.o $(BUILD)/aros-arsupport-isdosentrya.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Fault: $(BUILD)/Fault.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Ask: $(BUILD)/Ask.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

# If and Else skip a block by reading the script themselves, through AROS's
# own ReadItem() and FindArg(), which the DOS runtime already carries.
$(BUILD)/If: $(BUILD)/If.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Else: $(BUILD)/Else.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/EndIf: $(BUILD)/EndIf.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/EndSkip: $(BUILD)/EndSkip.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Lab: $(BUILD)/Lab.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Quit: $(BUILD)/Quit.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Skip: $(BUILD)/Skip.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/execute_entry.o: src/execute_entry.c src/native_host.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/Execute: $(BUILD)/execute_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Get: $(BUILD)/Get.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Getenv: $(BUILD)/Getenv.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Setenv: $(BUILD)/Setenv.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Unsetenv: $(BUILD)/Unsetenv.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Set: $(BUILD)/Set.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Unset: $(BUILD)/Unset.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Alias: $(BUILD)/Alias.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Unalias: $(BUILD)/Unalias.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/FailAt: $(BUILD)/FailAt.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Why: $(BUILD)/Why.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/Prompt: $(BUILD)/Prompt.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/ace-broker: $(BUILD)/broker.o $(BUILD)/dos-devices.o \
                     $(BUILD)/clipboard-bridge.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) $(BLKID_LIBS) -o $@

$(BUILD)/ace-brokerctl: $(BUILD)/brokerctl.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@


$(BUILD)/ace-console: $(BUILD)/amiga_console.o $(BUILD)/console_channel.o $(BUILD)/console_spec.o $(BUILD)/ace-appmenu-wayland.o $(BUILD)/console_device_bridge.o $(BUILD)/aros-console-editor.o $(BUILD)/aros-console-editor-stubs.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o $(BUILD)/clipboard-device.o $(BUILD)/clipboard-bridge.o $(BUILD)/iffparse-clipboard.o $(BUILD)/ace-vim-runtime.o \
                      $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS) \
                      $(BUILD)/aros-graphics-runtime.o $(AROS_GRAPHICS_OBJS) $(AROS_ARSUPPORT_OBJS)
	$(CC) $(CFLAGS) -pthread $(filter-out %.h,$^) $(GTK_LIBS) $(GFX_LIBS) $(WAYLAND_LIBS) -o $@

$(BUILD)/console_spec.o: src/console_spec.c src/console_spec.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console-spec-test: tests/console_spec_test.c $(BUILD)/console_spec.o
	$(CC) $(CFLAGS) -Isrc $(filter-out %.h,$^) -o $@

# Vim remains an untouched external checkout. ACE supplies the Amiga backend
# build objects and runtime seams, while the target makes the exact source
# tree explicit and reproducible. The full DOS runtime is intentionally not a
# dependency: its cooked editor exports names that collide with Vim's editor.
$(BUILD)/vim: tools/build-vim-ace.sh \
              src/ace_vim_compat.c src/ace_vim_files.c \
              src/ace_vim_editor_stubs.c \
              src/ace_vim_pathdef.c src/native_dos.c \
              compat/vim/include/devices/conunit.h \
              $(BUILD)/ace-vim-runtime.o $(BUILD)/broker_client.o \
              $(BUILD)/native_process.o $(BUILD)/native_command.o \
              $(BUILD)/assign_compat.o \
              $(BUILD)/console_channel.o \
              $(BUILD)/aros-dos-getdeviceproc.o \
              $(BUILD)/aros-dos-freedeviceproc.o \
              $(AROS_DOSPAT_OBJS) | $(BUILD)
	@test -n "$(VIM_SRC)" || (echo "use: make vim VIM_SRC=/path/to/untouched/vim" >&2; exit 2)
	VIM_SRC="$(VIM_SRC)" ACE_ROOT="$(CURDIR)" ACE_BUILD="$(BUILD)" CC="$(CC)" "$<"

vim: $(BUILD)/vim

$(BUILD)/NewCLI: $(BUILD)/aros-newcli.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/native_process.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/ace-shell: $(BUILD)/ace-launcher.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

$(BUILD)/ace-user-shell: $(BUILD)/ace-user-shell.o $(BUILD)/aros-real-shell.o $(AROS_SHELL_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

clean:
	$(RM) -r $(BUILD)

# The launcher names the binary it was installed beside, rather than looking
# ace-shell up on PATH. PATH order is not the desktop session's to promise,
# and when a second install exists the icon silently runs whichever directory
# comes first -- which is how an install can look complete and still start
# yesterday's build.
# Written by the install recipe rather than as a file target of its own,
# because its content depends on BINDIR -- a variable, not a file. As a target
# make would consider it up to date whenever data/ace.desktop.in had not
# changed, and happily install a launcher pointing at the prefix from the
# previous install.
install: all tine
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(addprefix $(BUILD)/,$(INSTALL_BINS)) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 "$(TINE_DIR)/tine" $(DESTDIR)$(BINDIR)/tine
	$(INSTALL) -m 0755 broker-start broker-stop $(DESTDIR)$(BINDIR)
	# SYS: -- the boot volume, in the shape dos.library's boot assigns expect.
	# Only the drawers ACE actually fills are created: AddBootAssign() in
	# rom/dos/cliinit.c falls back to SYS: itself for a drawer that is not
	# there, so L:, LIBS:, DEVS: and FONTS: resolve to SYS: on a system with
	# nothing to put in them, which is what ACE is.
	$(INSTALL) -d $(DESTDIR)$(SYSDIR)/C $(DESTDIR)$(SYSDIR)/S \
	              $(DESTDIR)$(SYSDIR)/Prefs/Env-Archive
	for command in $(AMIGA_COMMANDS); do \
	    ln -sf $(BINDIR)/$$command $(DESTDIR)$(SYSDIR)/C/$$command; \
	done
	$(INSTALL) -m 0644 data/Startup-Sequence $(DESTDIR)$(SYSDIR)/S/Startup-Sequence
	$(INSTALL) -d $(DESTDIR)$(APPLICATIONSDIR) $(DESTDIR)$(ICONDIR)
	sed 's|@BINDIR@|$(BINDIR)|g' data/ace.desktop.in > $(BUILD)/ace.desktop
	$(INSTALL) -m 0644 $(BUILD)/ace.desktop $(DESTDIR)$(APPLICATIONSDIR)/ace.desktop
	$(INSTALL) -m 0644 assets/ace.png $(DESTDIR)$(ICONDIR)/ace.png
	@if [ -z "$(DESTDIR)" ]; then \
	    found=`command -v ace-shell 2>/dev/null || true`; \
	    if [ -n "$$found" ] && [ "$$found" != "$(BINDIR)/ace-shell" ]; then \
	        echo; \
	        echo "warning: installed $(BINDIR)/ace-shell, but PATH finds $$found first."; \
	        echo "         Typing ace-shell runs that older install, not this one."; \
	        echo "         Remove it, or put $(BINDIR) earlier on PATH."; \
		fi; \
	    if command -v gtk-update-icon-cache >/dev/null 2>&1; then \
	        gtk-update-icon-cache -f -t $(DATADIR)/icons/hicolor || true; \
	    fi; \
	    if command -v update-desktop-database >/dev/null 2>&1; then \
	        update-desktop-database $(APPLICATIONSDIR) || true; \
	    fi; \
	fi

# Vim is built from an external checkout and is therefore intentionally not
# part of the ordinary ACE install set. When it has been built, install it
# beside ace-user-shell so the AROS command loader accepts it as a companion.
install-vim: vim
	@test -f "$(BUILD)/runtime/defaults.vim" || \
		(echo "use: make vim VIM_SRC=/path/to/untouched/vim" >&2; exit 2)
	$(INSTALL) -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(BINDIR)/runtime
	$(INSTALL) -m 0755 $(BUILD)/vim $(DESTDIR)$(BINDIR)/vim
	cp -a $(BUILD)/runtime/. $(DESTDIR)$(BINDIR)/runtime/
	# In SYS:C like any other command, so typing "vim" finds it through C:.
	$(INSTALL) -d $(DESTDIR)$(SYSDIR)/C
	ln -sf $(BINDIR)/vim $(DESTDIR)$(SYSDIR)/C/vim

test-console-device: $(BUILD)/console-device-test
	$(BUILD)/console-device-test

test-console-channel: $(BUILD)/console-channel-test
	$(BUILD)/console-channel-test

test-console-spec: $(BUILD)/console-spec-test
	$(BUILD)/console-spec-test

# The clipboard half of this test writes an IFF stream and reads it back.
# Left to itself the clipboard device bridges to the host clipboard, so the
# read returns whatever the user last copied, wrapped as IFF, and the test
# fails on any machine with a desktop session and a non-empty clipboard.
# Isolate it: no host bridging, and a private spool directory.
test-aros-exec-runtime: $(BUILD)/aros-exec-runtime-test
	ACE_CLIPBOARD_DISABLE_HOST=1 \
	    ACE_CLIPBOARD_DIR="$$(mktemp -d)" $(BUILD)/aros-exec-runtime-test

test-iffparse-clipboard: $(BUILD)/iffparse-clipboard-test
	$(BUILD)/iffparse-clipboard-test

test-acepaste: $(BUILD)/acepaste
	set -eu; clip_dir=$$(mktemp -d); host_file=$$(mktemp); \
	trap 'rm -rf "$$clip_dir" "$$host_file"' EXIT; \
	printf 'from host' >"$$host_file"; \
	ACE_CLIPBOARD_DIR="$$clip_dir" ACE_CLIPBOARD_HOST_FILE="$$host_file" \
		$(BUILD)/acepaste --get >"$$clip_dir/get-before"; \
	test "$$(cat "$$clip_dir/get-before")" = 'from host'; \
	printf 'primary text' | ACE_CLIPBOARD_DIR="$$clip_dir" \
		ACE_CLIPBOARD_HOST_FILE="$$host_file" $(BUILD)/acepaste --set; \
	test "$$(cat "$$host_file")" = 'primary text'; \
	test -f "$$clip_dir/clip0"; \
	printf 'alternate text' | ACE_CLIPBOARD_DIR="$$clip_dir" \
		ACE_CLIPBOARD_HOST_FILE="$$host_file" $(BUILD)/acepaste --unit 7 --set; \
	test -f "$$clip_dir/clip7"; \
	ACE_CLIPBOARD_DIR="$$clip_dir" ACE_CLIPBOARD_HOST_FILE="$$host_file" \
		$(BUILD)/acepaste --unit 7 --get >"$$clip_dir/get-seven"; \
	test "$$(cat "$$clip_dir/get-seven")" = 'alternate text'; \
	count=0; for unit in $$(seq 0 255); do \
		test -f "$$clip_dir/clip$$unit" && count=$$((count + 1)); \
	done; test "$$count" -eq 2

test-clipboard-client: $(BUILD)/Clip $(BUILD)/Assign $(BUILD)/acepaste
	sh tests/clipboard_client_test.sh

test-aros-console-editor: $(BUILD)/aros-console-editor-test
	$(BUILD)/aros-console-editor-test

test-native-input: $(BUILD)/native-input-test
	$(BUILD)/native-input-test

test-native-console-handle: $(BUILD)/native-console-handle-test
	$(BUILD)/native-console-handle-test

test-exec-compat: $(BUILD)/exec-compat-test
	$(BUILD)/exec-compat-test

test-boopsi: $(BUILD)/boopsi-test
	$(BUILD)/boopsi-test

test-graphics: $(BUILD)/graphics-test
	$(BUILD)/graphics-test

test-console-device-bridge: $(BUILD)/console-device-bridge-test
	$(BUILD)/console-device-bridge-test

# The comment harness is a test binary rather than a command: it checks the
# metadata path independently of List's command-level formatting.
$(BUILD)/dos-comment-test: tests/dos_comment_test.c $(DOS_RUNTIME_OBJ) \
                          $(BUILD)/native_dos.o $(BUILD)/native_command.o \
                          $(BUILD)/native_shcommand.o \
                          $(BUILD)/broker_client.o | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc $(filter-out %.h,$^) -o $@

test-system-assigns: all
	sh tests/system_assigns_test.sh

test-filesystem-translation: all $(BUILD)/dos-comment-test
	sh tests/filesystem_translation_test.sh

test-lha: all
	sh tests/lha_test.sh

test-file-commands: all
	sh tests/file_commands_test.sh

test-relabel: all
	sh tests/relabel_test.sh

test-info: all
	sh tests/info_test.sh

test-edit: all
	sh tests/edit_test.sh

test-dir-sort: all
	sh tests/dir_sort_test.sh

test-tine: all tine
	sh tests/tine_test.sh
	python3 tests/tine_console_query_test.py
	python3 tests/tine_screen_trace_test.py

.PHONY: all clean install tine lha lha-fetch install-vim vim test-console-device test-console-channel test-console-spec test-console-device-bridge test-filesystem-translation test-lha test-file-commands test-relabel test-info test-edit test-dir-sort test-tine test-system-assigns test-aros-exec-runtime test-iffparse-clipboard test-acepaste test-clipboard-client test-aros-console-editor test-native-input test-native-console-handle test-exec-compat test-boopsi test-graphics
AROS_CLIP_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Clip.c
$(BUILD)/Clip.o: $(AROS_CLIP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-sign-compare -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@
$(BUILD)/Clip: $(BUILD)/Clip.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

AROS_CUT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Cut.c
$(BUILD)/Cut.o: $(AROS_CUT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) $(AROS_SHCOMMAND_CFLAGS) -c $< -o $@
$(BUILD)/Cut: $(BUILD)/Cut.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_shcommand.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $(filter-out %.h,$^) -o $@

# Generated by -MMD above, one per object.  -MP adds a phony target for every
# header so that deleting or renaming one does not wedge the build with a
# "No rule to make target" error against a stale dependency file.
-include $(wildcard $(BUILD)/*.d)
