/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <replication.h>
#include <say.h>
#include <fiber.h>
extern "C" {
#include <cfg/warning.h>
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */
#include <stddef.h>

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <fcntl.h>

#include "fiber.h"
#include "recovery.h"
#include "log_io.h"
#include "evio.h"

/** Replication topology
 * ----------------------
 *
 * Tarantool replication consists of 3 interacting processes:
 * master, spawner and replication relay.
 *
 * The spawner is created at server start, and master communicates
 * with the spawner using a socketpair(2). Replication relays are
 * created by the spawner and handle one client connection each.
 *
 * The master process binds to the primary port and accepts
 * incoming connections. This is done in the master to be able to
 * correctly handle authentication of replication clients.
 *
 * Once a client socket is accepted, it is sent to the spawner
 * process, through the master's end of the socket pair.
 *
 * The spawner listens on the receiving end of the socket pair and
 * for every received socket creates a replication relay, which is
 * then responsible for sending write ahead logs to the replica.
 *
 * Upon shutdown, the master closes its end of the socket pair.
 * The spawner then reads EOF from its end, terminates all
 * children and exits.
 */
static int master_to_spawner_socket;

/**
 * State of a replica. We only need one global instance
 * since we fork() for every replica.
 */
struct replica {
	/** Replica connection */
	int sock;
	/** Initial lsn. */
	int64_t lsn;
} replica;

/** Send a file descriptor to replication relay spawner.
 *
 * Invoked when spawner's end of the socketpair becomes ready.
 */
static void
replication_send_socket(ev_loop *loop, ev_io *watcher, int /* events */);

/** Replication spawner process */
static struct spawner {
	/** reading end of the socket pair with the master */
	int sock;
	/** non-zero if got a terminating signal */
	sig_atomic_t killed;
	/** child process count */
	sig_atomic_t child_count;
} spawner;

/** Initialize spawner process.
 *
 * @param sock the socket between the main process and the spawner.
 */
static void
spawner_init(int sock);

/** Spawner main loop. */
static void
spawner_main_loop();

/** Shutdown spawner and all its children. */
static void
spawner_shutdown();

/** Handle SIGINT, SIGTERM, SIGHUP. */
static void
spawner_signal_handler(int signal);

/** Handle SIGCHLD: collect status of a terminated child.  */
static void
spawner_sigchld_handler(int signal __attribute__((unused)));

/** Create a replication relay.
 *
 * @return 0 on success, -1 on error
 */
static int
spawner_create_replication_relay();

/** Shut down all relays when shutting down the spawner. */
static void
spawner_shutdown_children();

/** Initialize replication relay process. */
static void
replication_relay_loop();

/*
 * ------------------------------------------------------------------------
 * replication module
 * ------------------------------------------------------------------------
 */

/** Pre-fork replication spawner process. */
void
replication_prefork()
{
	int sockpair[2];
	/*
	 * Create UNIX sockets to communicate between the main and
	 * spawner processes.
         */
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sockpair) != 0)
		panic_syserror("socketpair");

	/* create spawner */
	pid_t pid = fork();
	if (pid == -1)
		panic_syserror("fork");

	if (pid != 0) {
		/* parent process: tarantool */
		close(sockpair[1]);
		master_to_spawner_socket = sockpair[0];
		sio_setfl(master_to_spawner_socket, O_NONBLOCK, 1);
	} else {
		ev_loop_fork(loop());
		ev_run(loop(), EVRUN_NOWAIT);
		/* child process: spawner */
		close(sockpair[0]);
		/*
		 * Move to an own process group, to not receive
		 * signals from the controlling tty.
		 */
		setpgid(0, 0);
		spawner_init(sockpair[1]);
	}
}

/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/** State of subscribe request - master process. */
struct subscribe_request {
	struct ev_io io;
	int fd;
	int64_t lsn;
};

/** Replication acceptor fiber handler. */
void
subscribe(int fd, int64_t lsn)
{
	struct subscribe_request *request = (struct subscribe_request *)
		malloc(sizeof(struct subscribe_request));
	if (request == NULL) {
		close(fd);
		return;
	}
	request->fd = fd;
	request->io.data = request;
	request->lsn = lsn;
	ev_io_init(&request->io, replication_send_socket,
		   master_to_spawner_socket, EV_WRITE);
	ev_io_start(loop(), &request->io);
}


