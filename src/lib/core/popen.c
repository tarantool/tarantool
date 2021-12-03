#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <poll.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>

#include "popen.h"
#include "fiber.h"
#include "assoc.h"
#include "coio.h"
#include "say.h"

#ifdef TARGET_OS_DARWIN
# include <sys/ioctl.h>
#endif

/* A mapping to find popens by their pids in a signal handler */
static struct mh_i32ptr_t *popen_pids_map = NULL;

/* All popen handles to be able to cleanup them at exit */
static RLIST_HEAD(popen_head);

/*
 * A user may request to use /dev/null inside a child process
 * instead of stdin/out/err, for this sake we will open these
 * fds once on initialization and pass to children.
 */
static int dev_null_fd_ro = -1;
static int dev_null_fd_wr = -1;

static const struct {
	unsigned int	mask;
	unsigned int	mask_devnull;
	unsigned int	mask_close;
	int		fileno;
	int		*dev_null_fd;
	int		parent_idx;
	int		child_idx;
	bool		nonblock;
} pfd_map[POPEN_FLAG_FD_STDEND_BIT] = {
	{
		.mask		= POPEN_FLAG_FD_STDIN,
		.mask_devnull	= POPEN_FLAG_FD_STDIN_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDIN_CLOSE,
		.fileno		= STDIN_FILENO,
		.dev_null_fd	= &dev_null_fd_ro,
		.parent_idx	= 1,
		.child_idx	= 0,
	}, {
		.mask		= POPEN_FLAG_FD_STDOUT,
		.mask_devnull	= POPEN_FLAG_FD_STDOUT_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDOUT_CLOSE,
		.fileno		= STDOUT_FILENO,
		.dev_null_fd	= &dev_null_fd_wr,
		.parent_idx	= 0,
		.child_idx	= 1,
	}, {
		.mask		= POPEN_FLAG_FD_STDERR,
		.mask_devnull	= POPEN_FLAG_FD_STDERR_DEVNULL,
		.mask_close	= POPEN_FLAG_FD_STDERR_CLOSE,
		.fileno		= STDERR_FILENO,
		.dev_null_fd	= &dev_null_fd_wr,
		.parent_idx	= 0,
		.child_idx	= 1,
	},
};

/**
 * Register popen handle in a pids map.
 */
static void
popen_register(struct popen_handle *handle)
{
	struct mh_i32ptr_node_t node = {
		.key	= handle->pid,
		.val	= handle,
	};
	say_debug("popen: register %d", handle->pid);
	mh_i32ptr_put(popen_pids_map, &node, NULL, NULL);
}

/**
 * Find popen handler by its pid.
 */
static struct popen_handle *
popen_find(pid_t pid)
{
	mh_int_t k = mh_i32ptr_find(popen_pids_map, pid, NULL);
	if (k == mh_end(popen_pids_map))
		return NULL;
	return mh_i32ptr_node(popen_pids_map, k)->val;
}

/**
 * Remove popen handler from a pids map.
 */
static void
popen_unregister(struct popen_handle *handle)
{
	struct mh_i32ptr_node_t node = {
		.key	= handle->pid,
		.val	= NULL,
	};
	say_debug("popen: unregister %d", handle->pid);
	mh_i32ptr_remove(popen_pids_map, &node, NULL);
}

/**
 * Duplicate a file descriptor, but not to std{in,out,err}.
 *
 * Return a new fd at success, otherwise return -1 and set a diag.
 */
static int
dup_not_std_streams(int fd)
{
	int res = -1;
	int save_errno = 0;

	/*
	 * We will call dup() in a loop until it will return
	 * fd > STDERR_FILENO. The array `discarded_fds` stores
	 * intermediate fds to close them after all dup() calls.
	 */
	int discarded_fds[POPEN_FLAG_FD_STDEND_BIT] = {-1, -1, -1};

	for (size_t i = 0; i < lengthof(discarded_fds) + 1; ++i) {
		int new_fd = dup(fd);
		if (new_fd < 0) {
			save_errno = errno;
			break;
		}

		/* Found wanted fd. */
		if (new_fd > STDERR_FILENO) {
			res = new_fd;
			break;
		}

		/* Save to close then. */
		assert(i < lengthof(discarded_fds));
		discarded_fds[i] = new_fd;
	}

	/* Close all intermediate fds (if any). */
	for (size_t i = 0; i < lengthof(discarded_fds); ++i) {
		if (discarded_fds[i] >= 0)
			close(discarded_fds[i]);
	}

	/* Report an error if it occurs. */
	if (res < 0) {
		errno = save_errno;
		diag_set(SystemError, "Unable to duplicate an fd %d", fd);
		return -1;
	}

	return res;
}

/**
 * Allocate new popen hanldle with flags specified.
 */
