/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <replication.h>
#include <say.h>
#include <fiber.h>
#include TARANTOOL_CONFIG
#include <palloc.h>
#include <stddef.h>

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "fiber.h"

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
 * The master process binds to replication_port and accepts
 * incoming connections. This is done in the master to be able to
 * correctly handle RELOAD CONFIGURATION, which happens in the
 * master, and, in future, perform authentication of replication
 * clients. Since the master uses fibers to serve all clients,
 * replication acceptor fiber is just one of many fibers in use.
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
static int master_to_spawner_sock;

/** replication_port acceptor fiber */
static void
acceptor_handler(void *data __attribute__((unused)));

/** Send a file descriptor to replication relay spawner.
 *
 * @param client_sock the file descriptor to be sent.
 */
static void
acceptor_send_sock(int client_sock);

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

/** Handle SIGINT, SIGTERM, SIGPIPE, SIGHUP. */
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
spawner_create_replication_relay(int client_sock);

/** Shut down all relays when shutting down the spawner. */
static void
spawner_shutdown_children();

/** Initialize replication relay process. */
static void
replication_relay_loop(int client_sock);

/** A libev callback invoked when a relay client socket is ready
 * for read. This currently only happens when the client closes
 * its socket, and we get an EOF.
 */
static void
replication_relay_recv(struct ev_io *w, int revents);

/** Send a single row to the client. */
static int
replication_relay_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t);


/*
 * ------------------------------------------------------------------------
 * replication module
 * ------------------------------------------------------------------------
 */

/** Check replication module configuration. */
int
replication_check_config(struct tarantool_cfg *config)
{
	if (config->replication_port < 0 ||
	    config->replication_port >= USHRT_MAX) {
		say_error("invalid replication port value: %"PRId32,
			  config->replication_port);
		return -1;
	}

	return 0;
}

/** Pre-fork replication spawner process. */
void
replication_prefork()
{
	if (cfg.replication_port == 0) {
		/* replication is not needed, do nothing */
		return;
	}
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
		master_to_spawner_sock = sockpair[0];
		if (set_nonblock(master_to_spawner_sock) == -1)
			panic("set_nonblock");
	} else {
		ev_default_fork();
		ev_loop(EVLOOP_NONBLOCK);
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

/**
 * Create a fiber which accepts client connections and pushes them
 * to replication spawner.
 */

void
replication_init()
{
	if (cfg.replication_port == 0)
		return;                        /* replication is not in use */

	char fiber_name[FIBER_NAME_MAXLEN];

	/* create acceptor fiber */
	snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication", cfg.replication_port);

	struct fiber *acceptor = fiber_create(fiber_name, -1, acceptor_handler, NULL);

	if (acceptor == NULL) {
		panic("create fiber fail");
	}

	fiber_call(acceptor);
}


/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/** Replication acceptor fiber handler. */
static void
acceptor_handler(void *data __attribute__((unused)))
{
	if (fiber_serv_socket(fiber, cfg.replication_port, true, 0.1) != 0) {
		panic("can not bind to replication port");
	}

	for (;;) {
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int client_sock = -1;

		/* wait new connection request */
		fiber_io_start(fiber->fd, EV_READ);
		fiber_io_yield();

		/* accept connection */
		client_sock = accept(fiber->fd, (struct sockaddr*)&addr,
				     &addrlen);
		if (client_sock == -1) {
			if (errno == EAGAIN && errno == EWOULDBLOCK) {
				continue;
			}
			panic_syserror("accept");
		}

		/* up SO_KEEPALIVE flag */
		int keepalive = 1;
		if (setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE,
			       &keepalive, sizeof(int)) < 0)
			/* just print error, it's not critical error */
			say_syserror("setsockopt()");

		fiber_io_stop(fiber->fd, EV_READ);
		say_info("connection from %s:%d", inet_ntoa(addr.sin_addr),
			 ntohs(addr.sin_port));
		acceptor_send_sock(client_sock);
	}
}


