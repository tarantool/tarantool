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
#include "lua/socket.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
} /* extern "C" */

#include "sio.h"
#include "evio.h"
#include "coio.h"
#include "coeio.h"
#include "iobuf.h"
#include "coio_buf.h"

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "fiber.h"
#include "tbuf.h"
#include <lua/utils.h>
#include <stdlib.h>
#include <mutex.h>

static const char socketlib_name[] = "socket";

/**
 * gethostbyname(), getaddrinfo() and friends do not use
 * errno or errno.h errors for their error returns.
 * Here we map all failures of name resolution to a single
 * socket error number.
 */

/** last operation status */
enum bio_status {
	BIO_ERROR,
	BIO_TIMEOUT,
	BIO_EOF,
	BIO_LIMIT
};

struct bio_socket {
	struct ev_io io_r;
	struct ev_io io_w;
	struct mutex io_r_mutex;
	struct mutex io_w_mutex;
	struct iobuf *iob;
	/** SOCK_DGRAM or SOCK_STREAM */
	int socktype;
	int error;
};

static int
bio_pushsocket(struct lua_State *L, int socktype)
{
	struct bio_socket *s = (struct bio_socket *)
			lua_newuserdata(L, sizeof(struct bio_socket));
	luaL_getmetatable(L, socketlib_name);
	lua_setmetatable(L, -2);
	coio_init(&s->io_r);
	coio_init(&s->io_w);
	s->socktype = socktype;
	s->iob = NULL;
	s->error = 0;
	mutex_create(&s->io_r_mutex);
	mutex_create(&s->io_w_mutex);
	/*
	 * Do not create a file descriptor yet. Thanks to ipv6,
	 * socket family is not known until host name is resolved.
	 * Socket type is saved in s->socktype.
	 */
	return 1;
}

static inline struct bio_socket *
bio_checksocket(struct lua_State *L, int narg)
{
	/* avoiding unnecessary luajit assert */
	if (lua_gettop(L) < narg)
		luaL_error(L, "socket: incorrect method call");
	return (struct bio_socket *) luaL_checkudata(L, narg, socketlib_name);
}

static inline struct bio_socket *
bio_checkactivesocket(struct lua_State *L, int narg)
{
	struct bio_socket *s = bio_checksocket(L, narg);
	if (! evio_is_active(&s->io_w))
		luaL_error(L, "socket: socket is not initialized");
	return s;
}

/**
 * The last error is saved in socket. It can be pretty harmless,
 * for example a timeout. Clear the last error before the next
 * call. For now clear any error, even a persistent one: it's not
 * clear how being any smarter can benefit the library user.
 */
static inline void
bio_clearerr(struct bio_socket *s)
{
	s->error = false;
}

static void
bio_initbuf(struct bio_socket *s)
{
	assert(s->iob == NULL);
	char name[FIBER_NAME_MAX];
	const char *type = s->socktype == SOCK_STREAM ? "tcp" : "udp";
	snprintf(name, sizeof(name), "socket.%s(%d)",
		 type, s->io_w.fd);
	s->iob = iobuf_new(name);
}

static inline int
bio_pushstatus(struct lua_State *L, enum bio_status s)
{
	static const char *status_strs[] = {"error", "timeout", "eof", "limit"};
	lua_pushstring(L, status_strs[s]);
	return 1;
}

static int
bio_pusherrorcode(struct lua_State *L, struct bio_socket *s)
{
	lua_pushinteger(L, s->error);
	if (s->error >= 0)
		lua_pushstring(L, strerror(s->error));
	else if (s->error == ERESOLVE)
		lua_pushstring(L, "Host name resolution failed");
	return 2;
}

/** Error from accept, connect, bind, etc. */
static int
bio_pusherror(struct lua_State *L, struct bio_socket *s, int errorno)
{
	s->error = errorno;
	bio_pushstatus(L, errorno == ETIMEDOUT ? BIO_TIMEOUT : BIO_ERROR);
	return 1 + bio_pusherrorcode(L, s);
}

