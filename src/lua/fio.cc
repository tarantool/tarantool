#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>
#include "lua/fio.h"
#include <coeio.h>
#include <fiber.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include "lua/utils.h"

#include <disk_fiber_io.h>



static int
fio_eio_open(struct lua_State *L)
{
	const char *path = lua_tostring(L, 1);
	int flags = lua_tointeger(L, 2);
	int mode = lua_tointeger(L, 3);

	int fh = dfio_open(path, flags, mode);
	lua_pushinteger(L, fh);
	return 1;
}


static int
fio_eio_pwrite(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	const char *buf = lua_tostring(L, 2);
	size_t len = lua_tonumber(L, 3);
	size_t offset = lua_tonumber(L, 4);

	int res = dfio_pwrite(fh, buf, len, offset);
	lua_pushinteger(L, res);
	return 1;
}


static int
fio_eio_pread(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	size_t len = lua_tonumber(L, 2);
	size_t offset = lua_tonumber(L, 3);

	if (!len) {
		lua_pushliteral(L, "");
		return 1;
	}

	/* allocate buffer at lua stack */
	void *buf = lua_newuserdata(L, len);
	if (!buf) {
		errno = ENOMEM;
		lua_pushnil(L);
		return 1;
	}


	int res = dfio_pread(fh, buf, len, offset);

	if (res < 0) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, (char *)buf, res);
	lua_remove(L, -2);
	return 1;
}



#define ADD_CONST(c)	{			\
		lua_pushliteral(L, # c);	\
		lua_pushinteger(L, c);		\
		lua_settable(L, -3);		\
	}


