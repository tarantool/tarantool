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
#include "lua_io.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

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
#include <lua/init.h>
#include <stdlib.h>

static const char iolib_name[] = "box.io";

enum bio_error {
	ERESOLVE = -1
};

enum bio_status {
	BIO_ERROR,
	BIO_TIMEOUT,
	BIO_EOF,
	BIO_LIMIT
};

struct bio_socket {
	struct ev_io coio;
	struct iobuf *iob;
	/** SOCK_DGRAM or SOCK_STREAM */
	int socktype;
	int error;
	bool eof;
};

static int
bio_pushsocket(struct lua_State *L, int socktype)
{
	struct bio_socket *s = lua_newuserdata(L, sizeof(struct bio_socket));
	luaL_getmetatable(L, iolib_name);
	lua_setmetatable(L, -2);
	evio_clear(&s->coio);
	s->socktype = socktype;
	s->iob = NULL;
	s->error = 0;
	s->eof = false;
	return 1;
}

static inline struct bio_socket *
bio_checksocket(struct lua_State *L, int narg)
{
	/* avoiding unnecessary luajit assert */
	if (lua_gettop(L) < narg)
		luaL_error(L, "incorrect method call");
	return luaL_checkudata(L, narg, iolib_name);
}

static inline struct bio_socket *
bio_checkactivesocket(struct lua_State *L, int narg)
{
	struct bio_socket *s = bio_checksocket(L, narg);
	if (! evio_is_active(&s->coio))
		luaL_error(L, "socket is not initialized");
	return s;
}

static inline void
bio_clearerr(struct bio_socket *s)
{
	s->error = false;
}

static void
bio_initbuf(struct bio_socket *s)
{
	if (s->iob)
		return;
	char name[PALLOC_POOL_NAME_MAXLEN];
	const char *type = s->socktype == SOCK_STREAM ? "tcp" : "udp";
	snprintf(name, sizeof(name), "box.io.%s(%d)",
		 type, s->coio.fd);
	s->iob = iobuf_create(name);
}

static void
bio_close(struct bio_socket *s)
{
	if (! evio_is_active(&s->coio))
		return;
	if (s->iob)
		iobuf_destroy(s->iob);
	evio_close(&s->coio);
}

static inline int
bio_pushstatus(struct lua_State *L, enum bio_status s)
{
	static char *status_strs[] = {"error", "timeout", "eof", "limit"};
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
		lua_pushstring(L, "Host name resolving failed");
	return 2;
}

static int
bio_pusherror(struct lua_State *L, struct bio_socket *s, int errorno)
{
	s->error = errorno;
	lua_pushnil(L);
	bio_pushstatus(L, errorno == ETIMEDOUT ? BIO_TIMEOUT :
	               BIO_ERROR);
	return 2 + bio_pusherrorcode(L, s);
}

/*
 * Resolver function, run in separate thread by
 * coeio (libeio).
*/
static ssize_t getaddrinfo_cb(va_list ap)
{
	const char *host = va_arg(ap, const char *);
	const char *port = va_arg(ap, const char *);
	const struct addrinfo *hints = va_arg(ap, const struct addrinfo *);
	struct addrinfo **res = va_arg(ap, struct addrinfo **);
	return getaddrinfo(host, port, hints, res);
}

static struct addrinfo *
bio_resolve(int socktype, const char *host, const char *port,
            ev_tstamp timeout)
{
	/* fill hinting information with support for
	 * use by connect(2) or bind(2). */
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
	hints.ai_socktype = socktype;
	hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV|AI_PASSIVE;
	hints.ai_protocol = 0;
	/* do resolving */
	if (coeio_custom(getaddrinfo_cb, timeout, host, port, &hints, &result))
		return NULL;
	return result;
}

static int
lbox_io_tostring(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	lua_pushfstring(L, "%d", s->coio.fd);
	return 1;
}

/*
 * box.io.tcp()
 *
 * Create SOCK_STREAM socket object.
 */
static int
lbox_io_tcp(struct lua_State *L)
{
	return bio_pushsocket(L, SOCK_STREAM);
}