static int
bio_pushsockerror(struct lua_State *L, struct bio_socket *s, int errorno)
{
	lua_pushnil(L); /* no socket. */
	return 1 + bio_pusherror(L, s, errorno);
}

/** Error from send */
static int
bio_pushsenderror(struct lua_State *L, struct bio_socket *s, size_t sz, int errorno)
{
	lua_pushinteger(L, sz); /* sent zero bytes. */
	return 1 + bio_pusherror(L, s, errorno);
}

/** Error from recv */
static int
bio_pushrecverror(struct lua_State *L, struct bio_socket *s, int errorno)
{
	lua_pushstring(L, ""); /* received no data. */
	return 1 + bio_pusherror(L, s, errorno);
}

static inline int
bio_pusheof(struct lua_State *L, struct bio_socket *s)
{
	struct ibuf *in = &s->iob->in;
	lua_pushlstring(L, in->pos, ibuf_size(in));
	in->pos += ibuf_size(in);
	bio_pushstatus(L, BIO_EOF);
	return 2;
}

static int
lbox_socket_tostring(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	lua_pushstring(L, sio_socketname(s->io_w.fd));
	return 1;
}

/**
 * socket.tcp()
 *
 * Create SOCK_STREAM socket object.
 */
static int
lbox_socket_tcp(struct lua_State *L)
{
	return bio_pushsocket(L, SOCK_STREAM);
}

/**
 * socket.udp()
 *
 * Create SOCK_DGRAM socket object.
 */
static int
lbox_socket_udp(struct lua_State *L)
{
	return bio_pushsocket(L, SOCK_DGRAM);
}

/**
 * socket:close()
 *
 * Close the socket. A closed socket should not be used
 * any more.
 */
static int
lbox_socket_close(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	if (! evio_is_active(&s->io_w))
		return 0;
	if (s->iob) {
		iobuf_delete(s->iob);
		s->iob = NULL;
	}
	ev_io_stop(loop(), &s->io_r);
	evio_close(loop(), &s->io_w);
	s->io_r.fd = s->io_w.fd;
	assert(s->io_r.fd == -1);
	mutex_destroy(&s->io_r_mutex);
	mutex_destroy(&s->io_w_mutex);
	bio_clearerr(s);
	return 0;
}

/**
 * socket:shutdown(how)
 *
 * Shut down part of a full-duplex connection.
 *
 * @retval self                                 success
 * @retval nil, status = "error", eno, estr     error
 */
static int
lbox_socket_shutdown(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	int how = luaL_checkint(L, 2);
	bio_clearerr(s);
	if (shutdown(s->io_w.fd, how))
		return bio_pushsockerror(L, s, errno);
	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/**
 * socket:error()
 *
 * @return error code and error description of the last error.
 */
static int
lbox_socket_error(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	return bio_pusherrorcode(L, s);
}

/**
 * socket:connect(host, port [, timeout])
 *
 * Connect socket to a host.
 *
 * @retval self                                 success
 * @retval nil, status = "error", eno, estr     error
 * @retval nil, status = "timeout", eno, estr   timeout
 */
static int
lbox_socket_connect(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, 1);
	const char *host = luaL_checkstring(L, 2);
	const char *port = luaL_checkstring(L, 3);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 4)
		timeout = luaL_checknumber(L, 4);
	if (evio_is_active(&s->io_w))
		return bio_pushsockerror(L, s, EALREADY);
	bio_clearerr(s);

	/* try to resolve a hostname */
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	struct addrinfo *ai = coeio_resolve(s->socktype, host, port, delay);
	if (ai == NULL)
		return bio_pushsockerror(L, s, errno);

	coio_timeout_update(start, &delay);
	try {
		/* connect to a first available host */
		int r = coio_connect_addrinfo(&s->io_w, ai, delay);
		freeaddrinfo(ai);
		if (r)
			return bio_pushsockerror(L, s, ETIMEDOUT);

		/* set coio reader socket */
		s->io_r.fd = s->io_w.fd;
	} catch (SocketError *e) {
		freeaddrinfo(ai);
		return bio_pushsockerror(L, s, errno);
	}

	bio_initbuf(s);
	/* Success */
	lua_settop(L, 1);
	return 1;
}

