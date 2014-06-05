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
#include "replication.h"
#include <say.h>
#include <fiber.h>
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
#include "iproto_constants.h"
#include "msgpuck/msgpuck.h"
#include "box/cluster.h"
#include "box/schema.h"
#include "box/vclock.h"

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
static char cfg_wal_dir[PATH_MAX];
static char cfg_snap_dir[PATH_MAX];


/**
 * State of a replica. We only need one global instance
 * since we fork() for every replica.
 */
struct replica {
	/** Replica connection */
	int sock;
	/** Initial lsn. */
	int64_t lsn;
	uint64_t sync;
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
spawner_create_replication_relay(struct relay_data *data);

/** Shut down all relays when shutting down the spawner. */
static void
spawner_shutdown_children();

/** Initialize replication relay process. */
static void
replication_relay_loop(struct relay_data *data);

/*
 * ------------------------------------------------------------------------
 * replication module
 * ------------------------------------------------------------------------
 */

/** Pre-fork replication spawner process. */
void
replication_prefork(const char *snap_dir, const char *wal_dir)
{
	snprintf(cfg_snap_dir, sizeof(cfg_snap_dir), "%s", snap_dir);
	snprintf(cfg_wal_dir, sizeof(cfg_wal_dir), "%s", wal_dir);
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
struct relay_data {
	uint32_t type;
	uint64_t sync;

	/* for SUBSCRIBE */
	uint32_t node_id;
	uint32_t lsnmap_size;
	struct {
		uint32_t node_id;
		int64_t lsn;
	} lsnmap[];
};

struct replication_request {
	struct ev_io io;
	int fd;
	struct relay_data data;
};

/** Replication acceptor fiber handler. */
void
replication_join(int fd, struct iproto_header *header)
{
	assert(header->type == IPROTO_JOIN);
	if (header->bodycnt == 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "JOIN body");

	const char *data = (const char *) header->body[0].iov_base;
	const char *end = data + header->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "JOIN body");

	tt_uuid node_uuid = uuid_nil;
	d = data;
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d); /* key */
			mp_next(&d); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		if (key == IPROTO_NODE_UUID) {
			if (mp_typeof(*d) != MP_STR ||
			    mp_decode_strl(&d) != UUID_LEN) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid Node-UUID");
			}
			tt_uuid_dec_be(d, &node_uuid);
			d += UUID_LEN;
		} else {
			mp_next(&d); /* value */
		}
	}

	if (tt_uuid_is_nil(&node_uuid)) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "Can't find Node-UUID in JOIN request");
	}

	/* Notify box about new cluster node */
	recovery_state->join_handler(&node_uuid);

	struct replication_request *request = (struct replication_request *)
			malloc(sizeof(*request));
	if (request == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*request),
			  "iproto", "JOIN");
	}
	request->fd = fd;
	request->io.data = request;
	request->data.type = header->type;
	request->data.sync = header->sync;

	ev_io_init(&request->io, replication_send_socket,
		   master_to_spawner_socket, EV_WRITE);
	ev_io_start(loop(), &request->io);
}

