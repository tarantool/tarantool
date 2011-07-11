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
 * with the spawner using a socketpair(2).
 *
 * The master process binds to replication_port and accepts
 * incoming connections. We do it in the master to be able to
 * correctly handle RELOAD CONFIGURATION, which happens in the
 * master, and, in future, perform authentication of replication
 * clients. Sine the master uses fibers to serve all clients,
 * acceptor fiber is just one of the many fibers in use. Once
 * a client socket is accepted, it is sent to the spawner process,
 * through the master's end of the socket pair.
 *
 * The spawner listens on the receiving end of the socket pair and
 * for every received socket creates a replication relay, which is
 * then responsible for sending write ahead logs to the replica.
 */

static int replication_socks[2];

/** replication_port acceptor fiber */
static void
acceptor_handler(void *data);

/** Send file descriptor to replication relay spawner.
 *
 * @param client_sock the file descriptor to be sent.
 */
static void
acceptor_send_sock(int client_sock);

/** Replication spawner process */
static struct spawner {
	/** communication socket. */
	int sock;
	/** waitpid need */
	bool need_waitpid;
	/** got signal */
	bool is_done;
	/** child process counts */
	u32 child_count;
} spawner;

/** Initialize spawner process.
 *
 * @param sock the socket between the main process and the spawner.
 */
static void
spawner_init(int sock);

/**
 * Spawner main loop.
 */
static void
spawner_main_loop();

/**
 * Shutdown spawner and all its children.
 */
static void
spawner_shutdown();

/** Replication spawner process signal handler.
 *
 * @param signal is signal number.
 */
static void
spawner_signal_handler(int signal);

/**
 * Process waitpid children.
 */
static void
spawner_wait_children();

/**
 * Receive replication client socket from main process.
 *
 * @return On success, a zero is returned. On error, -1 is returned.
 */
static int
spawner_recv_client_sock(int *client_sock);

/** Create a replication relay.
 *
 * @return 0 on success, -1 on error
 */
static int
spawner_create_replication_relay(int client_sock);

/** Replicator spawner shutdown: kill children. */
static void
spawner_shutdown_kill_children();

/** * Replicator spawner shutdown: wait children.  */
static int
spawner_shutdown_wait_children();

/** Initialize replication relay process. */
static void
replication_relay_loop(int client_sock);

/** * Receive data event to replication socket handler. */
static void
replication_relay_recv(struct ev_io *w, int revents);

/** * Send a single row to the client. */
static int
replication_relay_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t);


/*-----------------------------------------------------------------------------*/
/* replication module                                                          */
/*-----------------------------------------------------------------------------*/

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
	/*
	 * Create UNIX sockets to communicate between the main and
	 * spawner processes.
         */
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, replication_socks) != 0)
		panic_syserror("socketpair");

	if (set_nonblock(replication_socks[0]) == -1)
		panic("set nonblock fail");

	/* create spawner */
	pid_t pid = fork();
	if (pid == -1)
		panic_syserror("fork");

	if (pid != 0) {
		/* parent process: tarantool */
		close(replication_socks[1]);
	} else {
		/* child process: spawner */
		close(replication_socks[0]);
		spawner_init(replication_socks[1]);
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
		return;                         /* replication is not in use */

	char fiber_name[FIBER_NAME_MAXLEN];

	/* create acceptor fiber */
	snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication", cfg.replication_port);

	struct fiber *acceptor = fiber_create(fiber_name, -1, -1, acceptor_handler, NULL);

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
		client_sock = accept(fiber->fd, &addr, &addrlen);
		if (client_sock == -1) {
			if (errno == EAGAIN && errno == EWOULDBLOCK) {
				continue;
			}
			panic_syserror("accept");
		}
		fiber_io_stop(fiber->fd, EV_READ);
		say_info("connection from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		acceptor_send_sock(client_sock);
	}
}