/*
 * box.io.udp()
 *
 * Create SOCK_DGRAM socket object.
 */
static int
lbox_io_udp(struct lua_State *L)
{
	return bio_pushsocket(L, SOCK_DGRAM);
}

/*
 * socket:close()
 *
 * Close socket and reinitialize readahead buffer.
 */
static int
lbox_io_close(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	bio_close(s);
	return 0;
}

/*
 * socket:shutdown(how)
 *
 * Shut down part of a full-duplex connection.
 *
 * Return:
 *
 * 1. ok:    socket (self)
 * 2. error: nil, status = "error", eno, estr
 */
static int
lbox_io_shutdown(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, -1);
	int how = luaL_checkint(L, 2);
	bio_clearerr(s);
	if (shutdown(s->coio.fd, how))
		return bio_pusherror(L, s, errno);
	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/*
 * socket:error()
 *
 * Return error code and error description.
 */
static int
lbox_io_error(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, -1);
	return bio_pusherrorcode(L, s);
}

/*
 * socket:connect(host, port [, timeout])
 *
 * Connect socket to a host.
 *
 * Return:
 *
 * 1. ok:      socket (self)
 * 2. error:   nil, status = "error",   eno, estr
 * 3. timeout: nil, status = "timeout", eno, estr
 */
static int
lbox_io_connect(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, 1);
	const char *host = luaL_checkstring(L, 2);
	const char *port = luaL_checkstring(L, 3);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 4)
		timeout = luaL_checknumber(L, 4);
	bio_clearerr(s);
	s->eof = false; /* clear eof in case of reconnect. */

	/* try to resolve a hostname */
	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);
	struct addrinfo *ai = bio_resolve(s->socktype, host, port, delay);
	if (ai == NULL)
		return bio_pusherror(L, s, errno);

	evio_timeout_update(start, &delay);
	@try {
		/* connect to a first available host */
		coio_connect_addrinfo(&s->coio, ai, delay);
	} @catch (SocketError *e) {
		/* case #2-3: error or timeout */
		return bio_pusherror(L, s, errno);
	} @finally {
		freeaddrinfo(ai);
	}
	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/*
 * socket:write(data [, timeout])
 *
 * Write up data to a socket.
 *
 * In case of socket error and timeout write will return
 * number of bytes written before error occur and
 * appropriate error will be set and returned.
 *
 * Return:
 *
 * 1. ok:      size
 * 2. error:   size, status = "error",   eno, estr
 * 3. timeout: size, status = "timeout", eno, estr
 *
 */
static int
lbox_io_write(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	size_t buf_size = 0;
	const char *buf = luaL_checklstring(L, 2, &buf_size);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 3)
		timeout = luaL_checknumber(L, 3);
	bio_clearerr(s);
	@try {
		coio_write_timeout(&s->coio, buf, buf_size, timeout);
	} @catch (SocketRWError *e) {
		/* case #2-3: error or timeout */
		s->error = errno;
		lua_pushinteger(L, e->n);
		bio_pushstatus(L, (s->error == ETIMEDOUT) ? BIO_TIMEOUT :
		                   BIO_ERROR);
		return 2 + bio_pusherrorcode(L, s);
	}
	/* case #1: Success */
	lua_pushinteger(L, buf_size);
	return 1;
}

static inline int
lbox_io_read_ret(struct lua_State *L, struct bio_socket *s, char *buf,
                 size_t size, enum bio_status st)
{
	lua_pushlstring(L, buf, size);
	bio_pushstatus(L, st);
	return 2 + bio_pusherrorcode(L, s);
}

static inline int
lbox_io_read_eof(struct lua_State *L, struct bio_socket *s,
		 size_t sz)
{
	size_t ret = ibuf_size(&s->iob->in);
	if (ret > sz)
		return -1;
	/* if this was the last chunk and it fits in
	 * read limit: return it with eof status
	 * set.
	 */
	lbox_io_read_ret(L, s, s->iob->in.pos, ret, BIO_EOF);
	s->iob->in.pos += ret;
	return 4;
}