/**
 * socket:send(data [, timeout])
 *
 * Send data to a socket.
 *
 * In case of socket an error or timeout send() returns
 * the number of bytes written before the error occurred.
 *
 *
 * @retval size                                 success
 * @retval size, status = "timeout", eno, estr  timeout
 * @retval size, status = "error", eno, estr    error
 *
 */
static int
lbox_socket_send(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	size_t buf_size = 0;
	const char *buf = luaL_checklstring(L, 2, &buf_size);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 3)
		timeout = luaL_checknumber(L, 3);
	if (s->iob == NULL)
		return bio_pushsenderror(L, s, 0, ENOTCONN);
	bio_clearerr(s);

	/* acquire write lock */
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	bool timed_out = mutex_lock_timeout(&s->io_w_mutex, delay);
	if (timed_out)
		return bio_pushsenderror(L, s, 0, ETIMEDOUT);
	coio_timeout_update(start, &delay);

	int rc;
	try {
		ssize_t nwr = coio_write_timeout(&s->io_w, buf, buf_size,
						 delay);
		if (nwr < buf_size) {
			rc = bio_pushsenderror(L, s, nwr, ETIMEDOUT);
			mutex_unlock(&s->io_w_mutex);
			return rc;
		}

		mutex_unlock(&s->io_w_mutex);
	} catch (SocketError *e) {
		mutex_unlock(&s->io_w_mutex);

		rc = bio_pushsenderror(L, s, 0, errno);
		return rc;
	} catch (Exception *e) {
		mutex_unlock(&s->io_w_mutex);
		throw;
	}

	/* case #1: Success */
	lua_pushinteger(L, buf_size);
	return 1;
}

/**
 * socket:recv(size [, timeout])
 *
 * Try to read size bytes from a socket.
 *
 * In case of timeout all read data will be available on next
 * read. In case of read error the data is thrown away.
 *
 * @retval data                                         success
 * @retval data = "", status = "error", eno, estr       socket error
 * @retval data = "", status = "timeout", eno, estr     read timeout
 * @retval data = chunk, status = "eof"                 eof
 */
static int
lbox_socket_recv(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	int sz = luaL_checkint(L, 2);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) >= 3)
		timeout = luaL_checknumber(L, 3);
	if (s->iob == NULL)
		return bio_pushrecverror(L, s, ENOTCONN);
	/* Clear possible old timeout status. */
	bio_clearerr(s);

	/*
	 * Readahead buffer can contain sufficient amount of
	 * data from the previous call to cover the required read
	 * size.
	 *
	 * If not, try to read as much as possible to readahead
	 * until it's more or equal to the required size.
	 */
	struct ibuf *in = &s->iob->in;
	ssize_t to_read = sz - ibuf_size(in);

	if (to_read > 0) {
		int rc;
		/* acquire read lock */
		ev_tstamp start, delay;
		coio_timeout_init(&start, &delay, timeout);
		bool timed_out = mutex_lock_timeout(&s->io_r_mutex, delay);
		if (timed_out)
			return bio_pushrecverror(L, s, ETIMEDOUT);
		coio_timeout_update(start, &delay);
		to_read = sz - ibuf_size(in);

		ssize_t nrd;
		try {
			nrd = coio_bread_timeout(&s->io_r, in, to_read,
						 delay);
			mutex_unlock(&s->io_r_mutex);
		} catch (SocketError *e) {
			mutex_unlock(&s->io_r_mutex);

			rc = bio_pushrecverror(L, s, errno);
			return rc;
		} catch (Exception *e) {
			mutex_unlock(&s->io_r_mutex);
			throw;
		}

		if (nrd < to_read) {
			/*  timeout or EOF. */
			if (errno == ETIMEDOUT)
				rc = bio_pushrecverror(L, s, ETIMEDOUT);
			else
				rc = bio_pusheof(L, s);
			return rc;
		}
	}
	lua_pushlstring(L, in->pos, sz);
	in->pos += sz;
	return 1;
}

