/* ACE's host-side configuration for the Amiga/AROS LHa source. */

#define ADDITIONAL_SUFFIXES "lha,"
#define DEFAULT_LZHUFF_METHOD LZHUFF5_METHOD_NUM
#define GETTIMEOFDAY_HAS_2ND_ARG 1

#define HAVE_BASENAME 1
#define HAVE_DECL_BASENAME 1
#define HAVE_DIRENT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FNMATCH 1
#define HAVE_FNMATCH_H 1
#define HAVE_FSEEKO 1
#define HAVE_FTELLO 1
#define HAVE_FTIME 1
#define HAVE_FTRUNCATE 1
#define HAVE_ICONV 1
#define HAVE_ICONV_H 1
#define HAVE_GETGRGID 1
#define HAVE_GETGRNAM 1
#define HAVE_GETPWNAM 1
#define HAVE_GETPWUID 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GID_T 1
#define HAVE_GRP_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LCHOWN 1
#define HAVE_LIMITS_H 1
#define HAVE_LONG_LONG 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_MKSTEMP 1
#define HAVE_MKTIME 1
#define HAVE_PWD_H 1
#define HAVE_SSIZE_T 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCASECMP 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_STAT_ST_INO 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1
#define HAVE_STRUCT_TM_TM_ZONE 1
#define HAVE_SYS_FILE_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIMELOCAL 1
#define HAVE_TM_ZONE 1
#define HAVE_TZSET 1
#define HAVE_UID_T 1
#define HAVE_UINT64_T 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME 1
#define HAVE_UTIMES 1
#define HAVE_UTIME_H 1
#define HAVE_UTIME_NULL 1
#define HAVE_VSNPRINTF 1
#define HAVE_WCHAR_H 1

#define LHA_CONFIGURE_OPTIONS "ACE Amiga/AROS target"
#define MULTIBYTE_FILENAME CODE_UTF8
#define NEED_INCREMENTAL_INDICATOR 1

#define PACKAGE "lha"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "LHa for AROS in ACE"
#define PACKAGE_STRING "LHa for AROS in ACE 1.14i.1"
#define PACKAGE_TARNAME "lha"
#define PACKAGE_URL "https://github.com/sodero/lha"
#define PACKAGE_VERSION "1.14i.1"
#define PLATFORM "ACE host runtime"
#define RETSIGTYPE void
#define SIZEOF_LONG 8
#define SIZEOF_OFF_T 8
#define STDC_HEADERS 1
#define STRCHR_8BIT_CLEAN 1
#define TIME_WITH_SYS_TIME 1
#define USE_ICONV 1
#define VERSION "1.14i.1"
