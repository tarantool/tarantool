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

#include "errno.h"
#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <say.h>

extern char errno_lua[];


void
tarantool_lua_errno_init(struct lua_State *L)
{
	static const struct { char name[32]; int value; } elist[] = {
		{ "E2BIG",		E2BIG		},
		{ "EACCES",		EACCES		},
		{ "EADDRINUSE",		EADDRINUSE	},
		{ "EADDRNOTAVAIL",	EADDRNOTAVAIL	},
		{ "EAFNOSUPPORT",	EAFNOSUPPORT	},
		{ "EAGAIN",		EAGAIN		},
		{ "EALREADY",		EALREADY	},
		{ "EBADF",		EBADF		},
		{ "EBADMSG",		EBADMSG		},
		{ "EBUSY",		EBUSY		},
		{ "ECANCELED",		ECANCELED	},
		{ "ECHILD",		ECHILD		},
		{ "ECONNABORTED",	ECONNABORTED	},
		{ "ECONNREFUSED",	ECONNREFUSED	},
		{ "ECONNRESET",		ECONNRESET	},
		{ "EDEADLK",		EDEADLK		},
		{ "EDESTADDRREQ",	EDESTADDRREQ	},
		{ "EDOM",		EDOM		},
		{ "EDQUOT",		EDQUOT		},
		{ "EEXIST",		EEXIST		},
		{ "EFAULT",		EFAULT		},
		{ "EFBIG",		EFBIG		},
		{ "EHOSTUNREACH",	EHOSTUNREACH	},
		{ "EIDRM",		EIDRM		},
		{ "EILSEQ",		EILSEQ		},
		{ "EINPROGRESS",	EINPROGRESS	},
		{ "EINTR",		EINTR		},
		{ "EINVAL",		EINVAL		},
		{ "EIO",		EIO		},
		{ "EISCONN",		EISCONN		},
		{ "EISDIR",		EISDIR		},
		{ "ELOOP",		ELOOP		},
		{ "EMFILE",		EMFILE		},
		{ "EMLINK",		EMLINK		},
		{ "EMSGSIZE",		EMSGSIZE	},
		{ "EMULTIHOP",		EMULTIHOP	},
		{ "ENAMETOOLONG",	ENAMETOOLONG	},
		{ "ENETDOWN",		ENETDOWN	},
		{ "ENETRESET",		ENETRESET	},
		{ "ENETUNREACH",	ENETUNREACH	},
		{ "ENFILE",		ENFILE		},
		{ "ENOBUFS",		ENOBUFS		},
		{ "ENODATA",		ENODATA		},
		{ "ENODEV",		ENODEV		},
		{ "ENOENT",		ENOENT		},
		{ "ENOEXEC",		ENOEXEC		},
		{ "ENOLCK",		ENOLCK		},
		{ "ENOLINK",		ENOLINK		},
		{ "ENOMEM",		ENOMEM		},
		{ "ENOMSG",		ENOMSG		},
		{ "ENOPROTOOPT",	ENOPROTOOPT	},
		{ "ENOSPC",		ENOSPC		},
		{ "ENOSR",		ENOSR		},
		{ "ENOSTR",		ENOSTR		},
		{ "ENOSYS",		ENOSYS		},
		{ "ENOTCONN",		ENOTCONN	},
		{ "ENOTDIR",		ENOTDIR		},
		{ "ENOTEMPTY",		ENOTEMPTY	},
		{ "ENOTSOCK",		ENOTSOCK	},
		{ "ENOTSUP",		ENOTSUP		},
		{ "ENOTTY",		ENOTTY		},
		{ "ENXIO",		ENXIO		},
		{ "EOPNOTSUPP",		EOPNOTSUPP	},
		{ "EOVERFLOW",		EOVERFLOW	},
		{ "EPERM",		EPERM		},
		{ "EPIPE",		EPIPE		},
		{ "EPROTO",		EPROTO		},
		{ "EPROTONOSUPPORT",	EPROTONOSUPPORT	},
		{ "EPROTOTYPE",		EPROTOTYPE	},
		{ "ERANGE",		ERANGE		},
		{ "EROFS",		EROFS		},
		{ "ESPIPE",		ESPIPE		},
		{ "ESRCH",		ESRCH		},
		{ "ESTALE",		ESTALE		},
		{ "ETIME",		ETIME		},
		{ "ETIMEDOUT",		ETIMEDOUT	},
		{ "ETXTBSY",		ETXTBSY		},
		{ "EWOULDBLOCK",	EWOULDBLOCK	},
		{ "EXDEV",		EXDEV		},
		{ "",			0		}
	};

	int top = lua_gettop(L);
	lua_getfield(L, LUA_GLOBALSINDEX, "box");
	lua_pushliteral(L, "errno");
	lua_newtable(L);
	for (int i = 0; elist[i].name[0]; i++) {
		lua_pushstring(L, elist[i].name);
		lua_pushinteger(L, elist[i].value);
		lua_rawset(L, -3);
	}
	lua_rawset(L, -3);

	if (luaL_dostring(L, errno_lua))
		panic("Error loading Lua source (internal)/errno.lua: %s",
			      lua_tostring(L, -1));

	lua_settop(L, top);
}

int
errno_get(void)
{
	return errno;
}

int
errno_set(int new_errno)
{
	errno = new_errno;
	return errno;
}
