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

#include "errno.h"
#include <errno.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <say.h>
#include "lua/utils.h"

extern char errno_lua[];

void
tarantool_lua_errno_init(struct lua_State *L)
{
	static const struct { char *name; int value; } elist[] = {

#ifdef	E2BIG
		{ "E2BIG",		E2BIG		},
#endif
#ifdef	EACCES
		{ "EACCES",		EACCES		},
#endif
#ifdef	EADDRINUSE
		{ "EADDRINUSE",		EADDRINUSE	},
#endif
#ifdef	EADDRNOTAVAIL
		{ "EADDRNOTAVAIL",	EADDRNOTAVAIL	},
#endif
#ifdef	EAFNOSUPPORT
		{ "EAFNOSUPPORT",	EAFNOSUPPORT	},
#endif
#ifdef	EAGAIN
		{ "EAGAIN",		EAGAIN		},
#endif
#ifdef	EALREADY
		{ "EALREADY",		EALREADY	},
#endif
#ifdef	EBADF
		{ "EBADF",		EBADF		},
#endif
#ifdef	EBADMSG
		{ "EBADMSG",		EBADMSG		},
#endif
#ifdef	EBUSY
		{ "EBUSY",		EBUSY		},
#endif
#ifdef	ECANCELED
		{ "ECANCELED",		ECANCELED	},
#endif
#ifdef	ECHILD
		{ "ECHILD",		ECHILD		},
#endif
#ifdef	ECONNABORTED
		{ "ECONNABORTED",	ECONNABORTED	},
#endif
#ifdef	ECONNREFUSED
		{ "ECONNREFUSED",	ECONNREFUSED	},
#endif
#ifdef	ECONNRESET
		{ "ECONNRESET",		ECONNRESET	},
#endif
#ifdef	EDEADLK
		{ "EDEADLK",		EDEADLK		},
#endif
#ifdef	EDESTADDRREQ
		{ "EDESTADDRREQ",	EDESTADDRREQ	},
#endif
#ifdef	EDOM
		{ "EDOM",		EDOM		},
#endif
#ifdef	EDQUOT
		{ "EDQUOT",		EDQUOT		},
#endif
#ifdef	EEXIST
		{ "EEXIST",		EEXIST		},
#endif
#ifdef	EFAULT
		{ "EFAULT",		EFAULT		},
#endif
#ifdef	EFBIG
		{ "EFBIG",		EFBIG		},
#endif
#ifdef	EHOSTUNREACH
		{ "EHOSTUNREACH",	EHOSTUNREACH	},
#endif
#ifdef	EIDRM
		{ "EIDRM",		EIDRM		},
#endif
#ifdef	EILSEQ
		{ "EILSEQ",		EILSEQ		},
#endif
#ifdef	EINPROGRESS
		{ "EINPROGRESS",	EINPROGRESS	},
#endif
#ifdef	EINTR
		{ "EINTR",		EINTR		},
#endif
#ifdef	EINVAL
		{ "EINVAL",		EINVAL		},
#endif
#ifdef	EIO
		{ "EIO",		EIO		},
#endif
#ifdef	EISCONN
		{ "EISCONN",		EISCONN		},
#endif
#ifdef	EISDIR
		{ "EISDIR",		EISDIR		},
#endif
#ifdef	ELOOP
		{ "ELOOP",		ELOOP		},
#endif
#ifdef	EMFILE
		{ "EMFILE",		EMFILE		},
#endif
#ifdef	EMLINK
		{ "EMLINK",		EMLINK		},
#endif
#ifdef	EMSGSIZE
		{ "EMSGSIZE",		EMSGSIZE	},
#endif
#ifdef	EMULTIHOP
		{ "EMULTIHOP",		EMULTIHOP	},
#endif
#ifdef	ENAMETOOLONG
		{ "ENAMETOOLONG",	ENAMETOOLONG	},
#endif
#ifdef	ENETDOWN
		{ "ENETDOWN",		ENETDOWN	},
#endif
#ifdef	ENETRESET
		{ "ENETRESET",		ENETRESET	},
#endif
#ifdef	ENETUNREACH
		{ "ENETUNREACH",	ENETUNREACH	},
#endif
#ifdef	ENFILE
		{ "ENFILE",		ENFILE		},
#endif
#ifdef	ENOBUFS
		{ "ENOBUFS",		ENOBUFS		},
#endif
#ifdef	ENODATA
		{ "ENODATA",		ENODATA		},
#endif
#ifdef	ENODEV
		{ "ENODEV",		ENODEV		},
#endif
#ifdef	ENOENT
		{ "ENOENT",		ENOENT		},
#endif
#ifdef	ENOEXEC
		{ "ENOEXEC",		ENOEXEC		},
#endif
#ifdef	ENOLCK
		{ "ENOLCK",		ENOLCK		},
#endif
#ifdef	ENOLINK
		{ "ENOLINK",		ENOLINK		},
#endif
#ifdef	ENOMEM
		{ "ENOMEM",		ENOMEM		},
#endif
#ifdef	ENOMSG
		{ "ENOMSG",		ENOMSG		},
#endif
#ifdef	ENOPROTOOPT
		{ "ENOPROTOOPT",	ENOPROTOOPT	},
#endif
#ifdef	ENOSPC
		{ "ENOSPC",		ENOSPC		},
#endif
#ifdef	ENOSR
		{ "ENOSR",		ENOSR		},
#endif
#ifdef	ENOSTR
		{ "ENOSTR",		ENOSTR		},
#endif
#ifdef	ENOSYS
		{ "ENOSYS",		ENOSYS		},
#endif
#ifdef	ENOTCONN
		{ "ENOTCONN",		ENOTCONN	},
#endif
#ifdef	ENOTDIR
		{ "ENOTDIR",		ENOTDIR		},
#endif
#ifdef	ENOTEMPTY
		{ "ENOTEMPTY",		ENOTEMPTY	},
#endif
#ifdef	ENOTSOCK
		{ "ENOTSOCK",		ENOTSOCK	},
#endif
#ifdef	ENOTSUP
		{ "ENOTSUP",		ENOTSUP		},
#endif
#ifdef	ENOTTY
		{ "ENOTTY",		ENOTTY		},
#endif
#ifdef	ENXIO
		{ "ENXIO",		ENXIO		},
#endif
#ifdef	EOPNOTSUPP
		{ "EOPNOTSUPP",		EOPNOTSUPP	},
#endif
#ifdef	EOVERFLOW
		{ "EOVERFLOW",		EOVERFLOW	},
#endif
#ifdef	EPERM
		{ "EPERM",		EPERM		},
#endif
#ifdef	EPIPE
		{ "EPIPE",		EPIPE		},
#endif
#ifdef	EPROTO
		{ "EPROTO",		EPROTO		},
#endif
#ifdef	EPROTONOSUPPORT
		{ "EPROTONOSUPPORT",	EPROTONOSUPPORT	},
#endif
#ifdef	EPROTOTYPE
		{ "EPROTOTYPE",		EPROTOTYPE	},
#endif
#ifdef	ERANGE
		{ "ERANGE",		ERANGE		},
#endif
#ifdef	EROFS
		{ "EROFS",		EROFS		},
#endif
#ifdef	ESPIPE
		{ "ESPIPE",		ESPIPE		},
#endif
#ifdef	ESRCH
		{ "ESRCH",		ESRCH		},
#endif
#ifdef	ESTALE
		{ "ESTALE",		ESTALE		},
#endif
#ifdef	ETIME
		{ "ETIME",		ETIME		},
#endif
#ifdef	ETIMEDOUT
		{ "ETIMEDOUT",		ETIMEDOUT	},
#endif
#ifdef	ETXTBSY
		{ "ETXTBSY",		ETXTBSY		},
#endif
#ifdef	EWOULDBLOCK
		{ "EWOULDBLOCK",	EWOULDBLOCK	},
#endif
#ifdef	EXDEV
		{ "EXDEV",		EXDEV		},
#endif
	};

	static const luaL_Reg errnolib[] = {
		{ NULL, NULL}
	};
	luaL_register_module(L, "errno", errnolib);
	for (int i = 0; i < (int)lengthof(elist); i++) {
		lua_pushstring(L, elist[i].name);
		lua_pushinteger(L, elist[i].value);
		lua_rawset(L, -3);
	}
	lua_pop(L, -1);

}
