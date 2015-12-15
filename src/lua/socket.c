/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
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

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <coio.h> /* coio_wait() */
#include <coeio.h> /* coio_getaddrinfo() */
#include <fiber.h>
#include "lua/utils.h"

extern int coio_wait(int fd, int event, double timeout);

extern char socket_lua[];

static const struct { char name[32]; int value; } domains[] = {
#ifdef	PF_UNIX
	{ "PF_UNIX",		PF_UNIX		},
#endif
#ifdef	PF_LOCAL
	{ "PF_LOCAL",		PF_LOCAL	},
#endif
#ifdef	PF_INET
	{ "PF_INET",		PF_INET		},
#endif
#ifdef	PF_INET6
	{ "PF_INET6",		PF_INET6	},
#endif
#ifdef	PF_IPX
	{ "PF_IPX",		PF_IPX		},
#endif
#ifdef	PF_NETLINK
	{ "PF_NETLINK",		PF_NETLINK	},
#endif
#ifdef	PF_X25
	{ "PF_X25",		PF_X25		},
#endif
#ifdef	PF_AX25
	{ "PF_AX25",		PF_AX25		},
#endif
#ifdef	PF_ATMPVC
	{ "PF_ATMPVC",		PF_ATMPVC	},
#endif
#ifdef	PF_APPLETALK
	{ "PF_APPLETALK",	PF_APPLETALK	},
#endif
#ifdef	PF_PACKET
	{ "PF_PACKET",		PF_PACKET	},
#endif
	{ "", 0 }
};

static const struct { char name[32]; int value; } types[] = {
#ifdef	SOCK_STREAM
	{ "SOCK_STREAM",	SOCK_STREAM	},
#endif
#ifdef	SOCK_DGRAM
	{ "SOCK_DGRAM",		SOCK_DGRAM	},
#endif
#ifdef	SOCK_SEQPACKET
	{ "SOCK_SEQPACKET",	SOCK_SEQPACKET	},
#endif
#ifdef	SOCK_RAW
	{ "SOCK_RAW",		SOCK_RAW	},
#endif
#ifdef	SOCK_RDM
	{ "SOCK_RDM",		SOCK_RDM	},
#endif
	{ "", 0 }
};

static const struct { char name[32]; int value; } send_flags[] = {
#ifdef MSG_OOB
	{"MSG_OOB",		MSG_OOB		},
#endif
#ifdef MSG_PEEK
	{"MSG_PEEK",		MSG_PEEK	},
#endif
#ifdef MSG_DONTROUTE
	{"MSG_DONTROUTE",	MSG_DONTROUTE	},
#endif
#ifdef MSG_TRYHARD
	{"MSG_TRYHARD",		MSG_TRYHARD	},
#endif
#ifdef MSG_CTRUNC
	{"MSG_CTRUNC",		MSG_CTRUNC	},
#endif
#ifdef MSG_PROXY
	{"MSG_PROXY",		MSG_PROXY	},
#endif
#ifdef MSG_TRUNC
	{"MSG_TRUNC",		MSG_TRUNC	},
#endif
#ifdef MSG_DONTWAIT
	{"MSG_DONTWAIT",	MSG_DONTWAIT	},
#endif
#ifdef MSG_EOR
	{"MSG_EOR",		MSG_EOR		},
#endif
#ifdef MSG_WAITALL
	{"MSG_WAITALL",		MSG_WAITALL	},
#endif
#ifdef MSG_FIN
	{"MSG_FIN",		MSG_FIN		},
#endif
#ifdef MSG_SYN
	{"MSG_SYN",		MSG_SYN		},
#endif
#ifdef MSG_CONFIRM
	{"MSG_CONFIRM",		MSG_CONFIRM	},
#endif
#ifdef MSG_RST
	{"MSG_RST",		MSG_RST		},
#endif
#ifdef MSG_ERRQUEUE
	{"MSG_ERRQUEUE",	MSG_ERRQUEUE	},
#endif
#ifdef MSG_NOSIGNAL
	{"MSG_NOSIGNAL",	MSG_NOSIGNAL	},
#endif
#ifdef MSG_MORE
	{"MSG_MORE",		MSG_MORE	},
#endif
#ifdef MSG_WAITFORONE
	{"MSG_WAITFORONE",	MSG_WAITFORONE	},
#endif
#ifdef MSG_FASTOPEN
	{"MSG_FASTOPEN",	MSG_FASTOPEN	},
#endif
#ifdef MSG_CMSG_CLOEXEC
	{"MSG_CMSG_CLOEXEC",	MSG_CMSG_CLOEXEC},
#endif
	{ "",			0		}
};