/*
 * socket:read(size [, timeout])
 *
 * Read up to size bytes from a socket.
 *
 * In case of socket error and timeout all read data will be
 * available on next read.
 *
 * Return:
 *
 * 1. ok:      data
 * 2. error:   data = "",    status = "error",   eno, estr
 * 3. timeout: data = "",    status = "timeout", eno, estr
 * 4. eof:     data = chunk, status = "eof",     eno, estr
 *
 */
static int
lbox_io_read(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	int sz = luaL_checkint(L, 2);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 3)
		timeout = luaL_checknumber(L, 3);
	bio_clearerr(s);

	/* maybe init buffer, can throw ER_MEMORY_ISSUE. */
	bio_initbuf(s);

	/* eof condition handling (step 2).
	 * return user data until buffer is empty.
	 *
	 * See case #4. */
	if (s->eof) {
		int rc = lbox_io_read_eof(L, s, sz);
		if (rc == -1)
			goto case_1;
		return rc;
	}

	ev_tstamp start, delay;
	evio_timeout_init(&start, &delay, timeout);

	/*
	 * Readahead buffer can contain a sufficient amount of data from a
	 * previous work to cover required read size.
	 *
	 * If not, try to read as much as possible to readahead until it's
	 * more or equal to the required size. */
	size_t nrd;
	while (ibuf_size(&s->iob->in) < sz) {
		@try {
			nrd = coio_bread_timeout(&s->coio, &s->iob->in, 1,
						 delay);
		} @catch (SocketRWError *e) {
			/*
			 * coio_bread_timeout() can throw an expception in case of
			 * timeout or socket error and not to advance readahead end
			 * pointer to the read amount before exceptions, this could
			 * lead to a data loss.
			 *
			 * this code deals with this situation by introduction
			 * SocketRWError exception with saved read size.
			*/

			/* case #2-3: don't advance read position, only readahead
			 *            one. */
			s->iob->in.end += e->n;
			s->error = errno;

			enum bio_status st =
				(s->error == ETIMEDOUT) ? BIO_TIMEOUT :
				 BIO_ERROR;
			return lbox_io_read_ret(L, s, NULL, 0, st);
		}

		/* in case of EOF from a client, return partly read chunk and
		 * return eof (step 1).
		 *
		 * case #4
		 * */
		if (nrd == 0) {
			s->eof = true;
			/* read chunk could be much bigger than limit, in
			 * this case, don't return eof flag to a user until
			 * whole data been read. */
			int rc = lbox_io_read_eof(L, s, sz);
			if (rc == -1)
				goto case_1;
			return rc;
		}

		evio_timeout_update(start, &delay);
	}

	assert( ibuf_size(&s->iob->in) >= sz );

	/* case #1: whole data chunk is ready */
case_1:
	lua_pushlstring(L, s->iob->in.pos, sz);
	s->iob->in.pos += sz;
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
			luaL_error(L, "box.io.readline: bad separator");
		lua_pop(L, 1);
		i++;
	}
}