static struct popen_handle *
handle_new(struct popen_opts *opts)
{
	struct popen_handle *handle;
	size_t size = 0, i;
	char *pos;

	assert(opts->argv != NULL && opts->nr_argv > 0);

	/*
	 * Killing group of signals allowed for a new
	 * session only where it makes sense, otherwise
	 * child gonna inherit group and we will be killing
	 * ourself.
	 */
	if (opts->flags & POPEN_FLAG_GROUP_SIGNAL &&
	    (opts->flags & POPEN_FLAG_SETSID) == 0) {
		diag_set(IllegalParams,
			 "popen: group signal without setting sid");
		return NULL;
	}

	for (i = 0; i < opts->nr_argv; i++) {
		if (opts->argv[i] == NULL)
			continue;
		size += strlen(opts->argv[i]) + 3;
	}

	handle = malloc(sizeof(*handle) + size);
	if (!handle) {
		diag_set(OutOfMemory, sizeof(*handle) + size,
			 "malloc", "popen_handle");
		return NULL;
	}

	pos = handle->command = (void *)handle + sizeof(*handle);
	for (i = 0; i < opts->nr_argv-1; i++) {
		if (opts->argv[i] == NULL)
			continue;
		bool is_multiword = strchr(opts->argv[i], ' ') != NULL;
		if (is_multiword)
			*pos++ = '\'';
		strcpy(pos, opts->argv[i]);
		pos += strlen(opts->argv[i]);
		if (is_multiword)
			*pos++ = '\'';
		*pos++ = ' ';
	}
	pos[-1] = '\0';

	handle->wstatus	= 0;
	handle->pid	= -1;
	handle->flags	= opts->flags;

	rlist_create(&handle->list);

	/*
	 * No need to initialize the whole ios structure,
	 * just set fd value to mark as unused.
	 */
	for (i = 0; i < lengthof(handle->ios); i++)
		handle->ios[i].fd = -1;

	say_debug("popen: alloc handle %p command '%s' flags %#x",
		  handle, handle->command, opts->flags);
	return handle;
}

/**
 * Free memory allocated for a handle. To pair with handle_new().
 */
static inline void
handle_free(struct popen_handle *handle)
{
	say_debug("popen: handle %p free %p", handle);
	free(handle);
}

/**
 * Generate an error about IO operation that is not supported by
 * a popen handle.
 */
static inline int
popen_set_unsupported_io_error(void)
{
	diag_set(IllegalParams, "popen: handle does not support the "
		 "requested IO operation");
	return -1;
}

/**
 * Test if the handle can run a requested IO operation.
 *
 * NB: Expects @a io_flags to be a ..._FD_STDx flag rather
 * then a mask with several flags: otherwise it'll check
 * that one (any) of @a io_flags is set.
 *
 * Returns 0 if so and -1 otherwise (and set a diag).
 */
static inline int
popen_may_io(struct popen_handle *handle, unsigned int idx,
	     unsigned int io_flags)
{
	assert(io_flags == POPEN_FLAG_FD_STDIN	||
	       io_flags == POPEN_FLAG_FD_STDOUT	||
	       io_flags == POPEN_FLAG_FD_STDERR);
	if (!(io_flags & handle->flags))
		return popen_set_unsupported_io_error();

	if (handle->ios[idx].fd < 0) {
		diag_set(IllegalParams, "popen: attempt to operate "
			 "on a closed file descriptor");
		return -1;
	}

	return 0;
}

/**
 * Test if the handle still have a living child process.
 *
 * Return -1 and set errno to ESRCH when a process does not
 * exist. Otherwise return 0.
 */
static inline int
popen_may_pidop(struct popen_handle *handle)
{
	assert(handle != NULL);
	if (handle->pid == -1) {
		errno = ESRCH;
		return -1;
	}
	return 0;
}

/**
 * Fill popen object statistics.
 */
void
popen_stat(struct popen_handle *handle, struct popen_stat *st)
{
	assert(handle != NULL);

	st->pid		= handle->pid;
	st->flags	= handle->flags;

	static_assert(lengthof(st->fds) == lengthof(handle->ios),
		      "Statistics fds are screwed");

	for (size_t i = 0; i < lengthof(handle->ios); i++)
		st->fds[i] = handle->ios[i].fd;
}

/**
 * Get a pointer to the former command line.
 */
const char *
popen_command(struct popen_handle *handle)
{
	assert(handle != NULL);
	return (const char *)handle->command;
}

/**
 * Get stdX descriptor string representation.
 */
static inline const char *
stdX_str(unsigned int index)
{
	static const char * stdX_names[] = {
		[STDIN_FILENO]	= "stdin",
		[STDOUT_FILENO]	= "stdout",
		[STDERR_FILENO]	= "stderr",
	};

	return index < lengthof(stdX_names) ?
		stdX_names[index] : "unknown";
}

/**
 * Write data to the child stdin.
 *
 * Yield until all @a count bytes will be written.
 *
 * Returns @a count at success, otherwise returns -1 and set a
 * diag.
 *
 * Possible errors:
 *
 * - IllegalParams: a parameter check fails:
 *   - count: data is too big.
 *   - flags: stdin is not set.
 *   - handle: handle does not support the requested IO operation.
 *   - handle: attempt to operate on a closed fd.
 * - SocketError: an IO error occurs at write().
 * - TimedOut: @a timeout quota is exceeded.
 * - FiberIsCancelled: cancelled by an outside code.
 *
 * An error may occur after a partial write. There is not way to
 * enquire amount of written bytes in the case.
 *
 * FIXME: Provide an info re amount written bytes in the case.
 *        Say, return -(written) in the case.
 */
ssize_t
popen_write_timeout(struct popen_handle *handle, const void *buf,
		    size_t count, unsigned int flags,
		    ev_tstamp timeout)
{
	assert(handle != NULL);

	if (count > (size_t)SSIZE_MAX) {
		diag_set(IllegalParams, "popen: data is too big");
		return -1;
	}

	if (!(flags & POPEN_FLAG_FD_STDIN)) {
		diag_set(IllegalParams, "popen: stdin is not set");
		return -1;
	}

	int idx = STDIN_FILENO;

	if (popen_may_io(handle, idx, flags) != 0)
		return -1;

	say_debug("popen: %d: write idx [%s:%d] buf %p count %zu "
		  "fds %d timeout %.9g",
		  handle->pid, stdX_str(idx), idx, buf, count,
		  handle->ios[idx].fd, timeout);

	ssize_t rc = coio_write_timeout_noxc(&handle->ios[idx], buf,
					     count, timeout);
	assert(rc < 0 || rc == (ssize_t)count);
	return rc;
}