static const struct { char name[32]; int value, type, rw; } so_opts[] = {
#ifdef SO_ACCEPTCONN
	{"SO_ACCEPTCONN",	SO_ACCEPTCONN,		1,	0, },
#endif
#ifdef SO_BINDTODEVICE
	{"SO_BINDTODEVICE",	SO_BINDTODEVICE,	2,	1, },
#endif
#ifdef SO_BROADCAST
	{"SO_BROADCAST",	SO_BROADCAST,		1,	1, },
#endif
#ifdef SO_DEBUG
	{"SO_DEBUG",		SO_DEBUG,		1,	1, },
#endif
#ifdef SO_DOMAIN
	{"SO_DOMAIN",		SO_DOMAIN,		1,	0, },
#endif
#ifdef SO_ERROR
	{"SO_ERROR",		SO_ERROR,		1,	0, },
#endif
#ifdef SO_DONTROUTE
	{"SO_DONTROUTE",	SO_DONTROUTE,		1,	1, },
#endif
#ifdef SO_KEEPALIVE
	{"SO_KEEPALIVE",	SO_KEEPALIVE,		1,	1, },
#endif
#ifdef SO_LINGER
	{"SO_LINGER",		SO_LINGER,		0,	0, },
#endif
#ifdef SO_MARK
	{"SO_MARK",		SO_MARK,		1,	1, },
#endif
#ifdef SO_OOBINLINE
	{"SO_OOBINLINE",	SO_OOBINLINE,		1,	1, },
#endif
#ifdef SO_PASSCRED
	{"SO_PASSCRED",		SO_PASSCRED,		1,	1, },
#endif
#ifdef SO_PEERCRED
	{"SO_PEERCRED",		SO_PEERCRED,		1,	0, },
#endif
#ifdef SO_PRIORITY
	{"SO_PRIORITY",		SO_PRIORITY,		1,	1, },
#endif
#ifdef SO_RCVBUF
	{"SO_RCVBUF",		SO_RCVBUF,		1,	1, },
#endif
#ifdef SO_RCVBUFFORCE
	{"SO_RCVBUFFORCE",	SO_RCVBUFFORCE,		1,	1, },
#endif
#ifdef SO_RCVLOWAT
	{"SO_RCVLOWAT",		SO_RCVLOWAT,		1,	1, },
#endif
#ifdef SO_SNDLOWAT
	{"SO_SNDLOWAT",		SO_SNDLOWAT,		1,	1, },
#endif
#ifdef SO_RCVTIMEO
	{"SO_RCVTIMEO",		SO_RCVTIMEO,		1,	1, },
#endif
#ifdef SO_SNDTIMEO
	{"SO_SNDTIMEO",		SO_SNDTIMEO,		1,	1, },
#endif
#ifdef SO_REUSEADDR
	{"SO_REUSEADDR",	SO_REUSEADDR,		1,	1, },
#endif
#ifdef SO_SNDBUF
	{"SO_SNDBUF",		SO_SNDBUF,		1,	1, },
#endif
#ifdef SO_SNDBUFFORCE
	{"SO_SNDBUFFORCE",	SO_SNDBUFFORCE,		1,	1, },
#endif
#ifdef SO_TIMESTAMP
	{"SO_TIMESTAMP",	SO_TIMESTAMP,		1,	1, },
#endif
#ifdef SO_PROTOCOL
	{"SO_PROTOCOL",		SO_PROTOCOL,		1,	0, },
#endif

	{"SO_TYPE",		SO_TYPE,		1,	0, },

	{"",			0,			0,	0, }
};

