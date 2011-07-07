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
#include <replicator.h>
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


/** replicator process context struct */
struct replicator_process {
	/** communication socket. */
	int sock;
	/** waitpid need */
	bool need_waitpid;
	/** replicator is done */
	bool is_done;
	/** child process counts */
	u32 child_count;
};

static int replicator_socks[2];

/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/**
 * Replication acceptor fiber handler.
 */
static void
acceptor_handler(void *data);

/**
 * Replication sender fiber.
 */
static void
sender_handler(void *data);

/**
 * Read sender's fiber inbox.
 *
 * @return On success, a file descriptor is returned. On error, -1 is returned.
 */
static int
sender_read_inbox();

/**
 * Send file descriptor to replicator process.
 *
 * @param fd the sending file descriptor.
 */
static void
sender_send_sock(int sock);


/*-----------------------------------------------------------------------------*/
/* replicator process                                                          */
/*-----------------------------------------------------------------------------*/

/** replicator process context */
static struct replicator_process replicator_process;

/**
 * Initialize replicator spawner process.
 *
 * @param sock the socket between tarantool and replicator.
 */
static void
spawner_init(int sock);

/**
 * Replicator spawner process main loop.
 */
static void
spawner_main_loop();

/**
 * Replicator spawner shutdown.
 */
static void
spawner_shutdown();

/**
 * Replicator's spawner process signals handler.
 *
 * @param signal is signal number.
 */
static void
spawner_signals_handler(int signal);

/**
 * Process waitpid childs.
 */
static void
spawner_process_wait_childs();

/**
 * Receive replication client socket from main process.
 *
 * @return On success, a zero is returned. On error, -1 is returned.
 */
static int
spawner_recv_client_sock(int *client_sock);

/**
 * Create replicator's client handler process.
 *
 * @return On success, a zero is returned. On error, -1 is returned.
 */
static int
spawner_create_client_handler(int client_sock);

/**
 * Replicator spawner shutdown: kill childs.
 */
static void
spawner_shutdown_kill_childs();

/**
 * Replicator spawner shutdown: wait childs.
 */
static int
spawner_shutdown_wait_childs();

/**
 * Initialize replicator's service process.
 */
static void
client_handler_init(int client_sock);

/**
 * Receive data event to replication socket handler
 */
static void
client_handler_recv(struct ev_io *w, int revents);

/**
 * Send to row to client.
 */
static int
client_handler_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t);


/*-----------------------------------------------------------------------------*/
/* replicatior module                                                          */
/*-----------------------------------------------------------------------------*/

/** Check replicator module configuration. */
u32
replicator_check_config(struct tarantool_cfg *config)
{
	if (config->replication_port == 0) {
		/* port not defined or setted to zero, replicator disabled */
		return 0;
	}

	if (config->replication_port < 0 ||
	    config->replication_port >= 65536) {
		say_error("replicator: invalid port: %"PRId32, config->replication_port);
		return -1;
	}

	return 0;
}

/** Reload replicator module configuration. */
void
replicator_reload_config(struct tarantool_cfg *config __attribute__((unused)))
{}

/** Pre-fork replicator spawner process. */
void
replicator_prefork()
{
	if (cfg.replication_port == 0) {
		/* replicator not needed, leave init function */
		return;
	}

	if (cfg.replicator_custom_proc_title == NULL) {
		cfg.replicator_custom_proc_title = "";
	}

	/* create communication sockes between tarantool and replicator processes via UNIX sockets*/
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, replicator_socks) != 0) {
		panic_syserror("socketpair");
	}

	/* create replicator process */
	pid_t pid = fork();
	if (pid == -1) {
		panic_syserror("fork");
	}

	if (pid != 0) {
		/* parent process: tarantool */
		close(replicator_socks[1]);
	} else {
		/* child process: replicator */
		close(replicator_socks[0]);
		spawner_init(replicator_socks[1]);
	}
}

/** Intialize tarantool's replicator module. */
void
replicator_init()
{
	if (cfg.replication_port == 0) {
		/* replicator not needed, leave init function */
		return;
	}

	char fiber_name[FIBER_NAME_MAXLEN];
	/* create sender fiber */
	if (snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication sender", cfg.replication_port) < 0) {
		panic("snprintf fail");
	}

	const size_t sender_inbox_size = 16 * sizeof(int);
	struct fiber *sender = fiber_create(fiber_name, replicator_socks[0], sender_inbox_size, sender_handler, NULL);
	if (sender == NULL) {
		panic("create fiber fail");
	}

	/* create acceptor fiber */
	if (snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication acceptor", cfg.replication_port) < 0) {
		panic("snprintf fail");
	}

	struct fiber *acceptor = fiber_create(fiber_name, -1, -1, acceptor_handler, sender);
	if (acceptor == NULL) {
		panic("create fiber fail");
	}

	fiber_call(acceptor);
	fiber_call(sender);
}