/** Send a file descriptor to the spawner. */
static void
acceptor_send_sock(int client_sock)
{
	struct msghdr msg;
	struct iovec iov[1];
	char control_buf[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *control_message = NULL;
	int cmd_code = 0;

	iov[0].iov_base = &cmd_code;
	iov[0].iov_len = sizeof(cmd_code);

	memset(&msg, 0, sizeof(msg));

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	control_message = CMSG_FIRSTHDR(&msg);
	control_message->cmsg_len = CMSG_LEN(sizeof(int));
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	*((int *) CMSG_DATA(control_message)) = client_sock;

	/* wait, when interprocess comm. socket is ready for write */
	fiber_io_start(master_to_spawner_sock, EV_WRITE);
	fiber_io_yield();
	/* send client socket to the spawner */
	if (sendmsg(master_to_spawner_sock, &msg, 0) < 0)
		say_syserror("sendmsg");

	fiber_io_stop(master_to_spawner_sock, EV_WRITE);
	/* close client socket in the main process */
	close(client_sock);
}


/*-----------------------------------------------------------------------------*/
/* spawner process                                                             */
/*-----------------------------------------------------------------------------*/

/** Initialize the spawner. */

static void
spawner_init(int sock)
{
	char name[sizeof(fiber->name)];
	struct sigaction sa;

	snprintf(name, sizeof(name), "spawner%s", custom_proc_title);
	fiber_set_name(fiber, name);
	set_proc_title(name);

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
	    sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGPIPE, &sa, NULL) == -1)
		say_syserror("sigaction");

	sa.sa_handler = spawner_sigchld_handler;

	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		say_syserror("sigaction");

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
	struct iovec iov[1];
	char control_buf[CMSG_SPACE(sizeof(int))];
	int cmd_code = 0;
	int client_sock;

	iov[0].iov_base = &cmd_code;
	iov[0].iov_len = sizeof(cmd_code);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	while (!spawner.killed) {
		int msglen = recvmsg(spawner.sock, &msg, 0);
		if (msglen > 0) {
			client_sock = spawner_unpack_cmsg(&msg);
			spawner_create_replication_relay(client_sock);
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
spawner_create_replication_relay(int client_sock)
{
	pid_t pid = fork();

	if (pid < 0) {
		say_syserror("fork");
		return -1;
	}

	if (pid == 0) {
		ev_default_fork();
		ev_loop(EVLOOP_NONBLOCK);
		close(spawner.sock);
		replication_relay_loop(client_sock);
	} else {
		spawner.child_count++;
		close(client_sock);
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

	say_info("sending signal %d to %"PRIu32" children", kill_signo,
		 (u32) spawner.child_count);

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

/** The main loop of replication client service process. */
static void
replication_relay_loop(int client_sock)
{
	char name[FIBER_NAME_MAXLEN];
	struct sigaction sa;
	struct recovery_state *log_io;
	struct tbuf *ver;
	i64 lsn;
	ssize_t r;

	fiber->has_peer = true;
	fiber->fd = client_sock;

	/* set process title and fiber name */
	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "relay/%s", fiber_peer_name(fiber));
	fiber_set_name(fiber, name);
	set_proc_title("%s%s", name, custom_proc_title);

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

	/* Block SIGPIPE, we already handle EPIPE. */
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
		say_syserror("sigaction");

	r = read(fiber->fd, &lsn, sizeof(lsn));
	if (r != sizeof(lsn)) {
		if (r < 0) {
			panic_syserror("read");
		}
		panic("invalid LSN request size: %zu", r);
	}
	say_info("starting recovery from lsn:%"PRIi64, lsn);

	ver = tbuf_alloc(fiber->gc_pool);
	tbuf_append(ver, &default_version, sizeof(default_version));
	replication_relay_send_row(NULL, ver);

	/* init libev events handlers */
	ev_default_loop(0);

	/* init read events */
	struct ev_io sock_read_ev;
	int sock_read_fd = fiber->fd;
	sock_read_ev.data = (void *)&sock_read_fd;
	ev_io_init(&sock_read_ev, replication_relay_recv, sock_read_fd, EV_READ);
	ev_io_start(&sock_read_ev);

	/* Initialize the recovery process */
	recovery_init(NULL, cfg.wal_dir, replication_relay_send_row,
		      INT32_MAX, "fsync_delay", 0,
		      RECOVER_READONLY, false);

	log_io = recovery_state;

	recover(log_io, lsn);
	recover_follow(log_io, 0.1);

	ev_loop(0);

	say_crit("exiting the relay loop");
	exit(EXIT_SUCCESS);
}

/** Receive data event to replication socket handler */
static void
replication_relay_recv(struct ev_io *w, int __attribute__((unused)) revents)
{
	int fd = *((int *)w->data);
	u8 data;

	int result = recv(fd, &data, sizeof(data), 0);

	if (result == 0 || (result < 0 && errno == ECONNRESET)) {
		say_info("the client has closed its replication socket, exiting");
		exit(EXIT_SUCCESS);
	}
	if (result < 0)
		say_syserror("recv");

	exit(EXIT_FAILURE);
}

/** Send to row to client. */
static int
replication_relay_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	u8 *data = t->data;
	ssize_t bytes, len = t->size;
	while (len > 0) {
		bytes = write(fiber->fd, data, len);
		if (bytes < 0) {
			if (errno == EPIPE) {
				/* socket closed on opposite site */
				goto shutdown_handler;
			}
			panic_syserror("write");
		}
		len -= bytes;
		data += bytes;
	}

	say_debug("send row: %" PRIu32 " bytes %s", t->size, tbuf_to_hex(t));
	return 0;
shutdown_handler:
	say_info("the client has closed its replication socket, exiting");
	exit(EXIT_SUCCESS);
}

