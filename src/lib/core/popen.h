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

#include "iostream.h"
#include "trivia/util.h"
#include "fiber_cond.h"

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
 * An instance of popen object.
 */
struct popen_handle {
	/**
	 * Process ID.
	 *
	 * -1 when the process is known to be completed.
	 */
	pid_t			pid;
	/**
	 * A string representation of the executable and the
	 * arguments for logging purposes.
	 *
	 * It does not precisely follow shell escaping rules, so
	 * attempt to use it programmatically may lead to an
	 * unexpected result.
	 */
	char			*command;
	/**
	 * Last known process status: alive, signaled, exited
	 * and so on. See wait(2) for details. Zero means alive
	 * (non-completed) process.
	 *
	 * @sa wstatus_str().
	 */
	int			wstatus;
	/**
	 * libev's SIGCHLD watcher.
	 */
	ev_child		ev_sigchld;
	/**
	 * Anchor to link all popen handles into a list.
	 *
	 * The list is used to kill child processes at exit
	 * (except handles with the ..._KEEP_CHILD flag).
	 */
	struct rlist		list;
	/**
	 * Single-bit parameters: whether to do stdin/stdout/stderr
	 * redirections, whether to use shell, whether to call
	 * setsid() in the child and so on.
	 *
	 * @sa enum popen_flag_bits.
	 */
	unsigned int		flags;
	/**
	 * Parent's ends of piped stdin/stdout/stderr as iostream
	 * objects.
	 */
	struct iostream		ios[POPEN_FLAG_FD_STDEND_BIT];
	/**
	 * A condition variable that is triggered at the process
	 * completion or the handle deletion.
	 */
	struct fiber_cond	completion_cond;
};

/**
 * Options for popen creation
 */
struct popen_opts {
	char			**argv;
	size_t			nr_argv;
	char			**env;
	unsigned int		flags;
	/** File descriptors that should be left open in the child. */
	int *inherit_fds;
	/** Size of the inherit_fds array. */
	size_t nr_inherit_fds;
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

/**
 * Await process completion.
 *
 * The function yields until the process will complete, the handle
 * will be deleted or the timeout will be reached.
 *
 * Note: the process completion and the handle deletion situations
 * are not differentiated by this function. If a caller needs it,
 * it should track popen_delete() calls on its own.
 *
 * Returns 0 at the process completion or the handle deletion,
 * otherwise returns -1 and sets a diag.
 *
 * Possible errors:
 *
 * - TimedOut: @a timeout quota is exceeded.
 * - FiberIsCancelled: cancelled by an outside code.
 */
extern int
popen_wait_timeout(struct popen_handle *handle, ev_tstamp timeout);

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