static const struct { char name[32]; int value; } ai_flags[] = {
#ifdef	AI_PASSIVE
	{"AI_PASSIVE",			AI_PASSIVE			},
#endif
#ifdef	AI_CANONNAME
	{"AI_CANONNAME",		AI_CANONNAME			},
#endif
#ifdef	AI_NUMERICHOST
	{"AI_NUMERICHOST",		AI_NUMERICHOST			},
#endif
#ifdef	AI_V4MAPPED
	{"AI_V4MAPPED",			AI_V4MAPPED			},
#endif
#ifdef	AI_ALL
	{"AI_ALL",			AI_ALL				},
#endif
#ifdef	AI_ADDRCONFIG
	{"AI_ADDRCONFIG",		AI_ADDRCONFIG			},
#endif
#ifdef AI_IDN
	{"AI_IDN",			AI_IDN				},
#endif
#ifdef AI_CANONIDN
	{"AI_CANONIDN",			AI_CANONIDN			},
#endif
#ifdef AI_IDN_ALLOW_UNASSIGNED
	{"AI_IDN_ALLOW_UNASSIGNED",	AI_IDN_ALLOW_UNASSIGNED		},
#endif
#ifdef AI_IDN_USE_STD3_ASCII_RULES
	{"AI_IDN_USE_STD3_ASCII_RULES",	AI_IDN_USE_STD3_ASCII_RULES	},
#endif
#ifdef AI_NUMERICSERV
	{"AI_NUMERICSERV",		AI_NUMERICSERV			},
#endif
	{"",				0				}
};

int
lbox_socket_local_resolve(const char *host, const char *port,
		     struct sockaddr *addr, socklen_t *socklen)
{
	if (strcmp(host, "unix/") == 0) {
		struct sockaddr_un *uaddr = (struct sockaddr_un *) addr;
		if (*socklen < sizeof(*uaddr)) {
			errno = ENOBUFS;
			return -1;
		}
		memset(uaddr, 0, sizeof(*uaddr));
		uaddr->sun_family = AF_UNIX;
		strncpy(uaddr->sun_path, port, sizeof(uaddr->sun_path));
		*socklen = sizeof(*uaddr);
		return 0;
	}

	/* IPv4 */
	in_addr_t iaddr = inet_addr(host);
	if (iaddr != (in_addr_t)(-1)) {
		struct sockaddr_in *inaddr = (struct sockaddr_in *) addr;
		if (*socklen < sizeof(*inaddr)) {
			errno = ENOBUFS;
			return -1;
		}
		memset(inaddr, 0, sizeof(*inaddr));
		inaddr->sin_family = AF_INET;
		inaddr->sin_addr.s_addr = iaddr;
		inaddr->sin_port = htons(atoi(port));
		*socklen = sizeof(*inaddr);
		return 0;
	}

	/* IPv6 */
	struct in6_addr ipv6;
	if (inet_pton(AF_INET6, host, &ipv6) == 1) {
		struct sockaddr_in6 *inaddr6 = (struct sockaddr_in6 *) addr;
		if (*socklen < sizeof(*inaddr6)) {
			errno = ENOBUFS;
			return -1;
		}
		memset(inaddr6, 0, sizeof(*inaddr6));
		inaddr6->sin6_family = AF_INET6;
		inaddr6->sin6_port = htons(atoi(port));
		memcpy(inaddr6->sin6_addr.s6_addr, &ipv6, sizeof(ipv6));
		*socklen = sizeof(*inaddr6);
		return 0;
	}