/**
 * Read data from a child's peer with timeout.
 *
 * Yield until some data will be available for read.
 *
 * Returns amount of read bytes at success, otherwise returns -1
 * and set a diag.
 *
 * Zero return value means EOF.
 *
 * Note: Less then @a count bytes may be available for read at a
 * moment, so a return value less then @a count does not mean EOF.
 *
 * Possible errors:
 *
 * - IllegalParams: a parameter check fails:
 *   - count: buffer is too big.
 *   - flags: stdout and stdrr are both set or both missed.
 *   - handle: handle does not support the requested IO operation.
 *   - handle: attempt to operate on a closed fd.
 * - SocketError: an IO error occurs at read().
 * - TimedOut: @a timeout quota is exceeded.
 * - FiberIsCancelled: cancelled by an outside code.
 */
ssize_t
popen_read_timeout(struct popen_handle *handle, void *buf,
		   size_t count, unsigned int flags,
		   ev_tstamp timeout)
{
	assert(handle != NULL);

	if (count > (size_t)SSIZE_MAX) {
		diag_set(IllegalParams, "popen: buffer is too big");
		return -1;
	}

	if (!(flags & (POPEN_FLAG_FD_STDOUT | POPEN_FLAG_FD_STDERR))) {
		diag_set(IllegalParams,
			 "popen: neither stdout nor stderr is set");
		return -1;
	}

	if (flags & POPEN_FLAG_FD_STDOUT && flags & POPEN_FLAG_FD_STDERR) {
		diag_set(IllegalParams, "popen: reading from both stdout and "
			 "stderr at one call is not supported");
		return -1;
	}

	int idx = flags & POPEN_FLAG_FD_STDOUT ?
		STDOUT_FILENO : STDERR_FILENO;

	if (popen_may_io(handle, idx, flags) != 0)
		return -1;

	say_debug("popen: %d: read idx [%s:%d] buf %p count %zu "
		  "fds %d timeout %.9g",
		  handle->pid, stdX_str(idx), idx, buf, count,
		  handle->ios[idx].fd, timeout);

	return coio_read_ahead_timeout_noxc(&handle->ios[idx], buf, 1, count,
					    timeout);
}

/**
 * Close parent's ends of std* fds.
 *
 * The following @a flags controls which fds should be closed:
 *
 *  POPEN_FLAG_FD_STDIN   close parent's end of child's stdin
 *  POPEN_FLAG_FD_STDOUT  close parent's end of child's stdout
 *  POPEN_FLAG_FD_STDERR  close parent's end of child's stderr
 *
 * The main reason to use this function is to send EOF to
 * child's stdin. However parent's end of stdout / stderr
 * may be closed too.
 *
 * The function does not fail on already closed fds (idempotence).
 * However it fails on attempt to close the end of a pipe that was
 * never exist. In other words, a subset of ..._FD_STD{IN,OUT,ERR}
 * flags used at a handle creation may be used here.
 *
 * The function does not close any fds on a failure: either all
 * requested fds are closed or neither of them.
 *
 * Returns 0 at success, otherwise -1 and set a diag.
 *
 * Possible errors:
 *
 * - IllegalParams: a parameter check fails:
 *   - flags: neither stdin, stdout nor stderr is set.
 *   - handle: handle does not support the requested IO operation
 *             (one of fds is not piped).
 */
int
popen_shutdown(struct popen_handle *handle, unsigned int flags)
{
	assert(handle != NULL);

	/* Ignore irrelevant flags. */
	flags &= POPEN_FLAG_FD_STDIN	|
		 POPEN_FLAG_FD_STDOUT	|
		 POPEN_FLAG_FD_STDERR;

	/* At least one ..._FD_STDx flag should be set. */
	if (flags == 0) {
		diag_set(IllegalParams,
			 "popen: neither stdin, stdout nor stderr is set");
		return -1;
	}

	/*
	 * The handle should have all std*, which are asked to
	 * close, be piped.
	 *
	 * Otherwise the operation has no sense: we should close
	 * parent's end of a pipe, but it was not created at all.
	 */
	if ((handle->flags & flags) != flags)
		return popen_set_unsupported_io_error();

	for (size_t idx = 0; idx < lengthof(pfd_map); ++idx) {
		/* Operate only on asked fds. */
		unsigned int op_mask = pfd_map[idx].mask;
		if ((flags & op_mask) == 0)
			continue;

		/* Skip already closed fds. */
		if (handle->ios[idx].fd < 0)
			continue;

		say_debug("popen: %d: shutdown idx [%s:%d] fd %s",
			  handle->pid, stdX_str(idx), idx,
			  handle->ios[idx].fd);
		coio_close_io(loop(), &handle->ios[idx]);
	}

	return 0;
}

/**
 * Encode signal status into a human readable form.
 *
 * Operates on S_DEBUG level only simply because snprintf
 * is pretty heavy in performance, otherwise @buf remains
 * untouched.
 */
static char *
wstatus_str(char *buf, size_t size, int wstatus)
{
	static const char fmt[] =
		"wstatus %#x exited %d status %d "
		"signaled %d wtermsig %d "
		"stopped %d stopsig %d "
		"coredump %d continued %d";

	assert(size > 0);

	if (say_log_level_is_enabled(S_DEBUG)) {
		snprintf(buf, size, fmt, wstatus,
			 WIFEXITED(wstatus),
			 WIFEXITED(wstatus) ?
			 WEXITSTATUS(wstatus) : -1,
			 WIFSIGNALED(wstatus),
			 WIFSIGNALED(wstatus) ?
			 WTERMSIG(wstatus) : -1,
			 WIFSTOPPED(wstatus),
			 WIFSTOPPED(wstatus) ?
			 WSTOPSIG(wstatus) : -1,
			 WCOREDUMP(wstatus),
			 WIFCONTINUED(wstatus));
	}

	return buf;
}

