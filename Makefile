CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-pointer-sign -O2
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

COMPAT := $(CURDIR)/compat/include
BUILD := $(CURDIR)/build
AROS_SRC := /home/erik/aros/workbench/c/shellcommands/Echo.c
AROS_CD_SRC := /home/erik/aros/workbench/c/shellcommands/CD.c
AROS_PATHPART_SRC := /home/erik/aros/workbench/c/shellcommands/PathPart.c
AROS_FAULT_SRC := /home/erik/aros/workbench/c/shellcommands/Fault.c
AROS_ASK_SRC := /home/erik/aros/workbench/c/shellcommands/Ask.c
AROS_GET_SRC := /home/erik/aros/workbench/c/shellcommands/Get.c
AROS_GETENV_SRC := /home/erik/aros/workbench/c/shellcommands/Getenv.c
AROS_SET_SRC := /home/erik/aros/workbench/c/shellcommands/Set.c
AROS_UNSET_SRC := /home/erik/aros/workbench/c/shellcommands/Unset.c
AROS_ALIAS_SRC := /home/erik/aros/workbench/c/shellcommands/Alias.c
AROS_UNALIAS_SRC := /home/erik/aros/workbench/c/shellcommands/Unalias.c
AROS_FAILAT_SRC := /home/erik/aros/workbench/c/shellcommands/FailAt.c
AROS_WHY_SRC := /home/erik/aros/workbench/c/shellcommands/Why.c
AROS_PROMPT_SRC := /home/erik/aros/workbench/c/shellcommands/Prompt.c
AROS_SHELL_NAMES := buffer cliEcho cliLen cliNan cliPrompt cliVarNum \
                    convertArg convertBackTicks convertLine convertLineDot \
                    convertRedir convertVar interpreter redirection
AROS_SHELL_OBJS := $(addprefix $(BUILD)/aros-shell-,$(addsuffix .o,$(AROS_SHELL_NAMES)))

all: $(BUILD)/Echo $(BUILD)/CD $(BUILD)/PathPart $(BUILD)/Fault $(BUILD)/Ask $(BUILD)/Get $(BUILD)/Getenv $(BUILD)/Set $(BUILD)/Unset $(BUILD)/Alias $(BUILD)/Unalias $(BUILD)/FailAt $(BUILD)/Why $(BUILD)/Prompt $(BUILD)/amiga-shell $(BUILD)/amiga-console $(BUILD)/NewCLI $(BUILD)/amiga-shell-broker $(BUILD)/brokerctl $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o $(BUILD)/aros-shell

$(BUILD):
	mkdir -p $@

$(BUILD)/native_dos.o: src/native_dos.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/native_args.o: src/native_args.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -c $< -o $@

$(BUILD)/broker_client.o: src/broker_client.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/broker.o: src/broker.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/brokerctl.o: src/brokerctl.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_shell.o: src/amiga_shell.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/amiga_console.o: src/amiga_console.c | $(BUILD)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -Isrc -c $< -o $@

$(BUILD)/console_device.o: src/console_device.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/con_handler.o: src/con_handler.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/exec_compat.o: src/exec_compat.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -Isrc -c $< -o $@

$(BUILD)/exec_compat_bindings.o: src/exec_compat_bindings.c | $(BUILD)
	$(CC) $(CFLAGS) -pthread -I$(COMPAT) -Isrc -c $< -o $@

$(BUILD)/console-device-test: tests/console_device_test.c $(BUILD)/console_device.o $(BUILD)/con_handler.o
	$(CC) $(CFLAGS) -pthread -Isrc $^ -o $@

$(BUILD)/exec-compat-test: tests/exec_compat_test.c $(BUILD)/exec_compat.o $(BUILD)/exec_compat_bindings.o
	$(CC) $(CFLAGS) -pthread -DAMIGA_EXEC_COMPAT_ENABLED -I$(COMPAT) -Isrc $^ -o $@

$(BUILD)/newcli.o: src/newcli.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/aros-shell-runtime.o: src/aros_shell_runtime.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I/home/erik/aros/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

$(BUILD)/aros-shell-%.o: /home/erik/aros/workbench/c/Shell/%.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(COMPAT) -Isrc -I/home/erik/aros/workbench/c/Shell -Wno-sign-compare -Wno-implicit-function-declaration -c $< -o $@

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

$(BUILD)/Echo: $(BUILD)/Echo.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/CD: $(BUILD)/CD.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/PathPart: $(BUILD)/PathPart.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Fault: $(BUILD)/Fault.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Ask: $(BUILD)/Ask.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Get: $(BUILD)/Get.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Getenv: $(BUILD)/Getenv.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Set: $(BUILD)/Set.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unset: $(BUILD)/Unset.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Alias: $(BUILD)/Alias.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Unalias: $(BUILD)/Unalias.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/FailAt: $(BUILD)/FailAt.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Why: $(BUILD)/Why.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/Prompt: $(BUILD)/Prompt.o $(BUILD)/native_dos.o $(BUILD)/native_args.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/amiga-shell-broker: $(BUILD)/broker.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/brokerctl: $(BUILD)/brokerctl.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/amiga-shell: $(BUILD)/amiga_shell.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/amiga-console: $(BUILD)/amiga_console.o $(BUILD)/console_device.o $(BUILD)/con_handler.o
	$(CC) $(CFLAGS) -pthread $^ $(GTK_LIBS) -o $@

$(BUILD)/NewCLI: $(BUILD)/newcli.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/aros-shell: $(BUILD)/aros-shell-runtime.o $(AROS_SHELL_OBJS) $(BUILD)/native_dos.o $(BUILD)/broker_client.o
	$(CC) $(CFLAGS) $^ -o $@

clean:
	$(RM) -r $(BUILD)

test-console-device: $(BUILD)/console-device-test
	$(BUILD)/console-device-test

test-exec-compat: $(BUILD)/exec-compat-test
	$(BUILD)/exec-compat-test

.PHONY: all clean test-console-device test-exec-compat