static int
fio_lua_glob(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: fio.glob('[pattern].*'");
	if (!lua_isstring(L, 1))
		luaL_error(L, "pattern must be string");
	const char *p = lua_tostring(L, 1);

	glob_t globbuf;
	switch (glob(p, GLOB_NOESCAPE, NULL, &globbuf)) {
		case 0:
			break;
		case GLOB_NOMATCH:
			lua_newtable(L);
			return 1;

		default:
		case GLOB_NOSPACE:
			errno = ENOMEM;
			lua_pushnil(L);
			return 1;
	}

	lua_newtable(L);

	for (size_t i = 0; i < globbuf.gl_pathc; i++) {
		lua_pushinteger(L, i + 1);
		lua_pushstring(L, globbuf.gl_pathv[i]);
		lua_settable(L, -3);
	}

	globfree(&globbuf);
	return 1;
}

static int
lua_pushtimespec(struct lua_State *L, const struct timespec *ts)
{
	double nsec = ts->tv_nsec;
	nsec /= 1000000000;
	lua_pushnumber(L, ts->tv_sec + nsec);
	return 1;
}

static int
lua_pushstat(struct lua_State *L, const struct stat *stat)
{
	lua_newtable(L);

	lua_pushliteral(L, "dev");
	lua_pushinteger(L, stat->st_dev);
	lua_settable(L, -3);

	lua_pushliteral(L, "inode");
	lua_pushinteger(L, stat->st_ino);
	lua_settable(L, -3);

	lua_pushliteral(L, "mode");
	lua_pushinteger(L, stat->st_mode);
	lua_settable(L, -3);

	lua_pushliteral(L, "nlink");
	lua_pushinteger(L, stat->st_nlink);
	lua_settable(L, -3);

	lua_pushliteral(L, "uid");
	lua_pushinteger(L, stat->st_uid);
	lua_settable(L, -3);

	lua_pushliteral(L, "gid");
	lua_pushinteger(L, stat->st_gid);
	lua_settable(L, -3);

	lua_pushliteral(L, "rdev");
	lua_pushinteger(L, stat->st_rdev);
	lua_settable(L, -3);

	lua_pushliteral(L, "size");
	lua_pushinteger(L, stat->st_size);
	lua_settable(L, -3);

	lua_pushliteral(L, "blksize");
	lua_pushinteger(L, stat->st_blksize);
	lua_settable(L, -3);

	lua_pushliteral(L, "blocks");
	lua_pushinteger(L, stat->st_blocks);
	lua_settable(L, -3);


	lua_pushliteral(L, "ctime");
	lua_pushtimespec(L, &stat->st_ctim);
	lua_settable(L, -3);

	lua_pushliteral(L, "mtime");
	lua_pushtimespec(L, &stat->st_mtim);
	lua_settable(L, -3);

	lua_pushliteral(L, "atime");
	lua_pushtimespec(L, &stat->st_atim);
	lua_settable(L, -3);

	return 1;
}

static int
fio_lua_lstat(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
		luaL_error(L, "Usage: fio.stat(pathname)");
	struct stat stat;
	if (lstat(lua_tostring(L, 1), &stat) < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lua_pushstat(L, &stat);
}


static int
fio_lua_fstat(struct lua_State *L)
{
	if (lua_gettop(L) < 1 || !lua_isnumber(L, 1))
		luaL_error(L, "Usage: fio.fstat(fd)");
	struct stat stat;
	if (fstat(lua_tointeger(L, 1), &stat) < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lua_pushstat(L, &stat);
}

void
fio_lua_init(struct lua_State *L)
{
	static const struct luaL_Reg fio_methods[] = {
		{ "stat",		fio_lua_lstat			},
		{ "fstat",		fio_lua_fstat			},
		{ "glob",		fio_lua_glob			},
		{ NULL,			NULL				}
	};

	luaL_register_module(L, "fio", fio_methods);



	/* internal table */
	lua_pushliteral(L, "internal");
	lua_newtable(L);
	static const struct luaL_Reg internal_methods[] = {
		{ "open",		fio_eio_open			},
		{ "pwrite",		fio_eio_pwrite			},
		{ "pread",		fio_eio_pread			},
		{ NULL,			NULL				}
	};
	luaL_register(L, NULL, internal_methods);
	lua_settable(L, -3);


	lua_pushliteral(L, "c");
	lua_newtable(L);


	lua_pushliteral(L, "flag");
	lua_newtable(L);
	#ifdef O_APPEND
		ADD_CONST(O_APPEND)
	#endif
	#ifdef O_ASYNC
		ADD_CONST(O_ASYNC)
	#endif
	#ifdef O_CLOEXEC
		ADD_CONST(O_CLOEXEC)
	#endif
	#ifdef O_CREAT
		ADD_CONST(O_CREAT)
	#endif
	#ifdef O_DIRECT
		ADD_CONST(O_DIRECT)
	#endif
	#ifdef O_DIRECTORY
		ADD_CONST(O_DIRECTORY)
	#endif
	#ifdef O_EXCL
		ADD_CONST(O_EXCL)
	#endif
	#ifdef O_LARGEFILE
		ADD_CONST(O_LARGEFILE)
	#endif
	#ifdef O_NOATIME
		ADD_CONST(O_NOATIME)
	#endif
	#ifdef O_NOCTTY
		ADD_CONST(O_NOCTTY)
	#endif
	#ifdef O_NOFOLLOW
		ADD_CONST(O_NOFOLLOW)
	#endif
	#ifdef O_NONBLOCK
		ADD_CONST(O_NONBLOCK)
	#endif
	#ifdef O_NDELAY
		ADD_CONST(O_NDELAY)
	#endif
	#ifdef O_PATH
		ADD_CONST(O_PATH)
	#endif
	#ifdef O_SYNC
		ADD_CONST(O_SYNC)
	#endif
	#ifdef O_TMPFILE
		ADD_CONST(O_TMPFILE)
	#endif
	#ifdef O_TRUNC
		ADD_CONST(O_TRUNC)
	#endif
	ADD_CONST(O_RDONLY);
	ADD_CONST(O_WRONLY);
	ADD_CONST(O_RDWR);
	lua_settable(L, -3);

	lua_pushliteral(L, "mode");
	lua_newtable(L);
	ADD_CONST(S_IRWXU);
	ADD_CONST(S_IRUSR);
	ADD_CONST(S_IWUSR);
	ADD_CONST(S_IXUSR);
	ADD_CONST(S_IRWXG);
	ADD_CONST(S_IRGRP);
	ADD_CONST(S_IWGRP);
	ADD_CONST(S_IXGRP);
	ADD_CONST(S_IRWXO);
	ADD_CONST(S_IROTH);
	ADD_CONST(S_IWOTH);
	ADD_CONST(S_IXOTH);
	lua_settable(L, -3);


	lua_pushliteral(L, "seek");
	lua_newtable(L);
	ADD_CONST(SEEK_SET);
	ADD_CONST(SEEK_CUR);
	ADD_CONST(SEEK_END);
	#ifdef SEEK_DATA
		ADD_CONST(SEEK_DATA);
	#endif
	#ifdef SEEK_HOLE
		ADD_CONST(SEEK_HOLE);
	#endif
	lua_settable(L, -3);


	lua_settable(L, -3);
	lua_pop(L, 1);
}