/**
 * Handle SIGCHLD when a child process exit.
 */
static void
popen_sigchld_handler(EV_P_ ev_child *w, int revents)
{
	struct popen_handle *handle;
	(void)revents;

	say_debug("popen_sigchld_handler");

	/*
	 * Stop watching this child, libev will
	 * remove it from own hashtable.
	 */
	ev_child_stop(EV_A_ w);

	if (say_log_level_is_enabled(S_DEBUG)) {
		char buf[128], *str;

		str = wstatus_str(buf, sizeof(buf), w->rstatus);
		say_debug("popen: sigchld notify %d (%s)", w->rpid, str);
	}

	handle = popen_find(w->rpid);
	if (handle) {
		assert(handle->pid == w->rpid);
		assert(w == &handle->ev_sigchld);

		handle->wstatus = w->rstatus;
		if (WIFEXITED(w->rstatus) || WIFSIGNALED(w->rstatus)) {
			say_debug("popen: ev_child_stop %d", handle->pid);
			/*
			 * libev calls for waitpid by self so
			 * we don't have to wait here.
			 */
			popen_unregister(handle);
			handle->pid = -1;
		}
	}
}

/**
 * Get current child state.
 */
void
popen_state(struct popen_handle *handle, int *state, int *exit_code)
{
	assert(handle != NULL);

	if (handle->pid != -1) {
		*state = POPEN_STATE_ALIVE;
		*exit_code = 0;
	} else {
		if (WIFEXITED(handle->wstatus)) {
			*state = POPEN_STATE_EXITED;
			*exit_code = WEXITSTATUS(handle->wstatus);
		} else {
			*state = POPEN_STATE_SIGNALED;
			*exit_code = WTERMSIG(handle->wstatus);
		}
	}
}

/**
 * Get process state string representation.
 */
const char *
popen_state_str(unsigned int state)
{
	/*
	 * A process may be in a number of states,
	 * running/sleeping/disk sleep/stopped and etc
	 * but we are interested only if it is alive
	 * or dead (via plain exit or kill signal).
	 *
	 * Thus while it present in a system and not
	 * yet reaped we call it "alive".
	 *
	 * Note this is API for lua, so change with
	 * caution if needed.
	 */
	static const char *state_str[POPEN_STATE_MAX] = {
		[POPEN_STATE_NONE]	= "none",
		[POPEN_STATE_ALIVE]	= "alive",
		[POPEN_STATE_EXITED]	= "exited",
		[POPEN_STATE_SIGNALED]	= "signaled",
	};

	return state < POPEN_STATE_MAX ? state_str[state] : "unknown";
}

/**
 * Send a signal to a child process.
 *
 * When POPEN_FLAG_GROUP_SIGNAL is set the function sends
 * a signal to a process group rather than a process.
 *
 * A signal will not be sent if the child process is already
 * dead: otherwise we might kill another process that occupies
 * the same PID later. This means that if the child process
 * dies before its own childs, the function will not send a
 * signal to the process group even when ..._SETSID and
 * ..._GROUP_SIGNAL are set.
 *
 * Mac OS may don't deliver a signal to a processes in a group
 * when ..._SETSID and ..._GROUP_SIGNAL are set. It seems there
 * is a race here: when a process is just forked it may be not
 * signaled.
 *
 * Return 0 at success or -1 at failure (and set a diag).
 *
 * Possible errors:
 *
 * - SystemError: a process does not exists anymore
 *
 *                Aside of a non-exist process it is also
 *                set for a zombie process or when all
 *                processes in a group are zombies (but
 *                see note re Mac OS below).
 *
 * - SystemError: invalid signal number
 *
 * - SystemError: no permission to send a signal to
 *                a process or a process group
 *
 *                It is set on Mac OS when a signal is sent
 *                to a process group, where a group leader
 *                is zombie (or when all processes in it
 *                are zombies, don't sure).
 *
 *                Whether it may appear due to other
 *                reasons is unclear.
 *
 * Set errno to ESRCH when a process does not exist or is
 * zombie.
 */
int
popen_send_signal(struct popen_handle *handle, int signo)
{
	static const char *killops[] = { "kill", "killpg" };
	const char *killop = handle->flags & POPEN_FLAG_GROUP_SIGNAL ?
		killops[1] : killops[0];
	int ret;

	assert(handle != NULL);

	/*
	 * A child may be killed or exited already.
	 */
	ret = popen_may_pidop(handle);
	if (ret == 0) {
		say_debug("popen: %s %d signo %d", killop,
			  handle->pid, signo);
		assert(handle->pid != -1);
		if (handle->flags & POPEN_FLAG_GROUP_SIGNAL)
			ret = killpg(handle->pid, signo);
		else
			ret = kill(handle->pid, signo);
	}
	if (ret < 0 && errno == ESRCH) {
		diag_set(SystemError, "Attempt to send a signal %d to a "
			 "process that does not exist anymore", signo);
		return -1;
	} else if (ret < 0) {
		diag_set(SystemError, "Unable to %s %d signo %d",
			 killop, handle->pid, signo);
		return -1;
	}
	return 0;
}

