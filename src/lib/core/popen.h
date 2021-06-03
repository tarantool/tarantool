#ifndef TARANTOOL_LIB_CORE_POPEN_H_INCLUDED
#define TARANTOOL_LIB_CORE_POPEN_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>

#include <small/rlist.h>

#include "trivia/util.h"
#include <tarantool_ev.h>

/**
 * Describes popen object creation. This is API with Lua.
 */
enum popen_flag_bits {
	POPEN_FLAG_NONE			= (0 << 0),

	/*
	 * Which fd we should handle as new pipes.
	 */
	POPEN_FLAG_FD_STDIN_BIT		= 0,
	POPEN_FLAG_FD_STDIN		= (1 << POPEN_FLAG_FD_STDIN_BIT),

	POPEN_FLAG_FD_STDOUT_BIT	= 1,
	POPEN_FLAG_FD_STDOUT		= (1 << POPEN_FLAG_FD_STDOUT_BIT),

	POPEN_FLAG_FD_STDERR_BIT	= 2,
	POPEN_FLAG_FD_STDERR		= (1 << POPEN_FLAG_FD_STDERR_BIT),

	/*
	 * Number of bits occupied for stdX descriptors.
	 */
	POPEN_FLAG_FD_STDEND_BIT	= POPEN_FLAG_FD_STDERR_BIT + 1,

	/*
	 * Instead of inheriting fds from a parent
	 * rather use /dev/null.
	 */
	POPEN_FLAG_FD_STDIN_DEVNULL_BIT	= 3,
	POPEN_FLAG_FD_STDIN_DEVNULL	= (1 << POPEN_FLAG_FD_STDIN_DEVNULL_BIT),
	POPEN_FLAG_FD_STDOUT_DEVNULL_BIT= 4,
	POPEN_FLAG_FD_STDOUT_DEVNULL	= (1 << POPEN_FLAG_FD_STDOUT_DEVNULL_BIT),
	POPEN_FLAG_FD_STDERR_DEVNULL_BIT= 5,
	POPEN_FLAG_FD_STDERR_DEVNULL	= (1 << POPEN_FLAG_FD_STDERR_DEVNULL_BIT),

	/*
	 * Instead of inheriting fds from a parent
	 * close fds completely.
	 */
	POPEN_FLAG_FD_STDIN_CLOSE_BIT	= 6,
	POPEN_FLAG_FD_STDIN_CLOSE	= (1 << POPEN_FLAG_FD_STDIN_CLOSE_BIT),
	POPEN_FLAG_FD_STDOUT_CLOSE_BIT	= 7,
	POPEN_FLAG_FD_STDOUT_CLOSE	= (1 << POPEN_FLAG_FD_STDOUT_CLOSE_BIT),
	POPEN_FLAG_FD_STDERR_CLOSE_BIT	= 8,
	POPEN_FLAG_FD_STDERR_CLOSE	= (1 << POPEN_FLAG_FD_STDERR_CLOSE_BIT),

	/*
	 * Reserved for a case where we will handle
	 * other fds as a source for stdin/out/err.
	 * Ie piping from child process to our side
	 * via splices and etc.
	 */
	POPEN_FLAG_FD_STDIN_EPIPE_BIT	= 9,
	POPEN_FLAG_FD_STDIN_EPIPE	= (1 << POPEN_FLAG_FD_STDIN_EPIPE_BIT),
	POPEN_FLAG_FD_STDOUT_EPIPE_BIT	= 10,
	POPEN_FLAG_FD_STDOUT_EPIPE	= (1 << POPEN_FLAG_FD_STDOUT_EPIPE_BIT),
	POPEN_FLAG_FD_STDERR_EPIPE_BIT	= 11,
	POPEN_FLAG_FD_STDERR_EPIPE	= (1 << POPEN_FLAG_FD_STDERR_EPIPE_BIT),

	/*
	 * Call exec directly or via shell.
	 */
	POPEN_FLAG_SHELL_BIT		= 12,
	POPEN_FLAG_SHELL		= (1 << POPEN_FLAG_SHELL_BIT),

	/*
	 * Create a new session.
	 */
	POPEN_FLAG_SETSID_BIT		= 13,
	POPEN_FLAG_SETSID		= (1 << POPEN_FLAG_SETSID_BIT),

	/*
	 * Close all inherited fds except stdin/out/err.
	 */
	POPEN_FLAG_CLOSE_FDS_BIT	= 14,
	POPEN_FLAG_CLOSE_FDS		= (1 << POPEN_FLAG_CLOSE_FDS_BIT),

	/*
	 * Restore signal handlers to default.
	 */
	POPEN_FLAG_RESTORE_SIGNALS_BIT	= 15,
	POPEN_FLAG_RESTORE_SIGNALS	= (1 << POPEN_FLAG_RESTORE_SIGNALS_BIT),

	/*
	 * Send signal to a process group.
	 *
	 * @see popen_send_signal() for details.
	 */
	POPEN_FLAG_GROUP_SIGNAL_BIT	= 16,
	POPEN_FLAG_GROUP_SIGNAL		= (1 << POPEN_FLAG_GROUP_SIGNAL_BIT),

	/*
	 * Keep child running on delete.
	 */
	POPEN_FLAG_KEEP_CHILD_BIT	= 17,
	POPEN_FLAG_KEEP_CHILD		= (1 << POPEN_FLAG_KEEP_CHILD_BIT),
};

/**
 * Popen object states. This is API with Lua.
 */
enum popen_states {
	POPEN_STATE_NONE		= 0,
	POPEN_STATE_ALIVE		= 1,
	POPEN_STATE_EXITED		= 2,
	POPEN_STATE_SIGNALED		= 3,

	POPEN_STATE_MAX,
};

/**
 * An instance of popen object
 */
struct popen_handle {
	pid_t			pid;
	char			*command;
	int			wstatus;
	ev_child		ev_sigchld;
	struct rlist		list;
	unsigned int		flags;
	struct ev_io		ios[POPEN_FLAG_FD_STDEND_BIT];
};

/**
 * Options for popen creation
 */
struct popen_opts {
	char			**argv;
	size_t			nr_argv;
	char			**env;
	unsigned int		flags;
};

/**
 * Popen object statistics
 *
 * This is a short version of struct popen_handle which should
 * be used by external code and which should be changed/extended
 * with extreme caution since it is used in Lua code. Consider it
 * as API for external modules.
 */
struct popen_stat {
	pid_t			pid;
	unsigned int		flags;
	int			fds[POPEN_FLAG_FD_STDEND_BIT];
};

extern void
popen_stat(struct popen_handle *handle, struct popen_stat *st);

extern const char *
popen_command(struct popen_handle *handle);

extern ssize_t
popen_write_timeout(struct popen_handle *handle, const void *buf,
		    size_t count, unsigned int flags,
		    ev_tstamp timeout);

extern ssize_t
popen_read_timeout(struct popen_handle *handle, void *buf,
		   size_t count, unsigned int flags,
		   ev_tstamp timeout);

extern int
popen_shutdown(struct popen_handle *handle, unsigned int flags);

extern void
popen_state(struct popen_handle *handle, int *state, int *exit_code);

extern const char *
popen_state_str(unsigned int state);

extern int
popen_send_signal(struct popen_handle *handle, int signo);

extern int
popen_delete(struct popen_handle *handle);

extern struct popen_handle *
popen_new(struct popen_opts *opts);

extern void
popen_init(void);

extern void
popen_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_LIB_CORE_POPEN_H_INCLUDED */
