#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "tine_console.h"
#include "structs.h"
#include "editor.h"
#include "util.h"

static EDITOR *editor;
static WINDOW *cmdwin;
static bool reversed = false;

static int
quit(const char *m, int rc)
{
    if (editor)
        closeeditor(editor);
    if (cmdwin)
        endwin();
    if (m)
        fputs(m, stderr);
    exit(rc);
}

static int
setupcommand(WINDOW *w, int i)
{
    (void)i;
    cmdwin = w;
    wbkgdset(w, reversed ? A_REVERSE : A_NORMAL);
    werase(w);
    return OK;
}

static void
initializescreen(int i)
{
    ripoffline(i, setupcommand);
    if (!initscr())
        quit("could not initialize screen", EXIT_FAILURE);
    raw();
    noecho();
    nonl();
    if (reversed)
      wbkgdset(stdscr, A_REVERSE);
    keypad(stdscr, TRUE);
    keypad(cmdwin, TRUE);
    intrflush(stdscr, FALSE);
}

static void
loadfile(const char *fn)
{
    wchar_t *s = stows(fn, strlen(fn));
    if (!s)
        quit("Out of memory\n", EXIT_FAILURE);
    ARG a = {.t = ARG_STRING, .s1 = s, .n1 = wcslen(s)};
    errno = 0;
    cmd_if(editor, &editor->docview, &a);
    int saved_errno = errno;
    if (saved_errno == ENOENT) {
        /* ED opens a missing file as a one-line blank document.  Keeping
         * that sentinel line matters even when the first operation is X:
         * a newly created file then contains its terminating blank line,
         * and I/A/U retain ED's line-relative behavior. */
        if (!insertline(editor->docview.b, 0))
            quit("Out of memory\n", EXIT_FAILURE);
        error(editor, "Creating new file");
    } else if (saved_errno == EILSEQ)
        error(editor, "Binary file");
    free(s);
}

static void
runfile(const char *fn)
{
    wchar_t *s = stows(fn, strlen(fn));
    if (!s)
        quit("out of memory\n", EXIT_FAILURE);
    ARG a = {.t = ARG_STRING, .s1 = s, .n1 = wcslen(s)};
    cmd_rf(editor, &editor->docview, &a);
    free(s);
}

static bool
tryrun(const char *p, const char *n)
{
   char fn[FILENAME_MAX + 1] = {0};
   snprintf(fn, FILENAME_MAX, "%s/%s", p, n);
   if (access(fn, R_OK) == 0){
      runfile(fn);
      return true;
   }
   return false;
}

static const char *
getdef(const char *env, const char *def)
{
   if (getenv(env))
      return getenv(env);
   return def;
}

static bool
number(const char *s, size_t *value)
{
    char *end;
    unsigned long long n;

    if (!s || !*s || s[0] == '-')
        return false;
    errno = 0;
    n = strtoull(s, &end, 10);
    if (errno || *end || !n || (unsigned long long)(size_t)n != n)
        return false;
    *value = (size_t)n;
    return true;
}

static void
runstartup(const char *ext)
{
   char def[FILENAME_MAX + 1] = {0};
   char path[FILENAME_MAX + 1] = {0};
   char *s = NULL;

   snprintf(def, FILENAME_MAX, "%s/.config", getdef("HOME", "/"));
   snprintf(path, FILENAME_MAX, "tine/%s", ext);
   if (tryrun(getdef("XDG_CONFIG_HOME", def), path))
      return;
   if ((s = strdup(getdef("XDG_CONFIG_DIRS", "/etc/xdg")))){
      for (char *n = strtok(s, ":"); n; n = strtok(NULL, ":")){
         if (tryrun(n, path))
            break;
      }
      free(s);
      return;
   }
}

static void
runstartupfiles(const char *fn)
{
   char path[FILENAME_MAX + 1] = {0};
   snprintf(path, FILENAME_MAX, "tinerc.%s", strrchr(fn, '.')? strrchr(fn, '.') + 1 : fn);
   runstartup("tinerc");
   runstartup(path);
}

static void
fixloc(void)
{
   int y, x;
   getyx(editor->focusview->w, y, x);
   wmove(editor->focusview->w, y, x);
   wrefresh(editor->focusview->w);
}

static void
run(void)
{
    if (editor->focusview->statuscb)
        editor->focusview->statuscb(editor, editor->focusview);
    redisplay(editor->focusview);

    while (editor->running){
        fixloc();
        KEYSTROKE k = getkeystroke(editor, true);
        if (k.o == ERR)
            continue;
        VIEW *previous_focus = editor->focusview;
        dispatch(editor, editor->focusview, k);
        if (editor->focusview->statuscb)
            editor->focusview->statuscb(editor, editor->focusview);
        if (editor->focusview == &editor->cmdview &&
            previous_focus != editor->focusview)
            redisplay(&editor->docview);
        redisplay(editor->focusview);
    }
}

#define USAGE "usage: tine [-rtn] FILE [CMDFILE|+LINE]...\n"
int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    int o = 0, toporbot = -1;
    bool runrc = true;
    bool ed_compat = false;
    const char *with = NULL;
    size_t ed_size = 0, tabs = 0, width = 0, height = 0;
    while ((o = getopt(argc, argv, "rtnES:T:W:H:G:C:")) != -1){
       switch (o){
          case 'n':
            runrc = false;
            break;
          case 't':
            toporbot = 1;
            break;
          case 'r':
            reversed = true;
            break;

          case 'E':
            ed_compat = true;
            break;
          case 'S':
            if (!number(optarg, &ed_size))
                quit(USAGE, EXIT_FAILURE);
            break;
          case 'T':
            if (!number(optarg, &tabs))
                quit(USAGE, EXIT_FAILURE);
            break;
          case 'W':
            if (!number(optarg, &width))
                quit(USAGE, EXIT_FAILURE);
            break;
          case 'H':
            if (!number(optarg, &height))
                quit(USAGE, EXIT_FAILURE);
            break;
          case 'G':
            /* Amiga WINDOW specifications have no ACE terminal equivalent. */
            break;
          case 'C':
            if (with)
                quit(USAGE, EXIT_FAILURE);
            with = optarg;
            break;

          default:
            quit(USAGE, EXIT_FAILURE);
            break;
       }
    }
    argc -= optind;
    argv += optind;

    if (argc < 1)
        quit(USAGE, EXIT_FAILURE);

    tine_set_dimensions((int)height, (int)width);
    initializescreen(toporbot);
    if ((editor = openeditor(argv[0], stdscr, cmdwin)) == NULL)
        return fputs("out of memory", stderr), EXIT_FAILURE;
    editor->ed_compat = ed_compat;
    editor->ed_size = 0;
    if (tabs)
        editor->docview.ts = tabs;
    loadfile(argv[0]);
    if (ed_compat){
        if (!ed_size)
            ed_size = ED_DEFAULT_SIZE;
        if (editor->docview.b->bytes > ed_size)
            ed_size = editor->docview.b->bytes;
        editor->ed_size = ed_size;
    }
    if (runrc)
       runstartupfiles(argv[0]);
    enableundo(editor->docview.b);
    editor->docview.b->dirty = false;

    if (with)
        runfile(with);

    for (int i = 1; i < argc; i++){
        if (argv[i][0] == '+' && isdigit(argv[i][1])){
            ARG a = {.t = ARG_NUMBER, .n1 = (size_t)atol(argv[i] + 1)};
            cmd_m(editor, &editor->docview, &a);
        } else
            runfile(argv[i]);
    }

    run();

    return quit(NULL, EXIT_SUCCESS);
}