/**
 * Delete a popen handle.
 *
 * Send SIGKILL and free the handle.
 *
 * Details about signaling:
 *
 * - The signal is sent only when ...KEEP_CHILD is not set.
 * - The signal is sent only when a process is alive according
 *   to the information available on current even loop iteration.
 *   (There is a gap here: a zombie may be signaled; it is
 *   harmless.)
 * - The signal is sent to a process or a grocess group depending
 *   of ..._GROUP_SIGNAL flag. @see popen_send_signal() for note
 *   about ..._GROUP_SIGNAL.
 *
 * Resources are released disregarding of whether a signal
 * sending succeeds: all fds occupied by the handle are closed,
 * the handle is removed from a living list, all occupied memory
 * is freed.
 *
 * The function may return 0 or -1 (and set a diag) as usual,
 * but it always frees the handle resources. So any return
 * value usually means success for a caller. The return
 * value and diagnostics are purely informational: it is
 * for logging or same kind of reporting.
 *
 * Possible diagnostics (don't consider them as errors):
 *
 * - SystemError: no permission to send a signal to
 *                a process or a process group
 *
 *                This error may appear due to Mac OS
 *                behaviour on zombies when
 *                ..._GROUP_SIGNAL is set,
 *                @see popen_send_signal().
 *
 *                Whether it may appear due to other
 *                reasons is unclear.
 *
 * Always return 0 when a process is known as dead: no signal
 * will be send, so no 'failure' may appear.
 */
int
popen_delete(struct popen_handle *handle)
{
	/*
	 * Save a result and a failure reason of the
	 * popen_send_signal() call.
	 */
	int rc = 0;
	struct diag *diag = diag_get();
	struct error *e = NULL;

	size_t i;

	assert(handle != NULL);

	if ((handle->flags & POPEN_FLAG_KEEP_CHILD) == 0) {
		/*
		 * Unable to kill the process -- save the error
		 * and pass over.
		 * The process is not exist already -- pass over.
		 */
		if (popen_send_signal(handle, SIGKILL) != 0 &&
		    errno != ESRCH) {
			rc = -1;
			e = diag_last_error(diag);
			assert(e != NULL);
			error_ref(e);
		}
	}

	for (i = 0; i < lengthof(handle->ios); i++) {
		if (handle->ios[i].fd != -1)
			coio_close_io(loop(), &handle->ios[i]);
	}

	/*
	 * Once we send a termination signal we no longer
	 * interested in this child process. Thus stop
	 * watching it. Note that we can enter deletion
	 * during error in popen creation so use list
	 * as a marker.
	 */
	if (handle->pid != -1 && !rlist_empty(&handle->list)) {
		say_debug("popen: ev_child_stop %d", handle->pid);
		ev_child_stop(EV_DEFAULT_ &handle->ev_sigchld);
		popen_unregister(handle);
	}

	rlist_del(&handle->list);
	handle_free(handle);

	/* Restore an error from popen_send_signal() if any. */
	if (rc != 0) {
		diag_set_error(diag, e);
		error_unref(e);
	}

	return rc;
}

/**
 * Create O_CLOEXEC pipes.
 */
static int
make_pipe(int pfd[2])
{
#ifdef TARGET_OS_LINUX
	if (pipe2(pfd, O_CLOEXEC)) {
		diag_set(SystemError, "Can't create pipe2");
		return -1;
	}
#else
	if (pipe(pfd)) {
		diag_set(SystemError, "Can't create pipe");
		return -1;
	}
	if (fcntl(pfd[0], F_SETFD, FD_CLOEXEC) ||
	    fcntl(pfd[1], F_SETFD, FD_CLOEXEC)) {
		int saved_errno = errno;
		diag_set(SystemError, "Can't unblock pipe");
		close(pfd[0]), pfd[0] = -1;
		close(pfd[1]), pfd[1] = -1;
		errno = saved_errno;
		return -1;
	}
#endif
	return 0;
}

/**
 * Close inherited file descriptors.
 *
 * @a skip_fds is an array of @a nr_skip_fds elements
 * with descriptors which should be kept opened.
 *
 * Returns 0 at success, otherwise -1 and set a diag.
 */
static int
close_inherited_fds(int *skip_fds, size_t nr_skip_fds)
{
# if defined(TARGET_OS_LINUX)
	static const char path[] = "/proc/self/fd";
# else
	static const char path[] = "/dev/fd";
# endif
	struct dirent *de;
	int fd_no, fd_dir;
	DIR *dir;
	size_t i;

	dir = opendir(path);
	if (!dir) {
		diag_set(SystemError, "fdin: Can't open %s", path);
		return -1;
	}
	fd_dir = dirfd(dir);

	for (de = readdir(dir); de; de = readdir(dir)) {
		if (!strcmp(de->d_name, ".") ||
		    !strcmp(de->d_name, ".."))
			continue;

		fd_no = atoi(de->d_name);

		if (fd_no == fd_dir)
			continue;

		/* We don't expect many numbers here */
		for (i = 0; i < nr_skip_fds; i++) {
			if (fd_no == skip_fds[i]) {
				fd_no = -1;
				break;
			}
		}

		if (fd_no == -1)
			continue;

		say_debug("popen: close inherited fd [%s:%d]",
			  stdX_str(fd_no), fd_no);
		if (close(fd_no)) {
			int saved_errno = errno;
			diag_set(SystemError, "fdin: Can't close %d", fd_no);
			closedir(dir);
			errno = saved_errno;
			return -1;
		}
	}

	if (closedir(dir)) {
		diag_set(SystemError, "fdin: Can't close %s", path);
		return -1;
	}
	return 0;
}

extern char **environ;

/**
 * Get pointer to environment variables to use in
 * a child process.
 */
static inline char **
get_envp(struct popen_opts *opts)
{
	if (!opts->env) {
		/* Inherit existing ones if not specified */
		return environ;
	}
	return opts->env;
}

/**
 * Reset signals to default before executing a program.
 *
 * FIXME: This is a code duplication fomr main.cc. Need to rework
 * signal handing otherwise it will become utter crap very fast.
 */
