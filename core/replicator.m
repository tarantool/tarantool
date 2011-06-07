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


/** replicator process context struct */
struct replicator_process {
	/** communication socket. */
	int sock;
	/** waitpid need */
	bool need_waitpid;
	/** replicator is done */
	bool is_done;
};


/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/**
 * Initialize replication fibers.
 */
static void
fibers_init(int sock);

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
 *
 */
static void
spawner_main_loop();

/**
 * Replicator's spawner process signals handler.
 *
 * @param signal is signal number.
 */
static void
spawner_signals_handler(int signal);

/**
 * Process finished childs.
 */
static void
spawner_process_finished_childs();

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
 * Initialize replicator's service process.
 */
static void
client_handler_init(int client_sock);

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

/** Intialize tarantool's replicator module. */
void
replicator_init(void)
{
	int socks[2];
	pid_t pid = -1;

	if (cfg.replication_port == 0) {
		/* replicator not needed, leave init function */
		return;
	}

	if (cfg.replicator_custom_proc_title == NULL) {
		cfg.replicator_custom_proc_title = "";
	}

	/* create communication sockes between tarantool and replicator processes via UNIX sockets*/
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, socks) != 0) {
		panic_syserror("socketpair");
	}

	/* create replicator process */
	pid = fork();
	if (pid == -1) {
		panic("fork");
	}

	if (pid != 0) {
		/* parent process: tarantool */
		close(socks[1]);
		fibers_init(socks[0]);
	} else {
		/* child process: replicator */
		close(socks[0]);
		spawner_init(socks[1]);
	}
}


/*-----------------------------------------------------------------------------*/
/* replication accept/sender fibers                                            */
/*-----------------------------------------------------------------------------*/

/** Initialize replication fibers. */
static void
fibers_init(int sock)
{
	char fiber_name[FIBER_NAME_MAXLEN];
	const size_t sender_inbox_size = 16 * sizeof(int);
	struct fiber *acceptor = NULL;
	struct fiber *sender = NULL;

	/* create sender fiber */
	if (snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication sender", cfg.replication_port) < 0) {
		panic("snprintf fail");
	}

	sender = fiber_create(fiber_name, sock, sender_inbox_size, sender_handler, NULL);
	if (sender == NULL) {
		panic("create fiber fail");
	}

	/* create acceptor fiber */
	if (snprintf(fiber_name, FIBER_NAME_MAXLEN, "%i/replication acceptor", cfg.replication_port) < 0) {
		panic("snprintf fail");
	}

	acceptor = fiber_create(fiber_name, -1, -1, acceptor_handler, sender);
	if (acceptor == NULL) {
		panic("create fiber fail");
	}

	fiber_call(acceptor);
	fiber_call(sender);
}

/** Replication acceptor fiber handler. */
static void
acceptor_handler(void *data)
{
	struct fiber *sender = (struct fiber *) data;
	struct tbuf *msg;

	if (fiber_serv_socket(fiber, tcp_server, cfg.replication_port, false, 0.0) != 0) {
		panic("can not bind replication port");
	}

	msg = tbuf_alloc(fiber->pool);

	while (true) {
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int client_sock = -1;

		/* wait new connection request */
		wait_for(EV_READ);

		/* accept connection */
		client_sock = accept(fiber->fd, &addr, &addrlen);
		if (client_sock == -1) {
			if (errno == EAGAIN && errno == EWOULDBLOCK) {
				continue;
			}
			panic_syserror("accept");
		}
		say_info("connection from %s:%d", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

		/* put descriptor to message and send to sender fiber */
		tbuf_append(msg, &client_sock, sizeof(client_sock));
		write_inbox(sender, msg);
		/* clean-up buffer & wait while sender fiber read message */
		tbuf_reset(msg);
		wait_inbox(sender);
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

	if (sendmsg(fiber->fd, &msg, 0) < 0) {
		say_syserror("sendmsg");
	}

	wait_for(EV_WRITE);
	/* close file descriptor in the main process */
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
			spawner_process_finished_childs();
		}

		if (spawner_recv_client_sock(&client_sock) != 0) {
			continue;
		}

		if (client_sock > 0) {
			spawner_create_client_handler(client_sock);
		}
	}

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

/** Process finished childs. */
static void
spawner_process_finished_childs()
{
	while (true) {
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

		say_debug("child finished: pid = %d, exit status = %d", pid, exit_status);
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
		close(client_sock);
	}

	return 0;
}

/** Initialize replicator's service process. */
static void
client_handler_init(int client_sock)
{
	char name[sizeof(fiber->name)];
	struct recovery_state *log_io;
	struct tbuf *ver;
	i64 lsn;
	ssize_t r;

	fiber->has_peer = true;
	fiber->fd = client_sock;

	memset(name, 0, sizeof(name));
	snprintf(name, sizeof(name), "replicator%s/handler", cfg.replicator_custom_proc_title);
	fiber_set_name(fiber, name);
	set_proc_title("%s %s", name, fiber_peer_name(fiber));

	ev_default_loop(0);

	r = read(fiber->fd, &lsn, sizeof(lsn));
	if (r != sizeof(lsn)) {
		if (r < 0) {
			panic_syserror("read");
		}
		panic("invalid lns request size: %zu", r);
	}

	ver = tbuf_alloc(fiber->pool);
	tbuf_append(ver, &default_version, sizeof(default_version));
	client_handler_send_row(NULL, ver);

	log_io = recover_init(NULL, cfg.wal_dir,
			      NULL, client_handler_send_row, INT32_MAX, 0, 64, RECOVER_READONLY, false);

	recover(log_io, lsn);
	recover_follow(log_io, 0.1);
	ev_loop(0);

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
			panic_syserror("write");
		}
		len -= bytes;
		data += bytes;
	}

	say_debug("send row: %" PRIu32 " bytes %s", t->len, tbuf_to_hex(t));

	return 0;
}