static inline int
readline_state_try(struct readline_state *rs, int i, char chr)
{
	if (unlikely(rs[i].sep[ rs[i].pos ] == chr)) {
		if (unlikely(rs[i].sep_size == rs[i].pos + 1))
			return i;
		rs[i].pos++;
	} else {
		rs[i].pos = 0;
	}
	return -1;
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
lbox_io_readline_cr(struct lua_State *L)
{
	/* emulate user passed {'\n'} as the separate table */
	lua_newtable(L);
	lua_pushnumber(L, 1);
	lua_pushstring(L, "\n");
	lua_rawset(L, -3);
}

static int
lbox_io_readline_ret(struct lua_State *L, struct bio_socket *s,
                     char *buf, size_t size,
                     enum bio_status st, int sid)
{
	lua_pushlstring(L, buf, size);
	lua_pushinteger(L, sid);
	bio_pushstatus(L, st);
	return 3 + bio_pusherrorcode(L, s);
}

static int
lbox_io_readline_opts(struct lua_State *L, unsigned int *limit,
		      double *timeout)
{
	int seplist = 2;
	switch (lua_gettop(L)) {
	case 1:
		/* readline() */
		lbox_io_readline_cr(L);
		break;
	case 2:
		 /* readline(limit)
		    readline({seplist}) */
		if (lua_isnumber(L, 2)) {
			*limit = luaL_checkint(L, 2);
			lbox_io_readline_cr(L);
			seplist = 3;
		} else if (! lua_istable(L, 2))
			luaL_error(L, "box.io.readline: bad argument");
		break;
	case 3:
		/* readline(limit, timeout)
		 * readline(limit, {seplist})
		 * readline({seplist}, timeout) */
		if (lua_isnumber(L, 2)) {
			*limit = luaL_checkint(L, 2);
			if (lua_isnumber(L, 3)) {
				*timeout = luaL_checknumber(L, 3);
				lbox_io_readline_cr(L);
				seplist = 4;
				break;
			} else if (! lua_istable(L, 3))
				luaL_error(L, "box.io.readline: bad argument");
			seplist = 3;
			break;
		} else if (! lua_istable(L, 2))
			luaL_error(L, "box.io.readline: bad argument");
		*timeout = luaL_checknumber(L, 3);
		seplist = 2;
		break;
	case 4:
		/* readline(limit, {seplist}, timeout) */
		*limit = luaL_checkint(L, 2);
		if (! lua_istable(L, 3))
			luaL_error(L, "box.io.readline: bad argument");
		seplist = 3;
		*timeout = luaL_checknumber(L, 4);
		break;
	default:
		luaL_error(L, "box.io.readline: bad argument");
		break;
	}
	return seplist;
}

/*
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
 * Return:
 *
 * 1. sep:     data,         sep = str
 * 2. timeout: data = "",    sep = nil, status = "timeout", eno, estr
 * 3. error:   data = "",    sep = nil, status = "error",   eno, estr
 * 4. limit:   data = chunk, sep = nil, status = "limit",   eno, estr
 * 5. eof:     data = chunk, sep = nil, status = "eof",     eno, estr
 *
 */
static int
lbox_io_readline(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	bio_clearerr(s);

	unsigned int limit = UINT_MAX;
	double timeout = TIMEOUT_INFINITY;
	int seplist = lbox_io_readline_opts(L, &limit, &timeout);

	int rs_size = lua_objlen(L, seplist);
	if (rs_size == 0)
		luaL_error(L, "box.io.readline: bad separator table");

	size_t bottom = 0;
	int match;

	/* maybe init buffer, can throw ER_MEMORY_ISSUE. */
	bio_initbuf(s);

	@try {
		/* readline implementation uses a simple state machine
		 * to determine current position of a possible
		 * separator. */
		struct readline_state *rs =
			palloc(s->iob->in.pool, sizeof(struct readline_state) * rs_size);
		readline_state_init(L, rs, seplist);

		ev_tstamp start, delay;
		evio_timeout_init(&start, &delay, timeout);

		while (1) {

			/* case #4: user limit reached */
			if (bottom == limit) {
				lbox_io_readline_ret(L, s, s->iob->in.pos, bottom,
				                     BIO_LIMIT, 0);
				s->iob->in.pos += bottom;
				return 5;
			}

			/* if current read position (bottom) equals to
			 * the readahead size, then read new data. */
			if (bottom == ibuf_size(&s->iob->in)) {

				/* eof handling.
				 *
				 * In case of eof by coio_bread(), ensure that there
				 * is no new data aviailable, if so return processed
				 * part. Otherwise continue processing, until buffer
				 * is fully traversed.
				 */
eof:				if (s->eof) {
					lbox_io_readline_ret(L, s, s->iob->in.pos, bottom,
							     BIO_EOF, 0);
					s->iob->in.pos += bottom;
					return 5;
				}

				ssize_t nrd = coio_bread_timeout(&s->coio, &s->iob->in, 1,
						                 delay);
				/* case #5: eof (step 1)*/
				if (nrd == 0) {
					s->eof = true;
					if (ibuf_size(&s->iob->in) - bottom == 0)
						goto eof;
				}
			}

			match = readline_state_next(rs, rs_size, s->iob->in.pos[bottom]);
			bottom++;
			if (match >= 0)
				break;

			evio_timeout_update(start, &delay);
		}

	} @catch (SocketRWError *e) {
		s->error = errno;
		/* case #2-3: timedout and errors.
		 * leave all data untouched in the readahead buffer.
		 * */
		s->iob->in.end += e->n;

		enum bio_status st =
			(s->error == ETIMEDOUT) ? BIO_TIMEOUT :
			 BIO_ERROR;
		return lbox_io_readline_ret(L, s, NULL, 0, st, 0);
	}

	/* case #1: success, separator matched */
	lua_pushlstring(L, s->iob->in.pos, bottom);
	lua_pushinteger(L, match + 1);
	s->iob->in.pos += bottom;
	return 2;
}

/*
 * socket:bind(host, port [, timeout])
 *
 * Bind a socket to the given host.
 *
 * Return:
 *
 * 1. ok:      socket (self)
 * 2. error:   nil, status = "error",   eno, estr
 * 3. timeout: nil, status = "timeout", eno, estr
 *
 */
static int
lbox_io_bind(struct lua_State *L)
{
	struct bio_socket *s = bio_checksocket(L, 1);
	const char *host = luaL_checkstring(L, 2);
	const char *port = luaL_checkstring(L, 3);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 4)
		timeout = luaL_checknumber(L, 4);
	bio_clearerr(s);
	/* try to resolve a hostname */
	struct addrinfo *ai = bio_resolve(s->socktype, host, port, timeout);
	if (ai == NULL)
		return bio_pusherror(L, s, errno);
	@try {
		coio_bind_addrinfo(&s->coio, ai);
	} @catch (SocketError *e) {
		/* case #2: error */
		return bio_pusherror(L, s, errno);
	} @finally {
		freeaddrinfo(ai);
	}
	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/*
 * socket:listen()
 *
 * Marks the socket that it will be used to accept incoming
 * connection requests using socket:accept().
 *
 * Return:
 *
 * 1. ok:      socket (self)
 * 2. error:   nil, status = "error",   eno, estr
 */
static int
lbox_io_listen(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	bio_clearerr(s);
	if (listen(s->coio.fd, sio_listen_backlog()))
		return bio_pusherror(L, s, errno);
	lua_settop(L, 1);
	return 1;
}

/*
 * socket:accept([timeout])
 *
 * Wait for a new client connection and create connected
 * socket.
 *
 * 1. ok:      socket (client), address, port
 * 2. error:   nil, nil, nil, status = "error",   eno, estr
 * 3. timeout: nil, nil, nil, status = "timeout", eno, estr
 */
static int
lbox_io_accept(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 2)
		timeout = luaL_checknumber(L, 2);
	bio_clearerr(s);

	struct sockaddr_in addr;
	/* push client socket */
	bio_pushsocket(L, SOCK_STREAM);
	struct bio_socket *client = lua_touserdata(L, -1);
	@try {
		int fd = coio_accept(&s->coio, &addr, sizeof(addr), timeout);
		coio_init(&client->coio, fd);
	} @catch (SocketError *e) {
		/* case #2: error */
		lua_pushnil(L);
		lua_pushnil(L);
		return 2 + bio_pusherror(L, s, errno);
	}
	/* get user host and port */
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];
	int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
			     hbuf, sizeof(hbuf),
			     sbuf, sizeof(sbuf), NI_NUMERICHOST|NI_NUMERICSERV);
	if (rc != 0) {
		/* case #2: error */
		lua_pushnil(L);
		lua_pushnil(L);
		return 2 + bio_pusherror(L, s, ERESOLVE);
	}
	/* push host and port */
	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);
	return 3;
}