static void
signal_reset(void)
{
	struct sigaction sa;
	sigset_t sigset;

	memset(&sa, 0, sizeof(sa));

	/* Reset all signals to their defaults. */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;

	if (sigaction(SIGUSR1, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGHUP, &sa, NULL) == -1 ||
	    sigaction(SIGWINCH, &sa, NULL) == -1 ||
	    sigaction(SIGSEGV, &sa, NULL) == -1 ||
	    sigaction(SIGFPE, &sa, NULL) == -1) {
		say_error("child: sigaction failed");
		_exit(errno);
	}

	/* Unblock any signals blocked by libev */
	sigfillset(&sigset);
	if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1) {
		say_error("child: SIG_UNBLOCK failed");
		_exit(errno);
	}
}

/**
 * Create new popen handle.
 *
 * This function creates a new child process and passes it
 * pipe ends to communicate with child's stdin/stdout/stderr
 * depending on @a opts:flags. Where @a opts:flags could be
 * the bitwise "OR" for the following values:
 *
 *  POPEN_FLAG_FD_STDIN		- to write to stdin
 *  POPEN_FLAG_FD_STDOUT	- to read from stdout
 *  POPEN_FLAG_FD_STDERR	- to read from stderr
 *
 * When need to pass /dev/null descriptor into a child
 * the following values can be used:
 *
 *  POPEN_FLAG_FD_STDIN_DEVNULL
 *  POPEN_FLAG_FD_STDOUT_DEVNULL
 *  POPEN_FLAG_FD_STDERR_DEVNULL
 *
 * These flags have no effect if appropriate POPEN_FLAG_FD_STDx
 * flags are set.
 *
 * When need to completely close the descriptors the
 * following values can be used:
 *
 *  POPEN_FLAG_FD_STDIN_CLOSE
 *  POPEN_FLAG_FD_STDOUT_CLOSE
 *  POPEN_FLAG_FD_STDERR_CLOSE
 *
 * These flags have no effect if appropriate POPEN_FLAG_FD_STDx
 * flags are set.
 *
 * If none of POPEN_FLAG_FD_STDx flags are specified the child
 * process will run with all files inherited from a parent.
 *
 * Returns pointer to a new popen handle on success,
 * otherwise NULL returned and an error is set to the
 * diagnostics area.
 *
 * Possible errors:
 *
 * - IllegalParams: a parameter check fails:
 *   - group signal is set, while setsid is not.
 * - SystemError: dup(), fcntl(), pipe(), vfork() or close() fails
 *   in the parent process.
 * - SystemError: (temporary restriction) one of std{in,out,err}
 *   is closed in the parent process.
 * - OutOfMemory: unable to allocate handle.
 */