struct readline_state {
	int pos;
	const char *sep;
	size_t sep_size;
};

static void
readline_state_init(struct lua_State *L, struct readline_state *rs, int idx)
{
	int i = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		rs[i].pos = 0;
		rs[i].sep = luaL_checklstring(L, -1, &rs[i].sep_size);
		if (rs[i].sep_size == 0)
			luaL_error(L, "socket.readline: bad separator");
		lua_pop(L, 1);
		i++;
	}
}

static int
readline_state_next(struct readline_state *rs, int size, char chr)
{
	int i;
	for (i = 0; i < size; i++) {
		/* we have to repeat state check to ensure that we
		 * don't miss new separator state after previous
		 * reset. */
		if (unlikely(rs[i].sep[ rs[i].pos ] == chr)) {
first_matched:		if (unlikely(rs[i].sep_size == rs[i].pos + 1))
				return i;
			rs[i].pos++;
			continue;
		}
		rs[i].pos = 0;
		if (unlikely(rs[i].sep[ rs[i].pos ] == chr))
			goto first_matched;
	}
	return -1;
}

static void
lbox_socket_readline_cr(struct lua_State *L)
{
	/* emulate user passed {'\n'} as the separate table */
	lua_newtable(L);
	lua_pushnumber(L, 1);
	lua_pushstring(L, "\n");
	lua_rawset(L, -3);
}

static int
lbox_socket_readline_opts(struct lua_State *L, unsigned int *limit,
		      double *timeout)
{
	int seplist = 2;
	switch (lua_gettop(L)) {
	case 1:
		/* readline() */
		lbox_socket_readline_cr(L);
		break;
	case 2:
		 /* readline(limit)
		    readline({seplist}) */
		if (lua_isnumber(L, 2)) {
			*limit = luaL_checkint(L, 2);
			lbox_socket_readline_cr(L);
			seplist = 3;
		} else if (! lua_istable(L, 2))
			luaL_error(L, "socket.readline: bad argument");
		break;
	case 3:
		/* readline(limit, timeout)
		 * readline(limit, {seplist})
		 * readline({seplist}, timeout) */
		if (lua_isnumber(L, 2)) {
			*limit = luaL_checkint(L, 2);
			if (lua_isnumber(L, 3)) {
				*timeout = luaL_checknumber(L, 3);
				lbox_socket_readline_cr(L);
				seplist = 4;
				break;
			} else if (! lua_istable(L, 3))
				luaL_error(L, "socket.readline: bad argument");
			seplist = 3;
			break;
		} else if (! lua_istable(L, 2))
			luaL_error(L, "socket.readline: bad argument");
		*timeout = luaL_checknumber(L, 3);
		seplist = 2;
		break;
	case 4:
		/* readline(limit, {seplist}, timeout) */
		*limit = luaL_checkint(L, 2);
		if (! lua_istable(L, 3))
			luaL_error(L, "socket.readline: bad argument");
		seplist = 3;
		*timeout = luaL_checknumber(L, 4);
		break;
	default:
		luaL_error(L, "socket.readline: bad argument");
		break;
	}
	return seplist;
}

