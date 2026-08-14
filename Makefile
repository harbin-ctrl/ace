CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-pointer-sign -O2
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
BLKID_CFLAGS := $(shell pkg-config --cflags blkid)
BLKID_LIBS := $(shell pkg-config --libs blkid)
GFX_CFLAGS := $(shell pkg-config --cflags cairo fontconfig)
GFX_LIBS := $(shell pkg-config --libs cairo fontconfig)
WAYLAND_CFLAGS := $(shell pkg-config --cflags wayland-client)
WAYLAND_LIBS := $(shell pkg-config --libs wayland-client)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPLICATIONSDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor/512x512/apps
INSTALL ?= install

COMPAT := $(CURDIR)/compat/include
BUILD := $(CURDIR)/build
AROS_ROOT ?= $(HOME)/aros
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
AROS_CD_SRC := $(AROS_ROOT)/workbench/c/shellcommands/CD.c
AROS_PATHPART_SRC := $(AROS_ROOT)/workbench/c/shellcommands/PathPart.c
AROS_FAULT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Fault.c
AROS_ASK_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Ask.c
AROS_GET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Get.c
AROS_GETENV_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Getenv.c
AROS_SET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Set.c
AROS_UNSET_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Unset.c
AROS_ALIAS_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Alias.c
AROS_UNALIAS_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Unalias.c
AROS_FAILAT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/FailAt.c
AROS_WHY_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Why.c
AROS_PROMPT_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Prompt.c
INSTALL_LNX_SRC := src/lnx.c
AROS_MAKEDIR_SRC := $(AROS_ROOT)/workbench/c/MakeDir.c
AROS_ENDCLI_SRC := $(AROS_ROOT)/workbench/c/shellcommands/EndCLI.c
AROS_NEWCLI_SRC := $(AROS_ROOT)/workbench/c/shellcommands/NewCLI.c
AROS_ASSIGN_SRC := $(AROS_ROOT)/workbench/c/Assign.c
AROS_TYPE_SRC := $(AROS_ROOT)/workbench/c/Type.c
AROS_RENAME_SRC := $(AROS_ROOT)/workbench/c/Rename.c
AROS_STACK_SRC := $(AROS_ROOT)/workbench/c/shellcommands/Stack.c
AROS_DOSPATH_DIR := $(AROS_ROOT)/rom/dos
AROS_DIR_SRC := $(AROS_ROOT)/workbench/c/Dir.c
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
INSTALL_BINS := Echo CD PathPart Dir Fault Ask Get Getenv Set Unset Alias Unalias \
                FailAt Why Prompt MakeDir EndCLI Assign Type Rename Stack Run LNX ace-shell ace-user-shell ace-console NewCLI \
                ace-broker ace-brokerctl

all: $(BUILD)/Echo $(BUILD)/CD $(BUILD)/PathPart $(BUILD)/Dir $(BUILD)/Fault $(BUILD)/Ask $(BUILD)/Get $(BUILD)/Getenv $(BUILD)/Set $(BUILD)/Unset $(BUILD)/Alias $(BUILD)/Unalias $(BUILD)/FailAt $(BUILD)/Why $(BUILD)/Prompt $(BUILD)/MakeDir $(BUILD)/EndCLI $(BUILD)/Assign $(BUILD)/Type $(BUILD)/Rename $(BUILD)/Stack $(BUILD)/Run $(BUILD)/LNX $(BUILD)/ace-shell $(BUILD)/ace-user-shell $(BUILD)/ace-console $(BUILD)/NewCLI $(BUILD)/ace-broker $(BUILD)/ace-brokerctl $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o $(BUILD)/aros-con-handler.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o $(BUILD)/aros-console-editor.o $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS)

$(BUILD):
	mkdir -p $@