/** Send file descriptor to the spawner. */
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
	fiber_io_start(replication_socks[0], EV_WRITE);
	fiber_io_yield();
	/* send client socket to the spawner */
	if (sendmsg(replication_socks[0], &msg, 0) < 0)
		say_syserror("sendmsg");

	fiber_io_stop(replication_socks[0], EV_WRITE);
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

	snprintf(name, sizeof(name), "replication%s/spawner", custom_proc_title);
	fiber_set_name(fiber, name);
	set_proc_title(name);

	/* init replicator process context */
	memset(&spawner, 0, sizeof(spawner));
	spawner.sock = sock;
	spawner.need_waitpid = false;
	spawner.is_done = false;
	spawner.child_count = 0;

	/* init signal */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;

	/* ignore SIGPIPE */
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		say_syserror("sigaction SIGPIPE");
	}

	/* set handler for signals: SIGHUP, SIGINT, SIGTERM and SIGCHLD */
	sa.sa_handler = spawner_signal_handler;

	if ((sigaction(SIGHUP, &sa, NULL) == -1) ||
	    (sigaction(SIGINT, &sa, NULL) == -1) ||
	    (sigaction(SIGTERM, &sa, NULL) == -1) ||
	    (sigaction(SIGCHLD, &sa, NULL) == -1)) {
		say_syserror("sigaction");
	}

	say_crit("initialized");
	spawner_main_loop();
}

/** Replicator spawner process main loop. */
static void
spawner_main_loop()
{
	while (!spawner.is_done) {
		int client_sock;

		if (spawner.need_waitpid) {
			spawner_wait_children();
		}

		if (spawner_recv_client_sock(&client_sock) != 0) {
			continue;
		}

		if (client_sock > 0) {
			spawner_create_replication_relay(client_sock);
		}
	}

	spawner_shutdown();
}

/** Replicator spawner shutdown. */
static void
spawner_shutdown()
{
	say_info("shutdown");

	/* kill all children */
	spawner_shutdown_kill_children();
	/* close socket */
	close(spawner.sock);

	exit(EXIT_SUCCESS);
}

/** Replicator's spawner process signal handler. */
static void spawner_signal_handler(int signal)
{
	switch (signal) {
	case SIGHUP:
	case SIGINT:
	case SIGTERM:
		spawner.is_done = true;
		break;
	case SIGCHLD:
		spawner.need_waitpid = true;
		break;
	}
}

/** Process waitpid children. */
static void
spawner_wait_children()
{
	while (spawner.child_count > 0) {
		int exit_status;
		pid_t pid;

		pid = waitpid(-1, &exit_status, WNOHANG);
		if (pid < 0) {
			say_syserror("waitpid");
			break;
		}

		if (pid == 0) {
			/* done, all finished process are processed */
			break;
		}

		say_info("child finished: pid = %d, exit status = %d", pid, WEXITSTATUS(exit_status));
		spawner.child_count--;
	}

	spawner.need_waitpid = false;
}

/** Receive replication client socket from main process. */
static int
spawner_recv_client_sock(int *client_sock)
{
	struct msghdr msg;
	struct iovec iov[1];
	char control_buf[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *control_message = NULL;
	int cmd_code = 0;

	iov[0].iov_base = &cmd_code;
	iov[0].iov_len = sizeof(cmd_code);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	if (recvmsg(spawner.sock, &msg, 0) < 0) {
		if (errno == EINTR) {
			*client_sock = 0;
			return 0;
		}
		say_syserror("recvmsg");
		return -1;
	}

	for (control_message = CMSG_FIRSTHDR(&msg);
	     control_message != NULL;
	     control_message = CMSG_NXTHDR(&msg, control_message)) {
		if ((control_message->cmsg_level == SOL_SOCKET) &&
		    (control_message->cmsg_type == SCM_RIGHTS)) {
			*client_sock = *((int *) CMSG_DATA(control_message));
		}
	}

	return 0;
}

/** Create replicator's client handler process. */
static int
spawner_create_replication_relay(int client_sock)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		say_syserror("fork");
		return -1;
	}

	if (pid == 0) {
		replication_relay_loop(client_sock);
	} else {
		say_info("replicator client handler spawned: pid = %d", pid);
		spawner.child_count++;
		close(client_sock);
	}

	return 0;
}

