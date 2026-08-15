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

#include <stdio.h>
#include <string.h>

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

int main(int argc, char **argv)
{
    if (argc != 3 ||
        (strcmp(argv[1], "examine") != 0 && strcmp(argv[1], "exnext") != 0 &&
         strcmp(argv[1], "exnext-types") != 0)) {
        fprintf(stderr, "usage: dos-comment-test examine|exnext|exnext-types NAME\n");
        return 2;
    }
    if (strcmp(argv[1], "examine") == 0)
        return examine_one(argv[2]);
    return examine_children(argv[2], strcmp(argv[1], "exnext-types") == 0);
}