$(BUILD)/native_dos.o: src/native_dos.c src/broker_protocol.h src/broker_client.h src/aros_dos_path.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_command.o: src/native_command.c src/broker_protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/ace-launcher.o: src/ace_launcher.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_args.o: src/native_args.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_process.o: src/native_process.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/makedir.o: $(AROS_MAKEDIR_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Dmain=ace_makedir_main -c $< -o $@

$(BUILD)/makedir_entry.o: src/makedir_entry.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/endcli.o: $(AROS_ENDCLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

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
                    $(BUILD)/aros-dos-strtolong.o | $(BUILD)
	$(CC) $(CFLAGS) -r $^ -o $@

$(BUILD)/Assign: $(BUILD)/Assign.o $(BUILD)/assign_entry.o \
                 $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o \
                 $(BUILD)/native_command.o $(BUILD)/native_args.o \
                 $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/LNX.o: $(INSTALL_LNX_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/broker_client.o: src/broker_client.c src/broker_protocol.h src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/broker.o: src/broker.c src/broker_protocol.h src/dos_devices.h | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/dos-devices.o: src/dos_devices.c src/dos_devices.h | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/brokerctl.o: src/brokerctl.c src/broker_protocol.h src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_shell.o: src/amiga_shell.c src/broker_protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_console.o: src/amiga_console.c src/console_device_bridge.h src/ace_appmenu_wayland.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GTK_CFLAGS) $(GFX_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/ace-appmenu-wayland.o: src/ace_appmenu_wayland.c src/ace_appmenu_wayland.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(GTK_CFLAGS) $(WAYLAND_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console_device_bridge.o: src/console_device_bridge.c src/console_device_bridge.h compat/aros-real/include/ace_graphics_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(GFX_CFLAGS) $(AROS_GRAPHICS_CFLAGS) $(AROS_GRAPHICS_INCLUDES) -Isrc -c $< -o $@

$(BUILD)/console_device.o: src/console_device.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/con_handler.o: src/con_handler.c | $(BUILD)
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
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(BUILD)/aros-console-%.o: $(AROS_GRAPHICS_DIR)/%.c compat/aros-real/include/ace_graphics_intern.h | $(BUILD)
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
	$(CC) $(CFLAGS) -pthread $^ $(GFX_LIBS) -o $@

$(BUILD)/console-device-bridge-test: $(BUILD)/console-device-bridge-test.o $(BUILD)/console_device_bridge.o \
                                     $(BUILD)/aros-graphics-runtime.o $(AROS_GRAPHICS_OBJS) \
                                     $(AROS_ARSUPPORT_OBJS) $(AROS_BOOPSI_OBJS) \
                                     $(BUILD)/aros-boopsi-runtime.o
	$(CC) $(CFLAGS) -pthread $^ $(GFX_LIBS) -o $@

$(BUILD)/aros-exec-runtime.o: src/aros_exec_runtime.c src/aros_exec_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-console-editor.o: src/aros_console_editor.c src/aros_console_editor.h $(AROS_CON_SUPPORT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/exec_compat.o: src/exec_compat.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/exec_compat_bindings.o: src/exec_compat_bindings.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/console-device-test: tests/console_device_test.c $(BUILD)/console_device.o $(BUILD)/con_handler.o
	$(CC) $(CFLAGS) -pthread -Isrc $^ -o $@

$(BUILD)/aros-exec-runtime-test: tests/aros_exec_runtime_test.c $(BUILD)/aros-exec-runtime.o
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_INCLUDES) $^ -o $@

$(BUILD)/aros-console-editor-test: tests/aros_console_editor_test.c $(BUILD)/aros-console-editor.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o \
                                   $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS) \
                                   $(BUILD)/aros-graphics-runtime.o $(AROS_ARSUPPORT_OBJS)
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) $^ $(GFX_LIBS) -o $@

$(BUILD)/exec-compat-test: tests/exec_compat_test.c $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o
	$(CC) $(CFLAGS) -pthread -DAMIGA_EXEC_COMPAT_ENABLED -I$(COMPAT) -Isrc $^ -o $@

$(BUILD)/aros-newcli.o: $(AROS_NEWCLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-shell-runtime.o: src/aros_shell_runtime.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/aros-real-shell.o: $(AROS_ROOT)/workbench/c/Shell/Shell.c | $(BUILD)
	$(CC) $(CFLAGS) -Dmain=ace_aros_shell_main -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/ace-user-shell.o: src/ace_user_shell.c src/broker_client.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/aros-shell-%.o: $(AROS_ROOT)/workbench/c/Shell/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/Echo.o: $(AROS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/CD.o: $(AROS_CD_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/PathPart.o: $(AROS_PATHPART_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Type.o: $(AROS_TYPE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -Wno-sign-compare -I$(COMPAT) -c $< -o $@

$(BUILD)/Rename.o: $(AROS_RENAME_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Stack.o: $(AROS_STACK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/run_entry.o: src/run_entry.c src/native_host.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-dos-%.o: $(AROS_DOSPAT_DIR)/%.c compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Dir.o: $(AROS_DIR_SRC) compat/include/ace_dos_intern.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_DOSPAT_CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Fault.o: $(AROS_FAULT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Ask.o: $(AROS_ASK_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Get.o: $(AROS_GET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Getenv.o: $(AROS_GETENV_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Set.o: $(AROS_SET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Unset.o: $(AROS_UNSET_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Alias.o: $(AROS_ALIAS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Unalias.o: $(AROS_UNALIAS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/FailAt.o: $(AROS_FAILAT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Why.o: $(AROS_WHY_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/Prompt.o: $(AROS_PROMPT_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/MakeDir: $(BUILD)/makedir.o $(BUILD)/makedir_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/EndCLI: $(BUILD)/endcli.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/LNX: $(BUILD)/LNX.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Echo: $(BUILD)/Echo.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/CD: $(BUILD)/CD.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/PathPart: $(BUILD)/PathPart.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Type: $(BUILD)/Type.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Rename: $(BUILD)/Rename.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Stack: $(BUILD)/Stack.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Run: $(BUILD)/run_entry.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Dir: $(BUILD)/Dir.o $(AROS_DOSPAT_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Fault: $(BUILD)/Fault.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Ask: $(BUILD)/Ask.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Get: $(BUILD)/Get.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Getenv: $(BUILD)/Getenv.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Set: $(BUILD)/Set.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unset: $(BUILD)/Unset.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Alias: $(BUILD)/Alias.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unalias: $(BUILD)/Unalias.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/FailAt: $(BUILD)/FailAt.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Why: $(BUILD)/Why.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Prompt: $(BUILD)/Prompt.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-broker: $(BUILD)/broker.o $(BUILD)/dos-devices.o
	$(CC) $(CFLAGS) $^ $(BLKID_LIBS) -o $@

$(BUILD)/ace-brokerctl: $(BUILD)/brokerctl.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@


$(BUILD)/ace-console: $(BUILD)/amiga_console.o $(BUILD)/ace-appmenu-wayland.o $(BUILD)/console_device_bridge.o $(BUILD)/aros-console-editor.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o \
                      $(BUILD)/aros-boopsi-runtime.o $(AROS_BOOPSI_OBJS) \
                      $(BUILD)/aros-graphics-runtime.o $(AROS_GRAPHICS_OBJS) $(AROS_ARSUPPORT_OBJS)
	$(CC) $(CFLAGS) -pthread $^ $(GTK_LIBS) $(GFX_LIBS) $(WAYLAND_LIBS) -o $@

$(BUILD)/NewCLI: $(BUILD)/aros-newcli.o $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/native_process.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-shell: $(BUILD)/ace-launcher.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-user-shell: $(BUILD)/ace-user-shell.o $(BUILD)/aros-real-shell.o $(AROS_SHELL_OBJS) $(DOS_RUNTIME_OBJ) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

clean:
	$(RM) -r $(BUILD)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(addprefix $(BUILD)/,$(INSTALL_BINS)) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 broker-start broker-stop $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(APPLICATIONSDIR) $(DESTDIR)$(ICONDIR)
	$(INSTALL) -m 0644 data/ace.desktop $(DESTDIR)$(APPLICATIONSDIR)/ace.desktop
	$(INSTALL) -m 0644 assets/ace.png $(DESTDIR)$(ICONDIR)/ace.png

test-console-device: $(BUILD)/console-device-test
	$(BUILD)/console-device-test

test-aros-exec-runtime: $(BUILD)/aros-exec-runtime-test
	$(BUILD)/aros-exec-runtime-test

test-aros-console-editor: $(BUILD)/aros-console-editor-test
	$(BUILD)/aros-console-editor-test

test-exec-compat: $(BUILD)/exec-compat-test
	$(BUILD)/exec-compat-test

test-boopsi: $(BUILD)/boopsi-test
	$(BUILD)/boopsi-test

test-graphics: $(BUILD)/graphics-test
	$(BUILD)/graphics-test

test-console-device-bridge: $(BUILD)/console-device-bridge-test
	$(BUILD)/console-device-bridge-test

test-filesystem-translation: all
	sh tests/filesystem_translation_test.sh

.PHONY: all clean install test-console-device test-console-device-bridge test-filesystem-translation test-aros-exec-runtime test-aros-console-editor test-exec-compat test-boopsi test-graphics
