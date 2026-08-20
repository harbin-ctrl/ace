/* Reads a file comment back out of the DOS seam.
 *
 * Filenote only writes. List is the AmigaDOS command that displays a comment,
 * but this harness also checks the DOS seam directly so the metadata path is
 * tested independently of command formatting.
 *
 * Both paths that fill a FileInfoBlock are driven, because they reach
 * native_fill_fib() from opposite ends: Examine() on a lock over the object
 * itself, and ExNext() over a directory's children.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <dos/dos.h>
#include <dos/dosextens.h>

static int examine_one(const char *name)
{
    struct FileInfoBlock fib;
    BPTR lock = Lock((CONST_STRPTR)name, SHARED_LOCK);

    if (!lock) {
        fprintf(stderr, "dos-comment-test: cannot lock %s (error %ld)\n",
                name, (long)IoErr());
        return 1;
    }
    if (!Examine(lock, &fib)) {
        fprintf(stderr, "dos-comment-test: cannot examine %s (error %ld)\n",
                name, (long)IoErr());
        UnLock(lock);
        return 1;
    }
    printf("%s\t%s\n", (const char *)fib.fib_FileName,
           (const char *)fib.fib_Comment);
    UnLock(lock);
    return 0;
}

static int examine_children(const char *name, int types)
{
    struct FileInfoBlock fib;
    BPTR lock = Lock((CONST_STRPTR)name, SHARED_LOCK);

    if (!lock) {
        fprintf(stderr, "dos-comment-test: cannot lock %s (error %ld)\n",
                name, (long)IoErr());
        return 1;
    }
    if (!Examine(lock, &fib)) {
        fprintf(stderr, "dos-comment-test: cannot examine %s (error %ld)\n",
                name, (long)IoErr());
        UnLock(lock);
        return 1;
    }
    while (ExNext(lock, &fib)) {
        if (types)
            printf("%s\t%ld\n", (const char *)fib.fib_FileName,
                   (long)fib.fib_DirEntryType);
        else
            printf("%s\t%s\n", (const char *)fib.fib_FileName,
                   (const char *)fib.fib_Comment);
    }
    UnLock(lock);
    return 0;
}

static int read_link_target(const char *directory, const char *name)
{
    char target[512];
    BPTR lock = Lock((CONST_STRPTR)directory, SHARED_LOCK);
    LONG length;

    if (!lock) {
        fprintf(stderr, "dos-comment-test: cannot lock %s (error %ld)\n",
                directory, (long)IoErr());
        return 1;
    }
    length = ReadLink(NULL, lock, (CONST_STRPTR)name, target, sizeof(target));
    UnLock(lock);
    if (length < 0) {
        fprintf(stderr, "dos-comment-test: cannot read link %s/%s (error %ld)\n",
                directory, name, (long)IoErr());
        return 1;
    }
    printf("%s\n", target);
    return 0;
}

static int break_exnext(const char *name)
{
    struct FileInfoBlock fib;
    struct timespec wait = { 0, 100000000L };
    BPTR lock = Lock((CONST_STRPTR)name, SHARED_LOCK);

    if (!lock || !Examine(lock, &fib)) {
        fprintf(stderr, "dos-comment-test: cannot open %s (error %ld)\n",
                name, (long)IoErr());
        if (lock)
            UnLock(lock);
        return 1;
    }

    /* Use the same host signal path as the console.  The short wait lets the
       runtime's signal handler publish the pending Ctrl-C before ExNext(). */
    if (raise(SIGUSR1) != 0 || nanosleep(&wait, NULL) != 0) {
        fprintf(stderr, "dos-comment-test: cannot queue Ctrl-C\n");
        UnLock(lock);
        return 1;
    }
    if (ExNext(lock, &fib) || IoErr() != ERROR_BREAK) {
        fprintf(stderr, "dos-comment-test: ExNext did not stop on Ctrl-C (error %ld)\n",
                (long)IoErr());
        UnLock(lock);
        return 1;
    }
    UnLock(lock);
    puts("ExNext Ctrl-C test passed");
    return 0;
}

int main(int argc, char **argv)
{
    if ((argc != 3 && argc != 4) ||
        (strcmp(argv[1], "examine") != 0 && strcmp(argv[1], "exnext") != 0 &&
         strcmp(argv[1], "exnext-types") != 0 && strcmp(argv[1], "readlink") != 0 &&
         strcmp(argv[1], "break-exnext") != 0)) {
        fprintf(stderr, "usage: dos-comment-test examine|exnext|exnext-types|break-exnext NAME | readlink DIRECTORY NAME\n");
        return 2;
    }
    if (strcmp(argv[1], "readlink") == 0)
        return argc == 4 ? read_link_target(argv[2], argv[3]) : 2;
    if (argc != 3)
        return 2;
    if (strcmp(argv[1], "break-exnext") == 0)
        return break_exnext(argv[2]);
    if (strcmp(argv[1], "examine") == 0)
        return examine_one(argv[2]);
    return examine_children(argv[2], strcmp(argv[1], "exnext-types") == 0);
}