/** Replication acceptor fiber handler. */
void
replication_subscribe(int fd, struct iproto_header *packet)
{
	if (packet->bodycnt == 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "subscribe body");
	assert(packet->bodycnt == 1);
	const char *data = (const char *) packet->body[0].iov_base;
	const char *end = data + packet->body[0].iov_len;
	const char *d = data;
	if (mp_check(&d, end) != 0 || mp_typeof(*data) != MP_MAP)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "subscribe body");
	tt_uuid cluster_uuid = uuid_nil, node_uuid = uuid_nil;

	const char *lsnmap = NULL;
	d = data;
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d); /* key */
			mp_next(&d); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_CLUSTER_UUID:
			if (mp_typeof(*d) != MP_STR ||
			    mp_decode_strl(&d) != UUID_LEN) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid Cluster-UUID");
			}
			tt_uuid_dec_be(d, &cluster_uuid);
			d += UUID_LEN;
			break;
		case IPROTO_NODE_UUID:
			if (mp_typeof(*d) != MP_STR ||
			    mp_decode_strl(&d) != UUID_LEN) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid Node-UUID");
			}
			tt_uuid_dec_be(d, &node_uuid);
			d += UUID_LEN;
			break;
		case IPROTO_LSNMAP:
			if (mp_typeof(*d) != MP_MAP) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid LSNMAP");
			}
			lsnmap = d;
			mp_next(&d);
			break;
		default:
			mp_next(&d); /* value */
		}
	}

	/* Check Cluster-UUID */
	cluster_check_id(&cluster_uuid);

	/* Check Node-UUID */
	uint32_t node_id = schema_find_id(SC_CLUSTER_ID, 1, tt_uuid_str(&node_uuid),
					  UUID_STR_LEN);
	if (node_id == SC_ID_NIL)
		tnt_raise(ClientError, ER_UNKNOWN_NODE, tt_uuid_str(&node_uuid));
	if (lsnmap == NULL)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "LSNMAP");
	/* Check & save LSNMAP */
	d = lsnmap;
	uint32_t lsnmap_size = mp_decode_map(&d);
	struct replication_request *request = (struct replication_request *)
		calloc(1, sizeof(*request) + sizeof(*request->data.lsnmap) *
		       (lsnmap_size + 1)); /* use calloc() for valgrind */

	if (request == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*request) +
			  sizeof(*request->data.lsnmap) * (lsnmap_size + 1),
			  "iproto", "SUBSCRIBE");
	}

	bool remote_found = false;
	for (uint32_t i = 0; i < lsnmap_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
		map_error:
			free(request);
			tnt_raise(ClientError, ER_INVALID_MSGPACK, "LSNMAP");
		}
		request->data.lsnmap[i].node_id = mp_decode_uint(&d);
		if (mp_typeof(*d) != MP_UINT)
			goto map_error;
		request->data.lsnmap[i].lsn = mp_decode_uint(&d);
		if (request->data.lsnmap[i].node_id == node_id)
			remote_found = true;
	}
	if (!remote_found) {
		/* Add remote node to the list */
		request->data.lsnmap[lsnmap_size].node_id = node_id;
		request->data.lsnmap[lsnmap_size].lsn = 0;
		++lsnmap_size;
	}

	request->fd = fd;
	request->io.data = request;
	request->data.type = packet->type;
	request->data.sync = packet->sync;
	request->data.node_id = node_id;
	request->data.lsnmap_size = lsnmap_size;

	ev_io_init(&request->io, replication_send_socket,
		   master_to_spawner_socket, EV_WRITE);
	ev_io_start(loop(), &request->io);
}