/**
 * socket:readline(limit, seplist, timeout)
 *
 * Possible usage:
 *
 * readline() == readline(limit == inf, seplist == {'\n'}, timeout == inf)
 * readline(limit)
 * readline(limit, timeout)
 * readline({seplist})
 * readline(limit, {seplist})
 * readline({seplist}, timeout)
 * readline(limit, {seplist}, timeout)
 *
 * In case of socket error and timeout all read data will be
 * available on next read.
 *
 * @retval data, nil, sep = str                     success
 * @retval data = "", status = "timeout", eno, estr timeout
 * @retval data = "", status = "error", eno, estr   error
 * @retval data = chunk, status = "limit"           limit
 * @retval data = chunk, status = "eof"             eof
 */
static int
lbox_socket_readline(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	if (s->iob == NULL)
		return bio_pushrecverror(L, s, ENOTCONN);
	bio_clearerr(s);

	unsigned int limit = UINT_MAX;
	double timeout = TIMEOUT_INFINITY;
	int seplist = lbox_socket_readline_opts(L, &limit, &timeout);

	int rs_size = lua_objlen(L, seplist);
	if (rs_size == 0)
		luaL_error(L, "socket.readline: bad separator table");

	/* acquire read lock */
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);
	bool timed_out = mutex_lock_timeout(&s->io_r_mutex, delay);
	if (timed_out)
		return bio_pushrecverror(L, s, ETIMEDOUT);
	coio_timeout_update(start, &delay);

	size_t bottom = 0;
	int match;
	struct ibuf *in = &s->iob->in;
	int rc;

	try {
		/* readline implementation uses a simple state machine
		 * to determine current position of a possible
		 * separator. */
		struct readline_state *rs = (struct readline_state *)
			region_alloc(in->pool, sizeof(struct readline_state) * rs_size);
		readline_state_init(L, rs, seplist);

		while (1) {
			/* case #4: user limit reached */
			if (bottom == limit) {
				lua_pushlstring(L, in->pos, bottom);
				s->iob->in.pos += bottom;
				bio_pushstatus(L, BIO_LIMIT);
				mutex_unlock(&s->io_r_mutex);
				return 2;
			}

			/* if current read position (bottom) equals to
			 * the readahead size, then read new data. */
			if (bottom == ibuf_size(in)) {
				ssize_t nrd = coio_bread_timeout(&s->io_r, &s->iob->in, 1,
								 delay);
				/* case #5: eof (step 1)*/
				if (nrd == 0) {
					if (errno == ETIMEDOUT)
						rc = bio_pushrecverror(L, s, ETIMEDOUT);
					else
						rc = bio_pusheof(L, s);
					mutex_unlock(&s->io_r_mutex);
					return rc;
				}
				coio_timeout_update(start, &delay);
			}

			match = readline_state_next(rs, rs_size, in->pos[bottom]);
			bottom++;
			if (match >= 0)
				break;
		}

		mutex_unlock(&s->io_r_mutex);
	} catch (SocketError *e) {
		mutex_unlock(&s->io_r_mutex);
		rc = bio_pushrecverror(L, s, errno);
		return rc;
	} catch (Exception *e) {
		mutex_unlock(&s->io_r_mutex);
		throw;
	}

	/* case #1: success, separator matched */
	lua_pushlstring(L, in->pos, bottom);
	in->pos += bottom;
	lua_pushnil(L);
	lua_rawgeti(L, seplist, match + 1);
	return 3;
}

/**
 * socket:bind(host, port [, timeout])
 *
 * Bind a socket to the given host.
 *
 * @retval socket                             success
 * @retval nil, status = "error", eno, estr   error
 * @retval nil, status = "timeout", eno, estr timeout
 */