/** Replicator spawner shutdown: kill children. */
static void
spawner_shutdown_kill_children()
{
	int result = 0;

	/* check child process count */
	if (spawner.child_count == 0) {
		return;
	}

	/* send terminate signal to children */
	say_info("send SIGTERM to %"PRIu32" children", spawner.child_count);
	result = kill(0, SIGTERM);
	if (result != 0) {
		say_syserror("kill");
		return;
	}

	/* wait when process is down */
	result = spawner_shutdown_wait_children();
	if (result != 0) {
		return;
	}

	/* check child process count */
	if (spawner.child_count == 0) {
		say_info("all children terminated");
		return;
	}
	say_info("%"PRIu32" children still alive", spawner.child_count);

	/* send terminate signal to children */
	say_info("send SIGKILL to %"PRIu32" children", spawner.child_count);
	result = kill(0, SIGKILL);
	if (result != 0) {
		say_syserror("kill");
		return;
	}

	/* wait when process is down */
	result = spawner_shutdown_wait_children();
	if (result != 0) {
		return;
	}
	say_info("all children terminated");
}

/** Replicator spawner shutdown: wait children. */
static int
spawner_shutdown_wait_children()
{
	const u32 wait_sec = 5;
	struct timeval tv;

	say_info("wait for children %"PRIu32" seconds", wait_sec);

	tv.tv_sec = wait_sec;
	tv.tv_usec = 0;

	/* wait children process */
	spawner_wait_children();
	while (spawner.child_count > 0) {
		int result;

		/* wait EINTR or timeout */
		result = select(0, NULL, NULL, NULL, &tv);
		if (result < 0 && errno != EINTR) {
			/* this is not signal */
			say_syserror("select");
			return - 1;
		}

		/* wait children process */
		spawner_wait_children();

		/* check timeout */
		if (tv.tv_sec == 0 && tv.tv_usec == 0) {
			/* timeout happen */
			break;
		}
	}

	return 0;
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
	snprintf(name, sizeof(name), "replication%s/relay", custom_proc_title);
	fiber_set_name(fiber, name);
	set_proc_title("%s %s", name, fiber_peer_name(fiber));

	/* init signals */
	memset(&sa, 0, sizeof(sa));

	/* ignore SIGPIPE and SIGCHLD */
	sa.sa_handler = SIG_IGN;
	if ((sigaction(SIGPIPE, &sa, NULL) == -1) ||
	    (sigaction(SIGCHLD, &sa, NULL) == -1)) {
		say_syserror("sigaction");
	}

	/* return signals SIGHUP, SIGINT and SIGTERM to default value */
	sa.sa_handler = SIG_DFL;

	if ((sigaction(SIGHUP, &sa, NULL) == -1) ||
	    (sigaction(SIGINT, &sa, NULL) == -1) ||
	    (sigaction(SIGTERM, &sa, NULL) == -1)) {
		say_syserror("sigaction");
	}

	r = read(fiber->fd, &lsn, sizeof(lsn));
	if (r != sizeof(lsn)) {
		if (r < 0) {
			panic_syserror("read");
		}
		panic("invalid lns request size: %zu", r);
	}
	say_info("start recover from lsn:%"PRIi64, lsn);

	ver = tbuf_alloc(fiber->pool);
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

	/* init reovery porcess */
	log_io = recover_init(NULL, cfg.wal_dir,
			      replication_relay_send_row, INT32_MAX, 0, 64, RECOVER_READONLY, false);

	recover(log_io, lsn);
	recover_follow(log_io, 0.1);

	ev_loop(0);

	say_crit("leave loop");
	exit(EXIT_SUCCESS);
}

/** Receive data event to replication socket handler */
static void
replication_relay_recv(struct ev_io *w, int __attribute__((unused)) revents)
{
	int fd = *((int *)w->data);
	u8 data;

	int result = recv(fd, &data, sizeof(data), 0);
	if (result < 0) {
		if (errno == ECONNRESET) {
			goto shutdown_handler;
		}
		say_syserror("recv");
		exit(EXIT_FAILURE);
	} else if (result == 0) {
		/* end-of-input */
		goto shutdown_handler;
	}

	exit(EXIT_FAILURE);
shutdown_handler:
	say_info("replication socket closed on opposite side, exit");
	exit(EXIT_SUCCESS);
}

/** Send to row to client. */
static int
replication_relay_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	u8 *data = t->data;
	ssize_t bytes, len = t->len;
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

	say_debug("send row: %" PRIu32 " bytes %s", t->len, tbuf_to_hex(t));
	return 0;
shutdown_handler:
	say_info("replication socket closed on opposite side, exit");
	exit(EXIT_SUCCESS);
}

