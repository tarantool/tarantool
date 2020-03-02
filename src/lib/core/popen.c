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
 * Allocate new popen hanldle with flags specified.
 */
static struct popen_handle *
handle_new(struct popen_opts *opts)
{
	struct popen_handle *handle;
	size_t size = 0, i;
	char *pos;

	assert(opts->argv != NULL && opts->nr_argv > 0);

	for (i = 0; i < opts->nr_argv; i++) {
		if (opts->argv[i] == NULL)
			continue;
		size += strlen(opts->argv[i]) + 1;
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
		strcpy(pos, opts->argv[i]);
		pos += strlen(opts->argv[i]);
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
 * Test if the handle can run io operation.
 */
static inline bool
popen_may_io(struct popen_handle *handle, unsigned int idx,
	     unsigned int io_flags)
{
	if (!handle) {
		errno = ESRCH;
		return false;
	}

	if (!(io_flags & handle->flags)) {
		errno = EINVAL;
		return false;
	}

	if (handle->ios[idx].fd < 0) {
		errno = EPIPE;
		return false;
	}

	return true;
}

/**
 * Test if the handle is not nil and still have
 * a living child process.
 */
static inline bool
popen_may_pidop(struct popen_handle *handle)
{
	if (!handle || handle->pid == -1) {
		errno = ESRCH;
		return false;
	}
	return true;
}

/**
 * Fill popen object statistics.
 */
int
popen_stat(struct popen_handle *handle, struct popen_stat *st)
{
	if (!handle) {
		errno = ESRCH;
		return -1;
	}

	st->pid		= handle->pid;
	st->flags	= handle->flags;

	static_assert(lengthof(st->fds) == lengthof(handle->ios),
		      "Statistics fds are screwed");

	for (size_t i = 0; i < lengthof(handle->ios); i++)
		st->fds[i] = handle->ios[i].fd;
	return 0;
}

/**
 * Get a pointer to the former command line.
 */
const char *
popen_command(struct popen_handle *handle)
{
	if (!handle) {
		errno = ESRCH;
		return NULL;
	}

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
 */
int
popen_write_timeout(struct popen_handle *handle, void *buf,
		    size_t count, unsigned int flags,
		    ev_tstamp timeout)
{
	int idx = STDIN_FILENO;

	if (!(flags & POPEN_FLAG_FD_STDIN)) {
	    errno = EINVAL;
	    return -1;
	}

	if (!popen_may_io(handle, STDIN_FILENO, flags))
		return -1;

	if (count > (size_t)SSIZE_MAX) {
		errno = E2BIG;
		return -1;
	}

	say_debug("popen: %d: write idx [%s:%d] buf %p count %zu "
		  "fds %d timeout %.9g",
		  handle->pid, stdX_str(idx), idx, buf, count,
		  handle->ios[idx].fd, timeout);

	return coio_write_timeout(&handle->ios[idx], buf,
				  count, timeout);
}

/**
 * Read data from a child's peer with timeout.
 */
ssize_t
popen_read_timeout(struct popen_handle *handle, void *buf,
		   size_t count, unsigned int flags,
		   ev_tstamp timeout)
{
	int idx = flags & POPEN_FLAG_FD_STDOUT ?
		STDOUT_FILENO : STDERR_FILENO;

	if (!(flags & (POPEN_FLAG_FD_STDOUT | POPEN_FLAG_FD_STDERR))) {
	    errno = EINVAL;
	    return -1;
	}

	if (!popen_may_io(handle, idx, flags))
		return -1;

	if (count > (size_t)SSIZE_MAX) {
		errno = E2BIG;
		return -1;
	}

	if (timeout < 0.)
		timeout = TIMEOUT_INFINITY;

	say_debug("popen: %d: read idx [%s:%d] buf %p count %zu "
		  "fds %d timeout %.9g",
		  handle->pid, stdX_str(idx), idx, buf, count,
		  handle->ios[idx].fd, timeout);

	return coio_read_timeout(&handle->ios[idx], buf,
				 count, timeout);
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
int
popen_state(struct popen_handle *handle, int *state, int *exit_code)
{
	if (!handle) {
		errno = ESRCH;
		return -1;
	}

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

	return 0;
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
 */
int
popen_send_signal(struct popen_handle *handle, int signo)
{
	int ret;

	/*
	 * A child may be killed or exited already.
	 */
	if (!popen_may_pidop(handle))
		return -1;

	say_debug("popen: kill %d signo %d", handle->pid, signo);
	ret = kill(handle->pid, signo);
	if (ret < 0) {
		diag_set(SystemError, "Unable to kill %d signo %d",
			 handle->pid, signo);
	}
	return ret;
}

/**
 * Delete a popen handle.
 *
 * The function kills a child process and
 * close all fds and remove the handle from
 * a living list and finally frees the handle.
 */
int
popen_delete(struct popen_handle *handle)
{
	size_t i;

	if (!handle) {
		errno = ESRCH;
		return -1;
	}

	if (popen_send_signal(handle, SIGKILL) && errno != ESRCH)
		return -1;

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
	return 0;
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
	if (fcntl(pfd[0], F_SETFL, O_CLOEXEC) ||
	    fcntl(pfd[1], F_SETFL, O_CLOEXEC)) {
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
	    sigaction(SIGFPE, &sa, NULL) == -1)
		_exit(errno);

	/* Unblock any signals blocked by libev */
	sigfillset(&sigset);
	if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1)
		_exit(errno);
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
 * otherwise NULL returned setting @a errno.
 */
struct popen_handle *
popen_new(struct popen_opts *opts)
{
	/*
	 * Without volatile compiler claims that
	 * handle might be clobbered from vfork.
	 */
	struct popen_handle * volatile handle;

	int pfd[POPEN_FLAG_FD_STDEND_BIT][2] = {
		{-1, -1}, {-1, -1}, {-1, -1},
	};

	char **envp = get_envp(opts);
	int saved_errno;
	size_t i;

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
	/*
	 * At max we could be skipping each pipe end
	 * plus dev/null variants.
	 */
	int skip_fds[POPEN_FLAG_FD_STDEND_BIT * 2 + 2];
	size_t nr_skip_fds = 0;

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
	if (!handle)
		return NULL;

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
	handle->pid = vfork();
	if (handle->pid < 0) {
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
		 * Also don't forget to drop signal handlers
		 * to default inside a child process since we're
		 * inheriting them from a caller process.
		 */
		if (opts->flags & POPEN_FLAG_RESTORE_SIGNALS)
			signal_reset();

		/*
		 * We have to be a session leader otherwise
		 * won't be able to kill a group of children.
		 */
		if (opts->flags & POPEN_FLAG_SETSID) {
			if (setsid() == -1)
				goto exit_child;
		}

		if (opts->flags & POPEN_FLAG_CLOSE_FDS) {
			if (close_inherited_fds(skip_fds, nr_skip_fds))
				goto exit_child;
		}

		for (i = 0; i < lengthof(pfd_map); i++) {
			int fileno = pfd_map[i].fileno;
			/*
			 * Pass pipe peer to a child.
			 */
			if (opts->flags & pfd_map[i].mask) {
				int child_idx = pfd_map[i].child_idx;

				/* put child peer end at known place */
				if (dup2(pfd[i][child_idx], fileno) < 0)
					goto exit_child;

				/* parent's pipe no longer needed */
				if (close(pfd[i][0]) ||
				    close(pfd[i][1]))
					goto exit_child;
				continue;
			}

			/*
			 * Use /dev/null if requested.
			 */
			if (opts->flags & pfd_map[i].mask_devnull) {
				if (dup2(*pfd_map[i].dev_null_fd,
					 fileno) < 0) {
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
				if (close(fileno) && errno != EBADF)
					goto exit_child;
				continue;
			}

			/*
			 * Otherwise inherit file descriptor
			 * from a parent.
			 */
		}

		if (close(dev_null_fd_ro) || close(dev_null_fd_wr))
			goto exit_child;

		if (opts->flags & POPEN_FLAG_SHELL)
			execve(_PATH_BSHELL, opts->argv, envp);
		else
			execve(opts->argv[2], &opts->argv[2], envp);
exit_child:
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
	popen_delete(handle);
	for (i = 0; i < lengthof(pfd); i++) {
		if (pfd[i][0] != -1)
			close(pfd[i][0]);
		if (pfd[i][1] != -1)
			close(pfd[i][1]);
	}
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
