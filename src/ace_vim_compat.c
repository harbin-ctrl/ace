#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vim.h"

#include "native_host.h"

/* Vim's Amiga backend asks its platform layer for direct system({list})
   output. Keep the implementation in ACE rather than adding a line to Vim:
   explicit host paths are exec'd directly, while bare names are restricted to
   ACE companion commands by native_command_path(). */
#if defined(FEAT_EVAL)
char_u *mch_get_cmd_output_direct(char **argv, char_u *infile,
                                  int flags UNUSED, int *ret_len)
{
    int output_pipe[2] = {-1, -1};
    int status = -1;
    pid_t child;
    garray_T output;

    ga_init2(&output, 1, 4096);
    if (!argv || !argv[0] || pipe(output_pipe) != 0)
        return NULL;
    child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return NULL;
    }
    if (child == 0) {
        char command_path[PATH_MAX];
        int input;

        close(output_pipe[0]);
        if (infile)
            input = open((char *)infile, O_RDONLY);
        else
            input = open("/dev/null", O_RDONLY);
        if (input >= 0) {
            (void)dup2(input, STDIN_FILENO);
            close(input);
        }
        (void)dup2(output_pipe[1], STDOUT_FILENO);
        (void)dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        if (strchr(argv[0], '/'))
            execv(argv[0], argv);
        else if (native_command_path(argv[0], command_path,
                                     sizeof(command_path)) == 0)
            execv(command_path, argv);
        _exit(127);
    }

    close(output_pipe[1]);
    for (;;) {
        char buffer[4096];
        ssize_t length = read(output_pipe[0], buffer, sizeof(buffer));

        if (length <= 0)
            break;
        ga_grow(&output, (int)length);
        mch_memmove((char *)output.ga_data + output.ga_len,
                    buffer, (size_t)length);
        output.ga_len += (int)length;
    }
    close(output_pipe[0]);
    (void)waitpid(child, &status, 0);
    if (WIFEXITED(status))
        status = WEXITSTATUS(status);
    else
        status = -1;
    set_vim_var_nr(VV_SHELL_ERROR, (long)status);

    if (output.ga_len != 0) {
        char_u *result = alloc((size_t)output.ga_len + 1);

        if (result) {
            mch_memmove(result, output.ga_data, (size_t)output.ga_len);
            if (ret_len)
                *ret_len = output.ga_len;
            else {
                int index;

                for (index = 0; index < output.ga_len; index++)
                    if (result[index] == NUL)
                        result[index] = 1;
                result[output.ga_len] = NUL;
            }
        }
        ga_clear(&output);
        return result;
    }
    if (ret_len)
        *ret_len = 0;
    ga_clear(&output);
    return NULL;
}
#endif
