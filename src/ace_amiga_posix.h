#ifndef ACE_AMIGA_POSIX_H
#define ACE_AMIGA_POSIX_H

/*
 * Path-taking C/POSIX calls for Amiga-derived programs running in ACE.
 *
 * The returned FILE and DIR objects are still the host libc's objects.  The
 * seam is only the pathname: AmigaDOS spelling is resolved through ACE before
 * the host call sees it.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utime.h>

FILE *ace_amiga_posix_fopen(const char *path, const char *mode);
int ace_amiga_posix_open(const char *path, int flags, ...);
int ace_amiga_posix_mkstemp(char *template_name);

int ace_amiga_posix_stat(const char *path, struct stat *information);
int ace_amiga_posix_lstat(const char *path, struct stat *information);
int ace_amiga_posix_access(const char *path, int mode);

int ace_amiga_posix_mkdir(const char *path, mode_t mode);
DIR *ace_amiga_posix_opendir(const char *path);

int ace_amiga_posix_rename(const char *old_path, const char *new_path);
int ace_amiga_posix_unlink(const char *path);
int ace_amiga_posix_remove(const char *path);
int ace_amiga_posix_rmdir(const char *path);
int ace_amiga_posix_chmod(const char *path, mode_t mode);
int ace_amiga_posix_utime(const char *path, const struct utimbuf *times);
int ace_amiga_posix_utimes(const char *path,
                           const struct timeval times[2]);
int ace_amiga_posix_symlink(const char *target, const char *link_path);
ssize_t ace_amiga_posix_readlink(const char *path, char *buffer,
                                 size_t buffer_size);

#endif
