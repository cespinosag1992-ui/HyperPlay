/*
 * Hand-written replacement for the waf-generated config.h of talloc,
 * for Android bionic (arm64-v8a, API level >= 31).  Only talloc.c (plus
 * android/replace_android.c) is compiled; lib/replace/replace.h reads
 * these macros to decide which libc functions need a rep_* fallback.
 * Every HAVE_* function listed here is provided by bionic at API 31.
 * The answers mirror Termux's libtalloc cross-answers (all checks "OK").
 * __STDC_WANT_LIB_EXT1__=1 is passed on the command line, as replace.h
 * requires it before any libc header.
 */
#ifndef HYPERPLAY_TALLOC_CONFIG_H
#define HYPERPLAY_TALLOC_CONFIG_H

#define TALLOC_BUILD_VERSION_MAJOR 2
#define TALLOC_BUILD_VERSION_MINOR 4
#define TALLOC_BUILD_VERSION_RELEASE 4

#define _GNU_SOURCE 1

/* Headers */
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_MALLOC_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_SYSMACROS_H 1
#define HAVE_LINUX_TYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_DLFCN_H 1
#define HAVE_SYS_AUXV_H 1

/* Types and language features */
#define HAVE_BOOL 1
#define HAVE_INTPTR_T 1
#define HAVE_UINTPTR_T 1
#define HAVE_PTRDIFF_T 1
#define HAVE_VOLATILE 1
#define HAVE_VA_COPY 1
#define HAVE___VA_COPY 1
#define HAVE_FUNCTION_MACRO 1
#define HAVE_func_MACRO 1
#define HAVE_COMPARISON_FN_T 1
#define HAVE_VISIBILITY_ATTR 1
#define HAVE_CONSTRUCTOR_ATTRIBUTE 1
#define HAVE___THREAD 1
#define HAVE_C99_VSNPRINTF 1

/* Declarations */
#define HAVE_DECL_EWOULDBLOCK 1
#define HAVE_ERRNO_DECL 1
#define HAVE_DECL_ENVIRON 1
#define HAVE_SETENV_DECL 1

/* Functions available in bionic (API 31) */
#define HAVE_GETAUXVAL 1
#define HAVE_STRERROR 1
#define HAVE_STRDUP 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMMEM 1
#define HAVE_MEMALIGN 1
#define HAVE_MKTIME 1
#define HAVE_TIMEGM 1
#define HAVE_UTIME 1
#define HAVE_UTIMES 1
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#define HAVE_STRNDUP 1
#define HAVE_STRNLEN 1
#define HAVE_SETENV 1
#define HAVE_UNSETENV 1
#define HAVE_SETEUID 1
#define HAVE_SETEGID 1
#define HAVE_CHOWN 1
#define HAVE_CHROOT 1
#define HAVE_LINK 1
#define HAVE_READLINK 1
#define HAVE_SYMLINK 1
#define HAVE_REALPATH 1
#define HAVE_LCHOWN 1
#define HAVE_SETLINEBUF 1
#define HAVE_STRCASESTR 1
#define HAVE_STRSEP 1
#define HAVE_STRTOK_R 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_FTRUNCATE 1
#define HAVE_INITGROUPS 1
#define HAVE_DLERROR 1
#define HAVE_DLOPEN 1
#define HAVE_DLSYM 1
#define HAVE_DLCLOSE 1
#define HAVE_SOCKETPAIR 1
#define HAVE_VDPRINTF 1
#define HAVE_DPRINTF 1
#define HAVE_VASPRINTF 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_ASPRINTF 1
#define HAVE_SYSLOG 1
#define HAVE_VSYSLOG 1
#define HAVE_WORKING_STRPTIME 1
#define HAVE_DUP2 1
#define HAVE_SECURE_MKSTEMP 1
#define HAVE_MKDTEMP 1
#define HAVE_PREAD 1
#define HAVE_PWRITE 1
#define HAVE_INET_NTOA 1
#define HAVE_INET_PTON 1
#define HAVE_INET_NTOP 1
#define HAVE_INET_ATON 1
#define HAVE_CONNECT 1
#define HAVE_GETHOSTBYNAME 1
#define HAVE_GETIFADDRS 1
#define HAVE_FREEIFADDRS 1
#define HAVE_STRERROR_R 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_FDATASYNC 1
#define HAVE_POLL 1
#define HAVE_USLEEP 1
#define HAVE_GETPROGNAME 1
/* Not in bionic API 31 (closefrom, get_current_dir_name, getpeereid,
 * setproctitle, memset_s, copy_file_range): left undefined, so
 * replace.h only declares rep_* prototypes, which talloc.c never calls. */

#endif /* HYPERPLAY_TALLOC_CONFIG_H */