	errno = EINVAL;
	return -1;
}

int
lbox_socket_nonblock(int fh, int mode)
{
	int flags = fcntl(fh, F_GETFL, 0);
	if (flags < 0)
		return -1;

	/* GET */
	if (mode == 0x80) {
		if (flags & O_NONBLOCK)
			return 1;
		else
			return 0;
	}

	if (mode) {
		if (flags & O_NONBLOCK)
			return 1;
		flags |= O_NONBLOCK;
	} else {
		if ((flags & O_NONBLOCK) == 0)
			return 0;
		flags &= ~O_NONBLOCK;
	}
	flags = fcntl(fh, F_SETFL, flags);
	if (flags < 0)
		return -1;

	return mode ? 1 : 0;
}

static int
lbox_socket_iowait(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		goto usage;
	int fh = lua_tointeger(L, 1);
	if (fh < 0)
		goto usage;
	ev_tstamp timeout = luaL_optnumber(L, 3, TIMEOUT_INFINITY);
	if (timeout < 0)
		goto usage;

	if (likely(lua_type(L, 2) == LUA_TNUMBER)) {
		/* Fast path: `events' is a bitmask of (COIO_READ|COIO_WRITE) */
		int events = lua_tointeger(L, 2);
		if (events <= 0 || events > (COIO_READ | COIO_WRITE))
			goto usage;
		int ret = coio_wait(fh, events, timeout);
		lua_pushinteger(L, ret);
		return 1;
	}

	/* Сonvenient version: `events' is a string ('R', 'W', 'RW') */
	int events = 0;
	const char *events_str = lua_tostring(L, 2);
	if (events_str == NULL)
		goto usage;
	for (const char *e = events_str; *e != '\0'; ++e) {
		/* Lower-case is needed to simplify integration with cqueues */
		switch (*e) {
		case 'r':
		case 'R':
			events |= COIO_READ;
			break;
		case 'w':
		case 'W':
			events |= COIO_WRITE;
			break;
		default:
			goto usage;
		}
	}
	if (events == 0)
		goto usage;
	int ret = coio_wait(fh, events, timeout);
	const char *result[] = { "", "R", "W", "RW" };
	assert(ret <= (COIO_READ | COIO_WRITE));
	lua_pushstring(L, result[ret]);
	return 1;

usage:
	return luaL_error(L, "Usage: iowait(fd, 1 | 'r' | 2 | 'w' | 3 | 'rw'"
			" [, timeout])");
}

static int
lbox_socket_push_family(struct lua_State *L, int family)
{
	switch (family) {
#ifdef	AF_UNIX
	case AF_UNIX:
		lua_pushliteral(L, "AF_UNIX");
		break;
#endif
#ifdef	AF_INET
	case AF_INET:
		lua_pushliteral(L, "AF_INET");
		break;
#endif
#ifdef	AF_INET6
	case AF_INET6:
		lua_pushliteral(L, "AF_INET6");
		break;
#endif
#ifdef	AF_IPX
	case AF_IPX:
		lua_pushliteral(L, "AF_IPX");
		break;
#endif
#ifdef	AF_NETLINK
	case AF_NETLINK:
		lua_pushliteral(L, "AF_NETLINK");
		break;
#endif
#ifdef AF_X25
	case AF_X25:
		lua_pushliteral(L, "AF_X25");
		break;
#endif
#ifdef	AF_AX25
	case AF_AX25:
		lua_pushliteral(L, "AF_AX25");
		break;
#endif
#ifdef	AF_ATMPVC
	case AF_ATMPVC:
		lua_pushliteral(L, "AF_ATMPVC");
		break;
#endif
#ifdef	AF_APPLETALK
	case AF_APPLETALK:
		lua_pushliteral(L, "AF_APPLETALK");
		break;
#endif
#ifdef	AF_PACKET
	case AF_PACKET:
		lua_pushliteral(L, "AF_PACKET");
		break;
#endif

	default:
		lua_pushinteger(L, family);
		break;
	}
	return 1;
}