struct popen_handle *
popen_new(struct popen_opts *opts)
{
	/*
	 * Without volatile compiler claims that those
	 * variables might be clobbered from vfork.
	 */
	struct popen_handle * volatile handle;
	int volatile log_fd = -1;

	int pfd[POPEN_FLAG_FD_STDEND_BIT][2] = {
		{-1, -1}, {-1, -1}, {-1, -1},
	};

	char **envp = get_envp(opts);
	int saved_errno;
	size_t i;

	/*
	 * At max we could be skipping each pipe end
	 * plus dev/null variants and logfd
	 */
	int skip_fds[POPEN_FLAG_FD_STDEND_BIT * 2 + 2 + 1];
	size_t nr_skip_fds = 0;

	/*
	 * We must decouple log file descriptor from stderr in order to
	 * close or redirect stderr, but keep logging as is until
	 * execve() call.
	 *
	 * The new file descriptor should not have the same number as
	 * stdin, stdout or stderr.
	 *
	 * NB: It is better to acquire it from the parent to catch
	 * possible error sooner and don't ever call vfork() if we
	 * reached a file descriptor limit.
	 */
	int old_log_fd = log_get_fd();
	if (old_log_fd >= 0) {
		log_fd = dup_not_std_streams(old_log_fd);
		say_debug("popen: duplicate logfd: %d", log_fd);
		if (log_fd < 0)
			return NULL;
		if (fcntl(log_fd, F_SETFD, FD_CLOEXEC) != 0) {
			diag_set(SystemError,
				 "Unable to set FD_CLOEXEC on temporary logfd");
			close(log_fd);
			return NULL;
		}
	}

	/*
	 * A caller must preserve space for this.
	 */
	if (opts->flags & POPEN_FLAG_SHELL) {
		opts->argv[0] = "sh";
		opts->argv[1] = "-c";
	}

	static_assert(STDIN_FILENO == 0 &&
		      STDOUT_FILENO == 1 &&
		      STDERR_FILENO == 2,
		      "stdin/out/err are not posix compatible");

	static_assert(lengthof(pfd) == lengthof(pfd_map),
		      "Pipes number does not map to fd bits");

	static_assert(POPEN_FLAG_FD_STDIN_BIT == STDIN_FILENO &&
		      POPEN_FLAG_FD_STDOUT_BIT == STDOUT_FILENO &&
		      POPEN_FLAG_FD_STDERR_BIT == STDERR_FILENO,
		      "Popen flags do not match stdX");

	handle = handle_new(opts);
	if (!handle) {
		if (log_fd >= 0)
			close(log_fd);
		return NULL;
	}

	if (log_fd >= 0)
		skip_fds[nr_skip_fds++] = log_fd;
	skip_fds[nr_skip_fds++] = dev_null_fd_ro;
	skip_fds[nr_skip_fds++] = dev_null_fd_wr;
	assert(nr_skip_fds <= lengthof(skip_fds));

	for (i = 0; i < lengthof(pfd_map); i++) {
		if (opts->flags & pfd_map[i].mask) {
			if (make_pipe(pfd[i]))
				goto out_err;

			/*
			 * FIXME: Rather force make_pipe
			 * to allocate new fds with higher
			 * number.
			 */
			if (pfd[i][0] <= STDERR_FILENO ||
			    pfd[i][1] <= STDERR_FILENO) {
				errno = EBADF;
				diag_set(SystemError,
					 "Low fds number [%s:%d:%d]",
					  stdX_str(i), pfd[i][0],
					  pfd[i][1]);
				goto out_err;
			}

			skip_fds[nr_skip_fds++] = pfd[i][0];
			skip_fds[nr_skip_fds++] = pfd[i][1];
			assert(nr_skip_fds <= lengthof(skip_fds));

			say_debug("popen: created pipe [%s:%d:%d]",
				  stdX_str(i), pfd[i][0], pfd[i][1]);
		} else if (!(opts->flags & pfd_map[i].mask_devnull) &&
			   !(opts->flags & pfd_map[i].mask_close)) {
			skip_fds[nr_skip_fds++] = pfd_map[i].fileno;

			say_debug("popen: inherit [%s:%d]",
				  stdX_str(i), pfd_map[i].fileno);
		}
	}

	/*
	 * We have to use vfork here because libev has own
	 * at_fork helpers with mutex, so we will have double
	 * lock here and stuck forever otherwise.
	 *
	 * The good news that this affect tx only the
	 * other tarantoll threads are not waiting for
	 * vfork to complete. Also we try to do as minimum
	 * operations before the exec() as possible.
	 */
#pragma GCC diagnostic push
	/*
	 * TODO(gh-6674): we need to review popen's design and refactor this
	 * part, completely getting rid of vfork, or change vfork to
	 * posix_spawn here, but only on macOS.
	 */
#pragma GCC diagnostic warning "-Wdeprecated-declarations"
	handle->pid = vfork();
#pragma GCC diagnostic pop
	if (handle->pid < 0) {
		diag_set(SystemError, "vfork() fails");
		goto out_err;
	} else if (handle->pid == 0) {
		/*
		 * The documentation for libev says that
		 * each new fork should call ev_loop_fork(EV_DEFAULT)
		 * on every new child process, but we're going
		 * to execute bew binary anyway thus everything
		 * related to OS resources will be eliminated except
		 * file descriptors we use for piping. Thus don't
		 * do anything special.
		 */

		/*
		 * Replace the logger fd to its duplicate. It
		 * should be done before we'll close inherited
		 * fds: old logger fd may be stderr and stderr may
		 * be subject to close.
		 *
		 * We should also do it before a first call to a
		 * say_*() function, because otherwise a user may
		 * capture our debug logs as stderr of the child
		 * process.
		 */
		if (log_fd >= 0)
			log_set_fd(log_fd);

		/*
		 * Also don't forget to drop signal handlers
		 * to default inside a child process since we're
		 * inheriting them from a caller process.
		 */
		if (opts->flags & POPEN_FLAG_RESTORE_SIGNALS)
			signal_reset();

		if (opts->flags & POPEN_FLAG_SETSID) {
#ifndef TARGET_OS_DARWIN
			if (setsid() == -1) {
				say_syserror("child: setsid failed");
				goto exit_child;
			}
#else
			/*
			 * Note that on MacOS we're not allowed to
			 * set sid after vfork (it is OS specific)
			 * thus use ioctl instead.
			 */
			int ttyfd = open("/dev/tty", O_RDWR, 0);
			if (ttyfd >= 0) {
				ioctl(ttyfd, TIOCNOTTY, 0);
				close(ttyfd);
			}

			if (setpgrp() == -1) {
				say_syserror("child: setpgrp failed");
				goto exit_child;
			}
#endif
		}

		if (opts->flags & POPEN_FLAG_CLOSE_FDS) {
			if (close_inherited_fds(skip_fds, nr_skip_fds) != 0) {
				diag_log();
				say_syserror("child: close inherited fds");
				goto exit_child;
			}
		}

		for (i = 0; i < lengthof(pfd_map); i++) {
			int fileno = pfd_map[i].fileno;
			/*
			 * Pass pipe peer to a child.
			 */
			if (opts->flags & pfd_map[i].mask) {
				int child_idx = pfd_map[i].child_idx;

				/* put child peer end at known place */
				if (dup2(pfd[i][child_idx], fileno) < 0) {
					say_syserror("child: dup %d -> %d",
						     pfd[i][child_idx], fileno);
					goto exit_child;
				}

				/* parent's pipe no longer needed */
				if (close(pfd[i][0]) ||
				    close(pfd[i][1])) {
					say_syserror("child: close %d %d",
						     pfd[i][0], pfd[i][1]);
					goto exit_child;
				}
				continue;
			}

			/*
			 * Use /dev/null if requested.
			 */
			if (opts->flags & pfd_map[i].mask_devnull) {
				if (dup2(*pfd_map[i].dev_null_fd, fileno) < 0) {
					say_syserror("child: dup2 %d -> %d",
						     *pfd_map[i].dev_null_fd,
						     fileno);
					goto exit_child;
				}
				continue;
			}

			/*
			 * Or close the destination completely, since
			 * we don't if the file in question is already
			 * closed by some other code we don't care if
			 * EBADF happens.
			 */
			if (opts->flags & pfd_map[i].mask_close) {
				if (close(fileno) && errno != EBADF) {
					say_syserror("child: can't close %d",
						     fileno);
					goto exit_child;
				}
				continue;
			}

			/*
			 * Otherwise inherit file descriptor
			 * from a parent.
			 */
		}

		if (close(dev_null_fd_ro) || close(dev_null_fd_wr)) {
			say_error("child: can't close %d or %d",
				  dev_null_fd_ro, dev_null_fd_wr);
			goto exit_child;
		}

		/*
		 * Return the logger back, because we're in the
		 * same virtual memory address space as the
		 * parent.
		 */
		if (log_fd >= 0)
			log_set_fd(old_log_fd);

		if (opts->flags & POPEN_FLAG_SHELL)
			execve(_PATH_BSHELL, opts->argv, envp);
		else
			execve(opts->argv[0], opts->argv, envp);
exit_child:
		if (log_fd >= 0)
			log_set_fd(old_log_fd);
		_exit(errno);
		unreachable();
	}

	for (i = 0; i < lengthof(pfd_map); i++) {
		if (opts->flags & pfd_map[i].mask) {
			int parent_idx = pfd_map[i].parent_idx;
			int child_idx = pfd_map[i].child_idx;
			int parent_fd = pfd[i][parent_idx];

			coio_create(&handle->ios[i], parent_fd);
			if (fcntl(parent_fd, F_SETFL, O_NONBLOCK)) {
				diag_set(SystemError, "Can't set O_NONBLOCK [%s:%d]",
					 stdX_str(i), parent_fd);
				goto out_err;
			}

			say_debug("popen: keep pipe [%s:%d]",
				  stdX_str(i), parent_fd);

			if (close(pfd[i][child_idx])) {
				diag_set(SystemError, "Can't close child [%s:%d]",
					     stdX_str(i), pfd[i][child_idx]);
				goto out_err;
			}

			pfd[i][child_idx] = -1;
		}
	}

	/* Close the temporary logger fd. */
	if (log_fd >= 0 && close(log_fd) != 0) {
		diag_set(SystemError, "Can't close temporary logfd %d", log_fd);
		log_fd = -1;
		goto out_err;
	}

	/*
	 * Link it into global list for force
	 * cleanup on exit. Note we use this
	 * list as a sign that child is registered
	 * in popen_delete.
	 */
	rlist_add(&popen_head, &handle->list);

	/*
	 * To watch when a child get exited.
	 */
	popen_register(handle);

	say_debug("popen: ev_child_start %d", handle->pid);
	ev_child_init(&handle->ev_sigchld, popen_sigchld_handler, handle->pid, 0);
	ev_child_start(EV_DEFAULT_ &handle->ev_sigchld);

	say_debug("popen: created child %d", handle->pid);

	return handle;

out_err:
	diag_log();
	saved_errno = errno;

	/*
	 * Save a reason of failure, because popen_delete() may
	 * clobber the diagnostics area.
	 */
	struct diag *diag = diag_get();
	struct error *e = diag_last_error(diag);
	assert(e != NULL);
	error_ref(e);

	popen_delete(handle);
	for (i = 0; i < lengthof(pfd); i++) {
		if (pfd[i][0] != -1)
			close(pfd[i][0]);
		if (pfd[i][1] != -1)
			close(pfd[i][1]);
	}
	if (log_fd >= 0)
		close(log_fd);

	/* Restore the diagnostics area entry. */
	diag_set_error(diag, e);
	error_unref(e);

	errno = saved_errno;
	return NULL;
}