/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/** Replication acceptor fiber handler. */
static void
acceptor_handler(void *data)
{
	struct fiber *sender = (struct fiber *) data;
	struct tbuf *msg;

	if (fiber_serv_socket(fiber, cfg.replication_port, true, 0.1) != 0) {
		panic("can not bind replication port");
	}

	msg = tbuf_alloc(fiber->pool);

	for (;;) {
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int client_sock = -1;

		/* wait new connection request */
		fiber_io_start(EV_READ);
		fiber_io_yield();
		/* accept connection */
		client_sock = accept(fiber->fd, &addr, &addrlen);
		if (client_sock == -1) {
			if (errno == EAGAIN && errno == EWOULDBLOCK) {
				continue;
			}
			panic_syserror("accept");
		}
		fiber_io_stop(EV_READ);
		say_info("connection from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

		/* put descriptor to message and send to sender fiber */
		tbuf_append(msg, &client_sock, sizeof(client_sock));
		write_inbox(sender, msg);
		/* clean-up buffer & wait while sender fiber read message */
		tbuf_reset(msg);
	}
}

/** Replication sender fiber. */
static void
sender_handler(void *data __attribute__((unused)))
{
	if (set_nonblock(fiber->fd) == -1) {
		panic("set nonblock fail");
	}

	while (true) {
		int client_sock;

		client_sock = sender_read_inbox();
		if (client_sock == -1) {
			continue;
		}

		sender_send_sock(client_sock);
	}
}

/** Read sender's fiber inbox. */
static int
sender_read_inbox()
{
	struct msg *message;

	message = read_inbox();
	if (message->msg->len != sizeof(int)) {
		say_error("invalid message length %"PRId32, message->msg->len);
		return -1;
	}

	return *((int *) message->msg->data);
}

/** Send file descriptor to replicator process. */
static void
sender_send_sock(int client_sock)
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

	/* wait, when interprocess comm. socke will ready for write */
	fiber_io_start(EV_WRITE);
	fiber_io_yield();
	/* send client socket to replicator porcess */
	if (sendmsg(fiber->fd, &msg, 0) < 0) {
		say_syserror("sendmsg");
	}
	fiber_io_stop(EV_WRITE);
	/* close client sock in the main process */
	close(client_sock);
}


/*-----------------------------------------------------------------------------*/
/* replicator process                                                          */
/*-----------------------------------------------------------------------------*/

/** Initialize replicator spawner process. */
static void
spawner_init(int sock)
{
	char name[sizeof(fiber->name)];
	struct sigaction sa;

	if (cfg.replicator_custom_proc_title == NULL) {
		cfg.replicator_custom_proc_title = "";
	}

	snprintf(name, sizeof(name), "replicator%s/spawner", cfg.replicator_custom_proc_title);
	fiber_set_name(fiber, name);
	set_proc_title(name);

	/* init replicator process context */
	memset(&replicator_process, 0, sizeof(replicator_process));
	replicator_process.sock = sock;
	replicator_process.need_waitpid = false;
	replicator_process.is_done = false;
	replicator_process.child_count = 0;

	/* init signals */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;

	/* ignore SIGPIPE */
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		say_syserror("sigaction SIGPIPE");
	}

	/* set handler for signals: SIGHUP, SIGINT, SIGTERM and SIGCHLD */
	sa.sa_handler = spawner_signals_handler;

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
	while (!replicator_process.is_done) {
		int client_sock;

		if (replicator_process.need_waitpid) {
			spawner_process_wait_childs();
		}

		if (spawner_recv_client_sock(&client_sock) != 0) {
			continue;
		}

		if (client_sock > 0) {
			spawner_create_client_handler(client_sock);
		}
	}

	spawner_shutdown();
}

/** Replicator spawner shutdown. */
static void
spawner_shutdown()
{
	say_info("shutdown");

	/* kill all childs */
	spawner_shutdown_kill_childs();
	/* close socket */
	close(replicator_process.sock);

	exit(EXIT_SUCCESS);
}