/** Send a file descriptor to the spawner. */
static void
replication_send_socket(ev_loop *loop, ev_io *watcher, int /* events */)
{
	struct subscribe_request *request =
		(struct subscribe_request *) watcher->data;
	struct msghdr msg;
	struct iovec iov;
	char control_buf[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *control_message = NULL;

	iov.iov_base = &request->lsn;
	iov.iov_len = sizeof(request->lsn);

	memset(&msg, 0, sizeof(msg));

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	control_message = CMSG_FIRSTHDR(&msg);
	control_message->cmsg_len = CMSG_LEN(sizeof(int));
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	*((int *) CMSG_DATA(control_message)) = request->fd;

	/* Send the client socket to the spawner. */
	if (sendmsg(master_to_spawner_socket, &msg, 0) < 0)
		say_syserror("sendmsg");

	ev_io_stop(loop, watcher);
	/* Close client socket in the main process. */
	close(request->fd);
	free(request);
}


/*--------------------------------------------------------------------------*
 * spawner process                                                          *
 * -------------------------------------------------------------------------*/

/** Initialize the spawner. */

static void
spawner_init(int sock)
{
	struct sigaction sa;

	title("spawner", NULL);
	fiber_set_name(fiber(), status);

	/* init replicator process context */
	spawner.sock = sock;

	/* init signals */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);

	/*
	 * The spawner normally does not receive any signals,
	 * except when sent by a system administrator.
	 * When the master process terminates, it closes its end
	 * of the socket pair and this signals to the spawner that
	 * it's time to die as well. But before exiting, the
	 * spawner must kill and collect all active replication
	 * relays. This is why we need to change the default
	 * signal action here.
	 */
	sa.sa_handler = spawner_signal_handler;

	if (sigaction(SIGHUP, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1)
		say_syserror("sigaction");

	sa.sa_handler = spawner_sigchld_handler;

	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		say_syserror("sigaction");

	sa.sa_handler = SIG_IGN;
	/*
	 * Ignore SIGUSR1, SIGUSR1 is used to make snapshots,
	 * and if someone wrote a faulty regexp for `ps' and
	 * fed it to `kill' the replication shouldn't die.
	 * Ignore SIGUSR2 as well, since one can be pretty
	 * inventive in ways of shooting oneself in the foot.
	 * Ignore SIGPIPE, otherwise we may receive SIGPIPE
	 * when trying to write to the log.
	 */
	if (sigaction(SIGUSR1, &sa, NULL) == -1 ||
	    sigaction(SIGUSR2, &sa, NULL) == -1 ||
	    sigaction(SIGPIPE, &sa, NULL) == -1) {

		say_syserror("sigaction");
	}

	say_crit("initialized");
	spawner_main_loop();
}

static int
spawner_unpack_cmsg(struct msghdr *msg)
{
	struct cmsghdr *control_message;
	for (control_message = CMSG_FIRSTHDR(msg);
	     control_message != NULL;
	     control_message = CMSG_NXTHDR(msg, control_message))
		if ((control_message->cmsg_level == SOL_SOCKET) &&
		    (control_message->cmsg_type == SCM_RIGHTS))
			return *((int *) CMSG_DATA(control_message));
	assert(false);
	return -1;
}

/** Replication spawner process main loop. */
static void
spawner_main_loop()
{
	struct msghdr msg;
	struct iovec iov;
	char control_buf[CMSG_SPACE(sizeof(int))];

	iov.iov_base = &replica.lsn;
	iov.iov_len = sizeof(replica.lsn);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	while (!spawner.killed) {
		int msglen = recvmsg(spawner.sock, &msg, 0);
		if (msglen > 0) {
			replica.sock = spawner_unpack_cmsg(&msg);
			spawner_create_replication_relay();
		} else if (msglen == 0) { /* orderly master shutdown */
			say_info("Exiting: master shutdown");
			break;
		} else { /* msglen == -1 */
			if (errno != EINTR)
				say_syserror("recvmsg");
			/* continue, the error may be temporary */
		}
	}
	spawner_shutdown();
}

/** Replication spawner shutdown. */
static void
spawner_shutdown()
{
	/*
	 * There is no need to ever use signals with the spawner
	 * process. If someone did send spawner a signal by
	 * mistake, at least make a squeak in the error log before
	 * dying.
	 */
	if (spawner.killed)
		say_info("Terminated by signal %d", (int) spawner.killed);

	/* close socket */
	close(spawner.sock);

	/* kill all children */
	spawner_shutdown_children();

	exit(EXIT_SUCCESS);
}

/** Replication spawner signal handler for terminating signals. */
static void spawner_signal_handler(int signal)
{
	spawner.killed = signal;
}

/** Wait for a terminated child. */
static void
spawner_sigchld_handler(int signo __attribute__((unused)))
{
	static const char waitpid_failed[] = "spawner: waitpid() failed\n";
	do {
		int exit_status;
		pid_t pid = waitpid(-1, &exit_status, WNOHANG);
		switch (pid) {
		case -1:
			if (errno != ECHILD) {
				int r = write(sayfd, waitpid_failed,
					      sizeof(waitpid_failed) - 1);
				(void) r; /* -Wunused-result warning suppression */
			}
			return;
		case 0: /* no more changes in children status */
			return;
		default:
			spawner.child_count--;
		}
	} while (spawner.child_count > 0);
}

/** Create replication client handler process. */
static int
spawner_create_replication_relay()
{
	pid_t pid = fork();

	if (pid < 0) {
		say_syserror("fork");
		return -1;
	}

	if (pid == 0) {
		ev_loop_fork(loop());
		ev_run(loop(), EVRUN_NOWAIT);
		close(spawner.sock);
		replication_relay_loop();
	} else {
		spawner.child_count++;
		close(replica.sock);
		say_info("created a replication relay: pid = %d", (int) pid);
	}

	return 0;
}

/** Replicator spawner shutdown: kill and wait for children. */
static void
spawner_shutdown_children()
{
	int kill_signo = SIGTERM, signo;
	sigset_t mask, orig_mask, alarm_mask;

retry:
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGALRM);
	/*
	 * We're going to kill the entire process group, which
	 * we're part of. Handle the signal sent to ourselves.
	 */
	sigaddset(&mask, kill_signo);

	if (spawner.child_count == 0)
		return;

	/* Block SIGCHLD and SIGALRM to avoid races. */
	if (sigprocmask(SIG_BLOCK, &mask, &orig_mask)) {
		say_syserror("sigprocmask");
		return;
	}

	/* We'll wait for children no longer than 5 sec.  */
	alarm(5);

	say_info("sending signal %d to %d children", kill_signo,
		 (int) spawner.child_count);

	kill(0, kill_signo);

	say_info("waiting for children for up to 5 seconds");

	while (spawner.child_count > 0) {
		sigwait(&mask, &signo);
		if (signo == SIGALRM) {         /* timed out */
			break;
		}
		else if (signo != kill_signo) {
			assert(signo == SIGCHLD);
			spawner_sigchld_handler(signo);
		}
	}

	/* Reset the alarm. */
	alarm(0);

	/* Clear possibly pending SIGALRM. */
	sigpending(&alarm_mask);
	if (sigismember(&alarm_mask, SIGALRM)) {
		sigemptyset(&alarm_mask);
		sigaddset(&alarm_mask, SIGALRM);
		sigwait(&alarm_mask, &signo);
	}

	/* Restore the old mask. */
	if (sigprocmask(SIG_SETMASK, &orig_mask, NULL)) {
		say_syserror("sigprocmask");
		return;
	}

	if (kill_signo == SIGTERM) {
		kill_signo = SIGKILL;
		goto retry;
	}
}