/*
 * socket:sendto(buf, host, port [, timeout])
 *
 * Send a message on a UDP socket to a specified host.
 *
 * Return:
 *
 * 1. ok:      socket (self)
 * 2. error:   nil, status = "error",   eno, estr
 * 3. timeout: nil, status = "timeout", eno, estr
 */
static int
lbox_io_sendto(struct lua_State *L)
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
	evio_timeout_init(&start, &delay, timeout);
	struct addrinfo *a = bio_resolve(s->socktype, host, port, delay);
	if (a == NULL)
		return bio_pusherror(L, s, errno);

	evio_timeout_update(start, &delay);
	@try {
		/* maybe init the socket */
		struct sockaddr_in *addr = (struct sockaddr_in *) a->ai_addr;
		if (! evio_is_active(&s->coio))
			coio_socket(&s->coio, addr->sin_family, s->socktype, 0);

		coio_sendto_timeout(&s->coio, buf, buf_size, 0,
				    addr, a->ai_addrlen, delay);
	} @catch (SocketError *e) {
		/* case #2-3: error or timeout */
		return bio_pusherror(L, s, errno);
	} @finally {
		freeaddrinfo(a);
	}
	/* case #1: Success */
	lua_settop(L, 1);
	return 1;
}

/*
 * socket:recvfrom(limit [, timeout])
 *
 * Receive a message on a UDP socket.
 *
 * Return:
 *
 * 1. ok:      data,      client_addr,       client_port
 * 2. error:   data = "", client_addr = nil, client_port = nil, status = "error",
 *             eno, estr
 * 3. timeout: data = "", client_addr = nil, client_port = nil, status = "timeout",
 *             eno, estr
 */
