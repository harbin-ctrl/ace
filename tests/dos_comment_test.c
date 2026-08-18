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

int main(int argc, char **argv)
{
    if ((argc != 3 && argc != 4) ||
        (strcmp(argv[1], "examine") != 0 && strcmp(argv[1], "exnext") != 0 &&
         strcmp(argv[1], "exnext-types") != 0 && strcmp(argv[1], "readlink") != 0)) {
        fprintf(stderr, "usage: dos-comment-test examine|exnext|exnext-types NAME | readlink DIRECTORY NAME\n");
        return 2;
    }
    if (strcmp(argv[1], "readlink") == 0)
        return argc == 4 ? read_link_target(argv[2], argv[3]) : 2;
    if (argc != 3)
        return 2;
    if (strcmp(argv[1], "examine") == 0)
        return examine_one(argv[2]);
    return examine_children(argv[2], strcmp(argv[1], "exnext-types") == 0);
}