/** A libev callback invoked when a relay client socket is ready
 * for read. This currently only happens when the client closes
 * its socket, and we get an EOF.
 */
static void
replication_relay_recv(ev_loop * /* loop */, struct ev_io *w, int __attribute__((unused)) revents)
{
	int replica_sock = (int) (intptr_t) w->data;
	uint8_t data;

	int rc = recv(replica_sock, &data, sizeof(data), 0);

	if (rc == 0 || (rc < 0 && errno == ECONNRESET)) {
		say_info("the client has closed its replication socket, exiting");
		exit(EXIT_SUCCESS);
	}
	if (rc < 0)
		say_syserror("recv");

	exit(EXIT_FAILURE);
}


/** Send a single row to the client. */
static int
replication_relay_send_row(void * /* param */, const struct log_row *row)
{
	ssize_t bytes, len = log_row_size(row);
	while (len > 0) {
		bytes = write(replica.sock, row, len);
		if (bytes < 0) {
			if (errno == EPIPE) {
				/* socket closed on opposite site */
				goto shutdown_handler;
			}
			panic_syserror("write");
		}
		len -= bytes;
		row += bytes;
	}

	return 0;
shutdown_handler:
	say_info("the client has closed its replication socket, exiting");
	exit(EXIT_SUCCESS);
}