static int
lbox_io_recvfrom(struct lua_State *L)
{
	struct bio_socket *s = bio_checkactivesocket(L, 1);
	int buf_size = luaL_checkint(L, 2);
	double timeout = TIMEOUT_INFINITY;
	if (lua_gettop(L) == 3)
		timeout = luaL_checknumber(L, 3);
	bio_clearerr(s);

	/* maybe init buffer, can throw ER_MEMORY_ISSUE. */
	bio_initbuf(s);

	struct sockaddr_in addr;
	struct tbuf *buf;
	size_t nrd;
	@try {
		buf = tbuf_alloc(s->iob->in.pool);
		tbuf_ensure(buf, buf_size);

		nrd = coio_recvfrom_timeout(&s->coio, buf->data, buf_size, 0, &addr,
				            sizeof(addr), timeout);
	} @catch (SocketError *e) {
		/* case #2-3: error or timeout */
		lua_pushnil(L);
		lua_pushnil(L);
		return 2 + bio_pusherror(L, s, errno);
	}

	/* push host and port */
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];
	int rc = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
			     hbuf, sizeof(hbuf),
			     sbuf, sizeof(sbuf), NI_NUMERICHOST|NI_NUMERICSERV);
	if (rc != 0) {
		/* case #2: error */
		lua_pushnil(L);
		lua_pushnil(L);
		return 2 + bio_pusherror(L, s, ERESOLVE);
	}

	/* case #1: push received data */
	lua_pushlstring(L, buf->data, nrd);
	lua_pushstring(L, hbuf);
	lua_pushstring(L, sbuf);
	return 3;
}

void
tarantool_lua_io_init(struct lua_State *L)
{
	static const struct luaL_reg lbox_io_meta[] = {
		{"__gc", lbox_io_close},
		{"__tostring", lbox_io_tostring},
		{"error", lbox_io_error},
		{"close", lbox_io_close},
		{"shutdown", lbox_io_shutdown},
		{"connect", lbox_io_connect},
		{"write", lbox_io_write},
		{"read", lbox_io_read},
		{"readline", lbox_io_readline},
		{"bind", lbox_io_bind},
		{"listen", lbox_io_listen},
		{"accept", lbox_io_accept},
		{"sendto", lbox_io_sendto},
		{"recvfrom", lbox_io_recvfrom},
		{NULL, NULL}
	};
	static const struct luaL_reg iolib[] = {
		{"tcp", lbox_io_tcp},
		{"udp", lbox_io_udp},
		{NULL, NULL}
	};
	tarantool_lua_register_type(L, iolib_name, lbox_io_meta);
	luaL_register(L, iolib_name, iolib);
	lua_pop(L, 1);
}