static int
lbox_socket_bind(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, 1);
	const char *host = luaL_checkstring(L, 2);
	const char *port = luaL_checkstring(L, 3);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 4)
		timeout = luaL_checknumber(L, 4);
	if (evio_is_active(&s->io_w))
		return bio_pusherror(L, s, EALREADY);
	bio_clearerr(s);
	/* try to resolve a hostname */
	struct addrinfo *ai = coeio_resolve(s->socktype, host, port, timeout);
	if (ai == NULL)
		return bio_pusherror(L, s, errno);
	try {
		evio_bind_addrinfo(&s->io_w, ai);
		freeaddrinfo(ai);
	} catch (SocketError *e) {
		/* case #2: error */
		freeaddrinfo(ai);
		return bio_pusherror(L, s, errno);
	}

	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/**
 * socket:listen()
 *
 * Marks the socket that it will be used to accept incoming
 * connection requests using socket:accept().
 *
 * @retval socket (self) on success
 * @retval nil, status = "error", errno, error string
 */
static int
lbox_socket_listen(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	bio_clearerr(s);
	if (listen(s->io_w.fd, sio_listen_backlog()))
		return bio_pusherror(L, s, errno);
	lua_settop(L, 1);
	return 1;
}

/**
 * socket:accept([timeout])
 *
 * Wait for a new client connection and create connected
 * socket.
 *
 * @retval socket (client), nil, address, port        success
 * @retval nil, status = "error", eno, estr           error
 * @retval nil, status = "timeout", eno, estr         timeout
 */
static int
lbox_socket_accept(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 2)
		timeout = luaL_checknumber(L, 2);
	bio_clearerr(s);

	struct sockaddr_storage addr;
	/* push client socket */
	bio_pushsocket(L, SOCK_STREAM);
	struct bio_socket *client = (struct bio_socket *) lua_touserdata(L, -1);
	try {
		client->io_w.fd = coio_accept(&s->io_w, (struct sockaddr*)&addr,
					     sizeof(addr), timeout);
		client->io_r.fd = client->io_w.fd;
	} catch (SocketError *e) {
		return bio_pusherror(L, s, errno);
	}
	/* get user host and port */
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];
	int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
			     hbuf, sizeof(hbuf),
			     sbuf, sizeof(sbuf), NI_NUMERICHOST|NI_NUMERICSERV);
	if (rc != 0)
		return bio_pusherror(L, s, ERESOLVE);

	bio_initbuf(client);
	lua_pushnil(L); /* status */
	/* push host and port */
	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);
	return 4;
}

/**
 * socket:sendto(buf, host, port [, timeout])
 *
 * Send a message on a UDP socket to a specified host.
 *
 * @retval  size success
 * @retval  0, status = "error", eno, estr    error
 * @retval  0, status = "timeout", eno, estr  timeout
 */
static int
lbox_socket_sendto(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, 1);
	size_t buf_size = 0;
	const char *buf = luaL_checklstring(L, 2, &buf_size);
	const char *host = luaL_checkstring(L, 3);
	const char *port = luaL_checkstring(L, 4);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 5)
		timeout = luaL_checknumber(L, 5);
	bio_clearerr(s);

	/* try to resolve a hostname */
	ev_tstamp start, delay;
	coio_timeout_init(&start, &delay, timeout);

	struct sockaddr_storage ss;
	assert(sizeof(ss) > sizeof(struct sockaddr_in6));
	struct addrinfo *a = NULL;
	struct sockaddr *addr = (struct sockaddr*)&ss;
	socklen_t addrlen;

	if (evio_pton(host, port, &ss, &addrlen) == -1) {
		coio_timeout_init(&start, &delay, timeout);
		/* try to resolve a hostname */
		struct addrinfo *a = coeio_resolve(s->socktype, host, port, delay);
		if (a == NULL)
			return bio_pushsenderror(L, s, 0, errno);
		coio_timeout_update(start, &delay);
		addr = (struct sockaddr *) a->ai_addr;
		addrlen = a->ai_addrlen;
	}

	size_t nwr;
	try {
		/* maybe init the socket */
		if (! evio_is_active(&s->io_w))
			evio_socket(&s->io_w, addr->sa_family, s->socktype, 0);
		nwr = coio_sendto_timeout(&s->io_w, buf, buf_size, 0,
			addr, addrlen, delay);
		if (a) {
			freeaddrinfo(a);
		}
	} catch (SocketError *e) {
		/* case #2-3: error or timeout */
		if (a) {
			freeaddrinfo(a);
		}
		return bio_pushsenderror(L, s, 0, errno);
	}

	if (nwr == 0) {
		assert(errno == ETIMEDOUT);
		return bio_pushsenderror(L, s, 0, ETIMEDOUT);
	}
	lua_pushinteger(L, nwr);
	return 1;
}