static int
lbox_socket_push_protocol(struct lua_State *L, int protonumber)
{
	if (protonumber == 0) {
		lua_pushinteger(L, 0);
		return 1;
	}

	struct protoent *p = getprotobynumber(protonumber);
	if (p) {
		lua_pushstring(L, p->p_name);
	} else {
		lua_pushinteger(L, protonumber);
	}
	return 1;
}

static int
lbox_socket_push_sotype(struct lua_State *L, int sotype)
{
	/* man 7 socket says that sotype can contain some flags */
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	sotype &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
	switch (sotype) {
#ifdef SOCK_STREAM
	case SOCK_STREAM:
		lua_pushliteral(L, "SOCK_STREAM");
		break;
#endif
#ifdef SOCK_DGRAM
	case SOCK_DGRAM:
		lua_pushliteral(L, "SOCK_DGRAM");
		break;
#endif
#ifdef SOCK_SEQPACKET
	case SOCK_SEQPACKET:
		lua_pushliteral(L, "SOCK_SEQPACKET");
		break;
#endif
#ifdef SOCK_RAW
	case SOCK_RAW:
		lua_pushliteral(L, "SOCK_RAW");
		break;
#endif
#ifdef SOCK_RDM
	case SOCK_RDM:
		lua_pushliteral(L, "SOCK_RDM");
		break;
#endif
#ifdef SOCK_PACKET
	case SOCK_PACKET:
		lua_pushliteral(L, "SOCK_PACKET");
		break;
#endif
	default:
		lua_pushinteger(L, sotype);
		break;
	}
	return 1;
}

static int
lbox_socket_push_addr(struct lua_State *L,
			 const struct sockaddr *addr, socklen_t alen)
{
	lua_newtable(L);

	lua_pushliteral(L, "family");
	lbox_socket_push_family(L, addr->sa_family);
	lua_rawset(L, -3);

	switch (addr->sa_family) {
	case PF_INET:
	case PF_INET6: {
		char shost[NI_MAXHOST];
		char sservice[NI_MAXSERV];
		int rc = getnameinfo(addr,
				     alen,
				     shost, sizeof(shost),
				     sservice, sizeof(sservice),
				     NI_NUMERICHOST|NI_NUMERICSERV
				    );

		if (rc == 0) {
			lua_pushliteral(L, "host");
			lua_pushstring(L, shost);
			lua_rawset(L, -3);

			lua_pushliteral(L, "port");
			lua_pushinteger(L, atol(sservice));
			lua_rawset(L, -3);
		}

		break;
	}

	case PF_UNIX:
		lua_pushliteral(L, "host");
		lua_pushliteral(L, "unix/");
		lua_rawset(L, -3);

		if (alen > sizeof(addr->sa_family)) {
			lua_pushliteral(L, "port");
			lua_pushstring(L,
				       ((struct sockaddr_un *)addr)->sun_path);
			lua_rawset(L, -3);
		} else {
			lua_pushliteral(L, "port");
			lua_pushliteral(L, "");
			lua_rawset(L, -3);
		}
		break;

	default:	/* unknown family */
		lua_pop(L, 1);
		lua_pushnil(L);
		break;
	}

	return 1;
}