static void
replication_relay_send_snapshot()
{
	FDGuard guard_replica(replica.sock);
	struct log_dir dir = snap_dir;
	dir.dirname = cfg.snap_dir;
	int64_t lsn = greatest_lsn(&dir);
	const char *filename = format_filename(&dir, lsn, NONE);
	int snapshot = open(filename, O_RDONLY);
	if (snapshot < 0)
		panic_syserror("can't find/open snapshot");

	FDGuard guard_snapshot(snapshot);

	struct stat st;
	if (fstat(snapshot, &st) != 0)
		panic_syserror("fstat");

	uint64_t header[2];
	header[0] = lsn;
	header[1] = st.st_size;
	sio_writen(replica.sock, header, sizeof(header));
	sio_sendfile(replica.sock, snapshot, NULL, header[1]);

	exit(EXIT_SUCCESS);
}

/** The main loop of replication client service process. */
static void
replication_relay_loop()
{
	struct sigaction sa;

	/* Set process title and fiber name.
	 * Even though we use only the main fiber, the logger
	 * uses the current fiber name.
	 */
	struct sockaddr_in peer;
	socklen_t addrlen = sizeof(peer);
	getpeername(replica.sock, ((struct sockaddr*)&peer), &addrlen);
	title("relay", "%s", sio_strfaddr(&peer));
	fiber_set_name(fiber(), status);

	/* init signals */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);

	/* Reset all signals to their defaults. */
	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGCHLD, &sa, NULL) == -1 ||
	    sigaction(SIGHUP, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1 ||
	    sigaction(SIGTERM, &sa, NULL) == -1)
		say_syserror("sigaction");

	/*
	 * Ignore SIGPIPE, we already handle EPIPE.
	 * Ignore SIGUSR1, SIGUSR1 is used to make snapshots,
	 * and if someone wrote a faulty regexp for `ps' and
	 * fed it to `kill' the replication shouldn't die.
	 * Ignore SIGUSR2 as well, since one can be pretty
	 * inventive in ways of shooting oneself in the foot.
	 */
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1 ||
	    sigaction(SIGUSR1, &sa, NULL) == -1 ||
	    sigaction(SIGUSR2, &sa, NULL) == -1) {

		say_syserror("sigaction");
	}

	if (replica.lsn == 0)
		replication_relay_send_snapshot(); /* exits */
	/*
	 * Init a read event: when replica closes its end
	 * of the socket, we can read EOF and shutdown the
	 * relay.
	 */
	struct ev_io sock_read_ev;
	sock_read_ev.data = (void *)(intptr_t) replica.sock;
	ev_io_init(&sock_read_ev, replication_relay_recv,
		   replica.sock, EV_READ);
	ev_io_start(loop(), &sock_read_ev);

	/* Initialize the recovery process */
	recovery_init(cfg.snap_dir, cfg.wal_dir,
		      replication_relay_send_row,
		      NULL, INT32_MAX);
	/*
	 * Note that recovery starts with lsn _NEXT_ to
	 * the confirmed one.
	 */
	recovery_state->lsn = recovery_state->confirmed_lsn = replica.lsn - 1;
	recover_existing_wals(recovery_state);
	/* Found nothing. */
	if (recovery_state->lsn == replica.lsn - 1)
		say_error("can't find WAL containing record with lsn: %" PRIi64,
			  replica.lsn);
	recovery_follow_local(recovery_state, 0.1);

	ev_run(loop(), 0);

	say_crit("exiting the relay loop");
	exit(EXIT_SUCCESS);
}