/**
 * socket:recvfrom(limit [, timeout])
 *
 * Receive a message on a UDP socket.
 *
 * @retval data, nil, client_addr, client_port      success
 * @retval data = "", status = "error",  eno, estr  error
 * @retval data = "", status = "timeout", eno, estr timeout
 * @retval data, status = "eof"                     eof
 */
static int
lbox_socket_recvfrom(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	int buf_size = luaL_checkint(L, 2);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 3)
		timeout = luaL_checknumber(L, 3);
	bio_clearerr(s);

	/* Maybe initialize the buffer, can throw ER_MEMORY_ISSUE. */
	if (s->iob == NULL)
		bio_initbuf(s);

	struct sockaddr_storage addr;
	struct ibuf *in = &s->iob->in;
	size_t nrd;
	try {
		ibuf_reserve(in, buf_size);
		nrd = coio_recvfrom_timeout(&s->io_w, in->pos, buf_size, 0,
					    (struct sockaddr*)&addr,
					    sizeof(addr), timeout);
	} catch (SocketError *e) {
		return bio_pushrecverror(L, s, errno);
	}
	if (nrd == 0) {
		if (errno == ETIMEDOUT)
			return bio_pushrecverror(L, s, errno);
		return bio_pusheof(L, s);
	}

	/* push host and port */
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];
	int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
			     hbuf, sizeof(hbuf),
			     sbuf, sizeof(sbuf), NI_NUMERICHOST|NI_NUMERICSERV);
	if (rc != 0) {
		/* case #2: error */
		return bio_pushrecverror(L, s, ERESOLVE);
	}

	/* case #1: push received data */
	lua_pushlstring(L, in->pos, nrd);
	lua_pushnil(L);
	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);
	return 4;
}

void
tarantool_lua_socket_init(struct lua_State *L)
{
	static const struct luaL_reg lbox_socket_meta[] = {
		{"__gc", lbox_socket_close},
		{"__tostring", lbox_socket_tostring},
		{"error", lbox_socket_error},
		{"close", lbox_socket_close},
		{"shutdown", lbox_socket_shutdown},
		{"connect", lbox_socket_connect},
		{"send", lbox_socket_send},
		{"recv", lbox_socket_recv},
		{"readline", lbox_socket_readline},
		{"bind", lbox_socket_bind},
		{"listen", lbox_socket_listen},
		{"accept", lbox_socket_accept},
		{"sendto", lbox_socket_sendto},
		{"recvfrom", lbox_socket_recvfrom},
		{NULL, NULL}
	};
	static const struct luaL_reg socketlib[] = {
		{"tcp", lbox_socket_tcp},
		{"udp", lbox_socket_udp},
		{NULL, NULL}
	};
	luaL_register_type(L, socketlib_name, lbox_socket_meta);
	luaL_register_module(L, socketlib_name, socketlib);
	lua_pushstring(L, "SHUT_RD");
	lua_pushnumber(L, SHUT_RD);
	lua_settable(L, -3);
	lua_pushstring(L, "SHUT_WR");
	lua_pushnumber(L, SHUT_WR);
	lua_settable(L, -3);
	lua_pushstring(L, "SHUT_RDWR");
	lua_pushnumber(L, SHUT_RDWR);
	lua_settable(L, -3);
	lua_pop(L, 1);
}