/**
 * Initialize popen subsystem.
 */
void
popen_init(void)
{
	static const int flags = O_CLOEXEC;
	static const char dev_null_path[] = "/dev/null";

	say_debug("popen: initialize subsystem");
	popen_pids_map = mh_i32ptr_new();

	dev_null_fd_ro = open(dev_null_path, O_RDONLY | flags);
	if (dev_null_fd_ro < 0)
		goto out_err;
	dev_null_fd_wr = open(dev_null_path, O_WRONLY | flags);
	if (dev_null_fd_wr < 0)
		goto out_err;

	/*
	 * FIXME: We should allocate them somewhere
	 * after STDERR_FILENO so the child would be
	 * able to find these fd numbers not occupied.
	 * Other option is to use unix scm and send
	 * them to the child on demand.
	 *
	 * For now left as is since we don't close
	 * our main stdX descriptors and they are
	 * inherited when we call first vfork.
	 */
	if (dev_null_fd_ro <= STDERR_FILENO ||
	    dev_null_fd_wr <= STDERR_FILENO) {
		say_error("popen: /dev/null %d %d numbers are too low",
			  dev_null_fd_ro, dev_null_fd_wr);
		goto out_err;
	}

	return;

out_err:
	say_syserror("popen: Can't open %s", dev_null_path);
	if (dev_null_fd_ro >= 0)
		close(dev_null_fd_ro);
	if (dev_null_fd_wr >= 0)
		close(dev_null_fd_wr);
	mh_i32ptr_delete(popen_pids_map);
	exit(EXIT_FAILURE);
}

/**
 * Free popen subsystem.
 *
 * Kills all running children and frees resources.
 */
void
popen_free(void)
{
	struct popen_handle *handle, *tmp;

	say_debug("popen: free subsystem");

	close(dev_null_fd_ro);
	close(dev_null_fd_wr);
	dev_null_fd_ro = -1;
	dev_null_fd_wr = -1;

	rlist_foreach_entry_safe(handle, &popen_head, list, tmp) {
		/*
		 * If children are still running we should move
		 * them out of the pool and kill them all then.
		 * Note though that we don't do an explicit wait
		 * here, OS will reap them anyway, just release
		 * the memory occupied for popen handles.
		 */
		popen_delete(handle);
	}

	if (popen_pids_map) {
		mh_i32ptr_delete(popen_pids_map);
		popen_pids_map = NULL;
	}
}