static int
lbox_socket_getaddrinfo(struct lua_State *L)
{
	assert(lua_gettop(L) == 4);
	lua_pushvalue(L, 1);
	const char *host = lua_tostring(L, -1);
	lua_pushvalue(L, 2);
	const char *port = lua_tostring(L, -1);

	ev_tstamp timeout = lua_tonumber(L, 3);

	struct addrinfo hints, *result = NULL;
	memset(&hints, 0, sizeof(hints));

	if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "family");
		if (lua_isnumber(L, -1))
			hints.ai_family = lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 4, "type");
		if (lua_isnumber(L, -1))
			hints.ai_socktype = lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 4, "protocol");
		if (lua_isnumber(L, -1))
			hints.ai_protocol = lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 4, "flags");
		if (lua_isnumber(L, -1))
			hints.ai_flags = lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	int dns_res = 0;
	dns_res = coio_getaddrinfo(host, port, &hints, &result, timeout);
	lua_pop(L, 2);	/* host, port */

	if (dns_res != 0) {
		lua_pushnil(L);
		return 1;
	}

	/* no results */
	if (!result) {
		lua_newtable(L);
		return 1;
	}

	lua_newtable(L);
	int i = 1;
	for (struct addrinfo *rp = result; rp; rp = rp->ai_next, i++) {
		lua_pushinteger(L, i);

		lbox_socket_push_addr(L, rp->ai_addr, rp->ai_addrlen);

		if (lua_isnil(L, -1)) {
			lua_pop(L, 2);
			i--;
			continue;
		}

		lua_pushliteral(L, "protocol");
		lbox_socket_push_protocol(L, rp->ai_protocol);
		lua_rawset(L, -3);

		lua_pushliteral(L, "type");
		lbox_socket_push_sotype(L, rp->ai_socktype);
		lua_rawset(L, -3);

		if (rp->ai_canonname) {
			lua_pushliteral(L, "canonname");
			lua_pushstring(L, rp->ai_canonname);
			lua_rawset(L, -3);
		}

		lua_rawset(L, -3);

	}
	/*
	 * XXX: this leaks freeaddrinfo() if anything in the loop
	 * above causes a Lua error.
	 */
	freeaddrinfo(result);
	return 1;
}

static int
lbox_socket_name(struct lua_State *L,
                    int (*getname_func) (int, struct sockaddr *, socklen_t *))
{
	lua_pushvalue(L, 1);
	int fh = lua_tointeger(L, -1);
	lua_pop(L, 1);

	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	if (getname_func(fh, (struct sockaddr *)&addr, &len) != 0) {
		lua_pushnil(L);
		return 1;
	}
	lbox_socket_push_addr(L, (const struct sockaddr *)&addr, len);
	if (lua_isnil(L, -1))
		return 1;

	int type;
	len = sizeof(type);
	if (getsockopt(fh, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
		lua_pushliteral(L, "type");
		lbox_socket_push_sotype(L, type);
		lua_rawset(L, -3);
	} else {
		type = -1;
	}

	int protocol = 0;
#ifdef SO_PROTOCOL
    len = sizeof(protocol);
	if (getsockopt(fh, SOL_SOCKET, SO_PROTOCOL, &protocol, &len) == 0) {
		lua_pushliteral(L, "protocol");
		lbox_socket_push_protocol(L, protocol);
		lua_rawset(L, -3);
	}
#else
	if (addr.ss_family == AF_INET || addr.ss_family == AF_INET6) {
		if (type == SOCK_STREAM)
			protocol = IPPROTO_TCP;
		if (type == SOCK_DGRAM)
			protocol = IPPROTO_UDP;
	}
	lua_pushliteral(L, "protocol");
	lbox_socket_push_protocol(L, protocol);
	lua_rawset(L, -3);
#endif
	return 1;
}

static int
lbox_socket_soname(struct lua_State *L)
{
	return lbox_socket_name(L, getsockname);
}

static int
lbox_socket_peername(struct lua_State *L)
{
	return lbox_socket_name(L, getpeername);
}

static int
lbox_socket_accept(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);

	struct sockaddr_storage fa;
	socklen_t len = sizeof(fa);

	int sc = accept(fh, (struct sockaddr*)&fa, &len);
	if (sc < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
			say_syserror("accept(%d)", fh);
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, sc);
	lbox_socket_push_addr(L, (struct sockaddr *)&fa, len);
	return 2;
}

