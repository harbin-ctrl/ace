CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-pointer-sign -O2
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
BLKID_CFLAGS := $(shell pkg-config --cflags blkid)
BLKID_LIBS := $(shell pkg-config --libs blkid)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

COMPAT := $(CURDIR)/compat/include
BUILD := $(CURDIR)/build
AROS_ROOT ?= $(HOME)/aros
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
AROS_CON_HANDLER_SRC := $(AROS_ROOT)/rom/filesys/console_handler/con_handler.c
AROS_CON_SUPPORT_SRC := $(AROS_ROOT)/rom/filesys/console_handler/support.c
AROS_CON_COMPLETION_SRC := $(AROS_ROOT)/rom/filesys/console_handler/completion.c
AROS_SHELL_NAMES := buffer cliEcho cliLen cliNan cliPrompt cliVarNum \
                    convertArg convertBackTicks convertLine convertLineDot \
                    convertRedir convertVar interpreter readLine redirection
AROS_SHELL_OBJS := $(addprefix $(BUILD)/aros-shell-,$(addsuffix .o,$(AROS_SHELL_NAMES)))
AROS_REAL_INCLUDES := -I$(CURDIR)/compat/aros-real/include \
                      -I$(AROS_ROOT)/arch/all-pc/include \
                      -I$(AROS_ROOT)/arch/x86_64-all/include \
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
INSTALL_BINS := Echo CD PathPart Fault Ask Get Getenv Set Unset Alias Unalias \
                FailAt Why Prompt MakeDir EndCLI LNX ace-shell ace-user-shell ace-console NewCLI \
                ace-broker ace-brokerctl

all: $(BUILD)/Echo $(BUILD)/CD $(BUILD)/PathPart $(BUILD)/Fault $(BUILD)/Ask $(BUILD)/Get $(BUILD)/Getenv $(BUILD)/Set $(BUILD)/Unset $(BUILD)/Alias $(BUILD)/Unalias $(BUILD)/FailAt $(BUILD)/Why $(BUILD)/Prompt $(BUILD)/MakeDir $(BUILD)/EndCLI $(BUILD)/LNX $(BUILD)/ace-shell $(BUILD)/ace-user-shell $(BUILD)/ace-console $(BUILD)/NewCLI $(BUILD)/ace-broker $(BUILD)/ace-brokerctl $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o $(BUILD)/aros-con-handler.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o $(BUILD)/aros-console-editor.o

$(BUILD):
	mkdir -p $@

$(BUILD)/native_dos.o: src/native_dos.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_command.o: src/native_command.c | $(BUILD)
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

$(BUILD)/LNX.o: $(INSTALL_LNX_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/broker_client.o: src/broker_client.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/broker.o: src/broker.c | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/dos-devices.o: src/dos_devices.c | $(BUILD)
	$(CC) $(CFLAGS) $(BLKID_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/brokerctl.o: src/brokerctl.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_shell.o: src/amiga_shell.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_console.o: src/amiga_console.c src/console_terminal.h | $(BUILD)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console_device.o: src/console_device.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/console_terminal.o: src/console_terminal.c src/console_terminal.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c src/console_terminal.c -o $@

$(BUILD)/con_handler.o: src/con_handler.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/aros-con-handler.o: $(AROS_CON_HANDLER_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-con-support.o: $(AROS_CON_SUPPORT_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

$(BUILD)/aros-con-completion.o: $(AROS_CON_COMPLETION_SRC) compat/aros-real/include/ace_handler_types.h | $(BUILD)
	$(CC) $(CFLAGS) $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) -c $< -o $@

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

$(BUILD)/console-terminal-test: tests/console_terminal_test.c $(BUILD)/console_terminal.o
	$(CC) $(CFLAGS) -Isrc $^ -o $@

$(BUILD)/aros-exec-runtime-test: tests/aros_exec_runtime_test.c $(BUILD)/aros-exec-runtime.o
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_INCLUDES) $^ -o $@

$(BUILD)/aros-console-editor-test: tests/aros_console_editor_test.c $(BUILD)/aros-console-editor.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o
	$(CC) $(CFLAGS) -pthread -Isrc $(AROS_REAL_CFLAGS) $(AROS_REAL_INCLUDES) $^ -o $@

$(BUILD)/exec-compat-test: tests/exec_compat_test.c $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o
	$(CC) $(CFLAGS) -pthread -DAMIGA_EXEC_COMPAT_ENABLED -I$(COMPAT) -Isrc $^ -o $@

$(BUILD)/aros-newcli.o: $(AROS_NEWCLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/aros-shell-runtime.o: src/aros_shell_runtime.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/aros-real-shell.o: $(AROS_ROOT)/workbench/c/Shell/Shell.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/aros-shell-%.o: $(AROS_ROOT)/workbench/c/Shell/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I$(AROS_ROOT)/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/Echo.o: $(AROS_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/CD.o: $(AROS_CD_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/PathPart.o: $(AROS_PATHPART_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

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

$(BUILD)/MakeDir: $(BUILD)/makedir.o $(BUILD)/makedir_entry.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/EndCLI: $(BUILD)/endcli.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/LNX: $(BUILD)/LNX.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Echo: $(BUILD)/Echo.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/CD: $(BUILD)/CD.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/PathPart: $(BUILD)/PathPart.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Fault: $(BUILD)/Fault.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Ask: $(BUILD)/Ask.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Get: $(BUILD)/Get.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Getenv: $(BUILD)/Getenv.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Set: $(BUILD)/Set.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unset: $(BUILD)/Unset.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Alias: $(BUILD)/Alias.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unalias: $(BUILD)/Unalias.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/FailAt: $(BUILD)/FailAt.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Why: $(BUILD)/Why.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Prompt: $(BUILD)/Prompt.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-broker: $(BUILD)/broker.o $(BUILD)/dos-devices.o
	$(CC) $(CFLAGS) $^ $(BLKID_LIBS) -o $@

$(BUILD)/ace-brokerctl: $(BUILD)/brokerctl.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@


$(BUILD)/ace-console: $(BUILD)/amiga_console.o $(BUILD)/console_terminal.o $(BUILD)/aros-console-editor.o $(BUILD)/aros-con-support.o $(BUILD)/aros-exec-runtime.o
	$(CC) $(CFLAGS) -pthread $^ $(GTK_LIBS) -o $@

$(BUILD)/NewCLI: $(BUILD)/aros-newcli.o $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/native_args.o $(BUILD)/native_process.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-shell: $(BUILD)/ace-launcher.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/ace-user-shell: $(BUILD)/aros-real-shell.o $(AROS_SHELL_OBJS) $(BUILD)/native_dos.o $(BUILD)/native_command.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

clean:
	$(RM) -r $(BUILD)

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(addprefix $(BUILD)/,$(INSTALL_BINS)) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 broker-start broker-stop $(DESTDIR)$(BINDIR)

test-console-device: $(BUILD)/console-device-test $(BUILD)/console-terminal-test
	$(BUILD)/console-device-test
	$(BUILD)/console-terminal-test

test-aros-exec-runtime: $(BUILD)/aros-exec-runtime-test
	$(BUILD)/aros-exec-runtime-test

test-aros-console-editor: $(BUILD)/aros-console-editor-test
	$(BUILD)/aros-console-editor-test

test-exec-compat: $(BUILD)/exec-compat-test
	$(BUILD)/exec-compat-test

.PHONY: all clean install test-console-device test-aros-exec-runtime test-aros-console-editor test-exec-compat
