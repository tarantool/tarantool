#ifndef LIBUTIL_FREEBSD_H_INCLUDED
#define LIBUTIL_FREEBSD_H_INCLUDED

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int
flopen(const char *path, int flags, ...);

struct pidfh;

struct pidfh *
pidfile_open(const char *path, mode_t mode, pid_t *pidptr);

int
pidfile_write(struct pidfh *pfh);

int
pidfile_close(struct pidfh *pfh);

int
pidfile_remove(struct pidfh *pfh);

int
pidfile_fileno(const struct pidfh *pfh);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* LIBUTIL_FREEBSD_H_INCLUDED */