static int
lbox_socket_recvfrom(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	int size = lua_tointeger(L, 2);
	int flags = lua_tointeger(L, 3);

	struct sockaddr_storage fa;
	socklen_t len = sizeof(fa);

	char *buf = (char *)malloc(size);
	if (!buf) {
		errno = ENOMEM;
		lua_pushnil(L);
		return 1;
	}

	ssize_t res = recvfrom(fh, buf, size, flags,
			       (struct sockaddr*)&fa, &len);

	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, buf, res);
	/*
	 * XXX: this leaks in case of pushlstring() error.
	 */
	free(buf);
	lbox_socket_push_addr(L, (struct sockaddr *)&fa, len);
	return 2;
}

/**
 * A special method to abort fiber blocked by iowait() by fid.
 * Used only by socket:close().
 */
static int
lbox_socket_abort(struct lua_State *L)
{
	int fid = lua_tointeger(L, 1);
	struct fiber *fiber = fiber_find(fid);
	if (fiber == NULL)
		return 0;
	fiber_wakeup(fiber);
	return 0;
}

void
tarantool_lua_socket_init(struct lua_State *L)
{
	static const struct luaL_Reg internal_methods[] = {
		{ "iowait",		lbox_socket_iowait	},
		{ "getaddrinfo",	lbox_socket_getaddrinfo	},
		{ "name",		lbox_socket_soname	},
		{ "peer",		lbox_socket_peername	},
		{ "recvfrom",		lbox_socket_recvfrom	},
		{ "abort",		lbox_socket_abort	},
		{ "accept",		lbox_socket_accept	},
		{ NULL,			NULL			}
	};

	luaL_register_module(L, "socket", internal_methods);

	/* domains table */
	lua_pushliteral(L, "DOMAIN");
	lua_newtable(L);
	for (int i = 0; domains[i].name[0]; i++) {
		lua_pushstring(L, domains[i].name);
		lua_pushinteger(L, domains[i].value);
		lua_rawset(L, -3);
		lua_pushliteral(L, "AF_");  /* Add AF_ alias */
		lua_pushstring(L, domains[i].name + 3);
		lua_concat(L, 2);
		lua_pushinteger(L, domains[i].value);
		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	/* SO_TYPE */
	lua_pushliteral(L, "SO_TYPE");
	lua_newtable(L);
	for (int i = 0; types[i].name[0]; i++) {
		lua_pushstring(L, types[i].name);
		lua_pushinteger(L, types[i].value);
		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	/* SEND_FLAGS */
	lua_pushliteral(L, "SEND_FLAGS");
	lua_newtable(L);
	for (int i = 0; send_flags[i].name[0]; i++) {
		lua_pushstring(L, send_flags[i].name);
		lua_pushinteger(L, send_flags[i].value);
		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	/* AI_FLAGS */
	lua_pushliteral(L, "AI_FLAGS");
	lua_newtable(L);
	for (int i = 0; ai_flags[i].name[0]; i++) {
		lua_pushstring(L, ai_flags[i].name);
		lua_pushinteger(L, ai_flags[i].value);
		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	/* SO_OPT */
	lua_pushliteral(L, "SO_OPT");
	lua_newtable(L);
	for (int i = 0; so_opts[i].name[0]; i++) {
		lua_pushstring(L, so_opts[i].name);
		lua_newtable(L);

		lua_pushliteral(L, "iname");
		lua_pushinteger(L, so_opts[i].value);
		lua_rawset(L, -3);

		lua_pushliteral(L, "type");
		lua_pushinteger(L, so_opts[i].type);
		lua_rawset(L, -3);

		lua_pushliteral(L, "rw");
		lua_pushboolean(L, so_opts[i].rw);
		lua_rawset(L, -3);

		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	/* constants */
	lua_pushliteral(L, "SOL_SOCKET");
	lua_pushinteger(L, SOL_SOCKET);
	lua_rawset(L, -3);

	lua_pop(L, 1); /* socket.internal */
}
