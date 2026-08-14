/* The host end of an AROS shell command.

   On AROS a command is a process: CreateProc() enters it with the argument
   line the shell parsed, and ReadArgs() reads that line. On ACE a command is
   a Linux process entered at main() with an argv vector, so the line has to
   be put back together before AROS's parser can see it.

   ACE already has the channel. src/native_command.c sets
   ACE_COMMAND_ARGUMENTS to the command's tail before execv(), and
   native_load_input_prefix() in src/native_dos.c feeds that string to
   Input(), which is where AROS's readargs.c takes an unsourced RDArgs from.
   So the only missing case is a command started from a Linux shell rather
   than from the ACE shell, and the fix is to synthesise the same variable
   from argv -- without overwrite, so that when the ACE shell did start the
   command, the line the shell itself parsed stays authoritative, quoting
   and all. */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include <dos/dos.h>
#include <proto/exec.h>

void native_publish_result(int result_code);
struct ExecBase *native_exec_base_pointer(void);

/* An argument that survived the host shell's own quoting may still contain
   characters AmigaDOS reads as structure. Quote it back the way readitem.c
   will take it apart: double quotes around the word, and '*' as the escape
   for a literal quote or asterisk inside it. An empty argument has to be
   quoted too, or it would vanish entirely. */
static int argument_needs_quotes(const char *argument)
{
    if (!*argument)
        return 1;
    return strpbrk(argument, " \t\n\"*") != NULL;
}

static size_t quoted_length(const char *argument)
{
    size_t length = 2;

    for (; *argument; argument++)
        length += (*argument == '"' || *argument == '*') ? 2 : 1;
    return length;
}

static char *append_quoted(char *out, const char *argument)
{
    *out++ = '"';
    for (; *argument; argument++) {
        if (*argument == '"' || *argument == '*')
            *out++ = '*';
        *out++ = *argument;
    }
    *out++ = '"';
    return out;
}

/* Returns a malloc()ed argument line. A command invoked with no arguments
   gets an empty line rather than nothing at all: that is what AmigaDOS
   leaves in the input stream, and it is what makes ReadArgs() report a
   missing required argument instead of reading on into real input. */
static char *argument_line(int argc, char **argv)
{
    size_t length = 0;
    char *line;
    char *out;
    int index;

    if (argc < 2)
        return strdup("\n");
    for (index = 1; index < argc; index++)
        length += (argument_needs_quotes(argv[index]) ?
                   quoted_length(argv[index]) : strlen(argv[index])) + 1;
    line = malloc(length + 1);
    if (!line)
        return NULL;
    out = line;
    for (index = 1; index < argc; index++) {
        if (index > 1)
            *out++ = ' ';
        if (argument_needs_quotes(argv[index]))
            out = append_quoted(out, argv[index]);
        else {
            size_t size = strlen(argv[index]);

            memcpy(out, argv[index], size);
            out += size;
        }
    }
    *out++ = '\n';
    *out = '\0';
    return line;
}

/* Makes argv reachable by ReadArgs(). Deliberately does not overwrite: a
   command started by the ACE shell already has the shell's own line. */
void ace_command_arguments_from_argv(int argc, char **argv)
{
    char *line = argument_line(argc, argv);

    if (!line)
        return;
    (void)setenv("ACE_COMMAND_ARGUMENTS", line, 0);
    free(line);
}

const char *ace_command_argument_line(void)
{
    const char *line = getenv("ACE_COMMAND_ARGUMENTS");

    return line ? line : "";
}

/* Entry for a command whose arguments are declared with the AROS_SHn macros:
   the macro expansion owns ReadArgs(), so all that is left is the argument
   line, the call, and the session's result record. */
int ace_shcommand_start(int argc, char **argv,
                        SIPTR (*entry)(STRPTR, ULONG, struct ExecBase *))
{
    const char *line;
    int result;

    ace_command_arguments_from_argv(argc, argv);
    line = ace_command_argument_line();
    result = (int)entry((STRPTR)line, (ULONG)strlen(line),
                        native_exec_base_pointer());
    native_publish_result(result);
    return result;
}

/* Entry for a command that calls ReadArgs() itself and keeps its own main(),
   which the Makefile renames out of the way. */
int ace_command_start(int argc, char **argv, int (*entry)(void))
{
    int result;

    ace_command_arguments_from_argv(argc, argv);
    result = entry();
    native_publish_result(result);
    return result;
}