/** Replicator's spawner process signals handler. */
static void spawner_signals_handler(int signal)
{
	switch (signal) {
	case SIGHUP:
	case SIGINT:
	case SIGTERM:
		replicator_process.is_done = true;
		break;
	case SIGCHLD:
		replicator_process.need_waitpid = true;
		break;
	}
}

/** Process waitpid childs. */
static void
spawner_process_wait_childs()
{
	while (replicator_process.child_count > 0) {
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
		replicator_process.child_count--;
	}

	replicator_process.need_waitpid = false;
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

	if (recvmsg(replicator_process.sock, &msg, 0) < 0) {
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
spawner_create_client_handler(int client_sock)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		say_syserror("fork");
		return -1;
	}

	if (pid == 0) {
		client_handler_init(client_sock);
	} else {
		say_info("replicator client handler spawned: pid = %d", pid);
		replicator_process.child_count++;
		close(client_sock);
	}

	return 0;
}

/** Replicator spawner shutdown: kill childs. */
static void
spawner_shutdown_kill_childs()
{
	int result = 0;

	/* check child process count */
	if (replicator_process.child_count == 0) {
		return;
	}

	/* send terminate signals to childs */
	say_info("send SIGTERM to %"PRIu32" childs", replicator_process.child_count);
	result = kill(0, SIGTERM);
	if (result != 0) {
		say_syserror("kill");
		return;
	}

	/* wait when process is down */
	result = spawner_shutdown_wait_childs();
	if (result != 0) {
		return;
	}

	/* check child process count */
	if (replicator_process.child_count == 0) {
		say_info("all childs terminated");
		return;
	}
	say_info("%"PRIu32" childs still alive", replicator_process.child_count);

	/* send terminate signals to childs */
	say_info("send SIGKILL to %"PRIu32" childs", replicator_process.child_count);
	result = kill(0, SIGKILL);
	if (result != 0) {
		say_syserror("kill");
		return;
	}

	/* wait when process is down */
	result = spawner_shutdown_wait_childs();
	if (result != 0) {
		return;
	}
	say_info("all childs terminated");
}

/** Replicator spawner shutdown: wait childs. */
static int
spawner_shutdown_wait_childs()
{
	const u32 wait_sec = 5;
	struct timeval tv;

	say_info("wait for childs %"PRIu32" seconds", wait_sec);

	tv.tv_sec = wait_sec;
	tv.tv_usec = 0;

	/* wait childs process */
	spawner_process_wait_childs();
	while (replicator_process.child_count > 0) {
		int result;

		/* wait EINTR or timeout */
		result = select(0, NULL, NULL, NULL, &tv);
		if (result < 0 && errno != EINTR) {
			/* this is not signal */
			say_syserror("select");
			return - 1;
		}

		/* wait childs process */
		spawner_process_wait_childs();

		/* check timeout */
		if (tv.tv_sec == 0 && tv.tv_usec == 0) {
			/* timeout happen */
			break;
		}
	}

	return 0;
}

/** Initialize replicator's service process. */
static void
client_handler_init(int client_sock)
{
	char name[sizeof(fiber->name)];
	struct sigaction sa;
	struct recovery_state *log_io;
	struct tbuf *ver;
	i64 lsn;
	ssize_t r;

	fiber->has_peer = true;
	fiber->fd = client_sock;

	/* set replicator name */
	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "replicator%s/handler", cfg.replicator_custom_proc_title);
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
	client_handler_send_row(NULL, ver);

	/* init libev events handlers */
	ev_default_loop(0);

	/* init read events */
	struct ev_io sock_read_ev;
	int sock_read_fd = fiber->fd;
	sock_read_ev.data = (void *)&sock_read_fd;
	ev_io_init(&sock_read_ev, client_handler_recv, sock_read_fd, EV_READ);
	ev_io_start(&sock_read_ev);

	/* init reovery porcess */
	log_io = recover_init(NULL, cfg.wal_dir,
			      client_handler_send_row, INT32_MAX, 0, 64, RECOVER_READONLY, false);

	recover(log_io, lsn);
	recover_follow(log_io, 0.1);

	ev_loop(0);

	say_crit("leave loop");
	exit(EXIT_SUCCESS);
}

/** Receive data event to replication socket handler */
static void
client_handler_recv(struct ev_io *w, int __attribute__((unused)) revents)
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
client_handler_send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
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