/** Send a file descriptor to the spawner. */
static void
replication_send_socket(ev_loop *loop, ev_io *watcher, int /* events */)
{
	struct replication_request *request =
		(struct replication_request *) watcher->data;
	struct msghdr msg;
	struct iovec iov[2];
	char control_buf[CMSG_SPACE(sizeof(int))];
	memset(control_buf, 0, sizeof(control_buf)); /* valgrind */
	struct cmsghdr *control_message = NULL;

	size_t len = sizeof(request->data) + sizeof(*request->data.lsnmap) *
			request->data.lsnmap_size;
	iov[0].iov_base = &len;
	iov[0].iov_len = sizeof(len);
	iov[1].iov_base = &request->data;
	iov[1].iov_len = len;

	memset(&msg, 0, sizeof(msg));

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = nelem(iov);
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

	size_t len;
	iov.iov_base = &len;
	iov.iov_len = sizeof(len);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	while (!spawner.killed) {
		ssize_t msglen = recvmsg(spawner.sock, &msg, 0);
		if (msglen == 0) { /* orderly master shutdown */
			say_info("Exiting: master shutdown");
			break;
		} else if (msglen == -1) {
			if (errno == EINTR)
				continue;
			say_syserror("recvmsg");
			/* continue, the error may be temporary */
			break;
		}

		replica.sock = spawner_unpack_cmsg(&msg);
		struct relay_data *data = (struct relay_data *) malloc(len);
		msglen = read(spawner.sock, data, len);
		if (msglen == 0) { /* orderly master shutdown */
			say_info("Exiting: master shutdown");
			free(data);
			break;
		} else if (msglen == -1) {
			free(data);
			if (errno == EINTR)
				continue;
			say_syserror("recvmsg");
			/* continue, the error may be temporary */
			break;
		}
		replica.sync = data->sync;

		spawner_create_replication_relay(data);
		free(data);
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
spawner_create_replication_relay(struct relay_data *data)
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
		replication_relay_loop(data);
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
static void
replication_relay_send_row(void * /* param */, struct iproto_header *packet)
{
	struct recovery_state *r = recovery_state;

	/* Don't duplicate data */
	if (packet->node_id == 0 || packet->node_id != r->node_id)  {
		packet->sync = replica.sync;
		struct iovec iov[IPROTO_ROW_IOVMAX];
		char fixheader[IPROTO_FIXHEADER_SIZE];
		int iovcnt = iproto_row_encode(packet, iov, fixheader);
		sio_writev_all(replica.sock, iov, iovcnt);
	}

	if (iproto_request_is_dml(packet->type)) {
		/*
		 * Update local vclock. During normal operation wal_write()
		 * updates local vclock. In relay mode we have to update
		 * it here.
		 */
		vclock_set(&r->vclock, packet->node_id, packet->lsn);
	}
}

static void
replication_relay_join(struct recovery_state *r)
{
	FDGuard guard_replica(replica.sock);

	/* Send snapshot */
	recover_snap(r);

	/* Send response to JOIN command = end of stream */
	struct iproto_header packet;
	memset(&packet, 0, sizeof(packet));
	packet.type = IPROTO_JOIN;
	packet.sync = replica.sync;

	char fixheader[IPROTO_FIXHEADER_SIZE];
	struct iovec iov[IPROTO_ROW_IOVMAX];
	int iovcnt = iproto_row_encode(&packet, iov, fixheader);
	sio_writev_all(replica.sock, iov, iovcnt);

	say_info("snapshot sent");
	/* replica.sock closed by guard */
}

static void
replication_relay_subscribe(struct recovery_state *r, struct relay_data *data)
{
	assert(data->type == IPROTO_SUBSCRIBE);
	/* Set LSNs */
	for (uint32_t i = 0; i < data->lsnmap_size; i++) {
		vclock_set(&r->vclock, data->lsnmap[i].node_id,
			       data->lsnmap[i].lsn);
	}

	/* Set node_id */
	r->node_id = data->node_id;

	recovery_follow_local(r, 0.1);
	ev_run(loop(), 0);

	say_crit("exiting the relay loop");
}

/** The main loop of replication client service process. */
static void
replication_relay_loop(struct relay_data *data)
{
	struct sigaction sa;

	/* Set process title and fiber name.
	 * Even though we use only the main fiber, the logger
	 * uses the current fiber name.
	 */
	struct sockaddr_storage peer;
	socklen_t addrlen = sizeof(peer);
	getpeername(replica.sock, ((struct sockaddr*)&peer), &addrlen);
	title("relay", "%s", sio_strfaddr((struct sockaddr *)&peer));
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
	recovery_init(cfg_snap_dir, cfg_wal_dir,
		      replication_relay_send_row,
		      NULL, NULL, NULL, INT32_MAX);
	recovery_state->relay = true; /* recovery used in relay mode */

	try {
		switch (data->type) {
		case IPROTO_JOIN:
			replication_relay_join(recovery_state);
			break;
		case IPROTO_SUBSCRIBE:
			replication_relay_subscribe(recovery_state, data);
			break;
		default:
			assert(false);
		}
	} catch(Exception *e) {
		say_error("relay error: %s", e->errmsg());
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}
