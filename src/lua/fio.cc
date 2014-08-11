#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
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


static int
fio_eio_rename(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		luaL_error(L, "Usage: fio.rename(oldpath, newpath)");
	const char *oldpath = lua_tostring(L, 1);
	const char *newpath = lua_tostring(L, 2);
	int res = dfio_rename(oldpath, newpath);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
fio_eio_unlink(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: fio.unlink(pathname)");
	const char *pathname = lua_tostring(L, 1);
	if (!pathname) {
		errno = EINVAL;
		lua_pushboolean(L, 0);
		return 1;
	}
	int res = dfio_unlink(pathname);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
fio_eio_ftruncate(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	off_t length = lua_tonumber(L, 2);
	int res = dfio_ftruncate(fd, length);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
fio_eio_truncate(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1)
		luaL_error(L, "Usage: fio.truncate(pathname[, newlen])");
	const char *path = lua_tostring(L, 1);
	off_t length;
	if (top >= 2)
		length = lua_tonumber(L, 2);
	else
		length = 0;
	int res = dfio_truncate(path, length);

	lua_pushboolean(L, res == 0);
	return 1;
}

static int
fio_eio_write(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	const char *buf = lua_tostring(L, 2);
	size_t len = lua_tonumber(L, 3);
	int res = dfio_write(fh, buf, len);
	lua_pushinteger(L, res);
	return 1;
}

static int
fio_eio_chown(struct lua_State *L)
{
	if (lua_gettop(L) < 3)
		luaL_error(L, "Usage: fio.chown(pathname, owner, group)");
	const char *path = lua_tostring(L, 1);
	uid_t owner;
	if (lua_isnumber(L, 2)) {
		owner = lua_tointeger(L, 2);
	} else {
		const char *username = lua_tostring(L, 2);
		struct passwd *entry = getpwnam(username);
		if (!entry) {
			errno = EINVAL;
			lua_pushnil(L);
			return 1;
		}
		owner = entry->pw_uid;
	}
	gid_t group;

	if (lua_isnumber(L, 3)) {
		group = lua_tointeger(L, 3);
	} else {
		const char *groupname = lua_tostring(L, 3);
		struct group *entry = getgrnam(groupname);
		if (!entry) {
			errno = EINVAL;
			lua_pushnil(L);
			return 1;
		}
		group = entry->gr_gid;
	}
	int res = dfio_chown(path, owner, group);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
fio_eio_chmod(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		luaL_error(L, "Usage: fio.chmod(pathname, mode)");
	const char *path = lua_tostring(L, 1);
	mode_t mode = lua_tointeger(L, 2);
	lua_pushboolean(L, dfio_chmod(path, mode) == 0);
	return 1;
}

static int
fio_eio_read(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	size_t len = lua_tonumber(L, 2);

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


	int res = dfio_read(fh, buf, len);

	if (res < 0) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, (char *)buf, res);
	lua_remove(L, -2);
	return 1;
}


static int
fio_eio_lseek(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	off_t offset = lua_tonumber(L, 2);
	int whence = lua_tointeger(L, 3);
	off_t res = dfio_lseek(fh, offset, whence);
	lua_pushnumber(L, res);
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
fio_eio_lstat(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "pathname is absent");
	const char *pathname = lua_tostring(L, 1);
	struct stat stat;

	int res = dfio_lstat(pathname, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lua_pushstat(L, &stat);
}

static int
fio_eio_stat(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "pathname is absent");
	const char *pathname = lua_tostring(L, 1);
	struct stat stat;

	int res = dfio_stat(pathname, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lua_pushstat(L, &stat);
}

static int
fio_eio_fstat(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	struct stat stat;
	int res = dfio_fstat(fd, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lua_pushstat(L, &stat);
}


static int
fio_eio_mkdir(struct lua_State *L)
{
	int top = lua_gettop(L);
	if (top < 1)
		luaL_error(L, "usage fio.mkdir(pathname[, mode])");

	const char *pathname = lua_tostring(L, 1);

	mode_t mode;

	if (top >= 2)
		mode = lua_tointeger(L, 2);
	else
		mode = 0;
	lua_pushboolean(L, dfio_mkdir(pathname, mode) == 0);
	return 1;
}

static int
fio_eio_rmdir(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "usage: fio.rmdir(pathname)");

	const char *pathname = lua_tostring(L, 1);
	lua_pushboolean(L, dfio_rmdir(pathname) == 0);
	return 1;
}

static int
fio_eio_glob(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: fio.glob(pattern)");

	const char *pattern = lua_tostring(L, 1);

	glob_t globbuf;
	switch (glob(pattern, GLOB_NOESCAPE, NULL, &globbuf)) {
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
fio_eio_link(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		luaL_error(L, "Usage: fio.link(target, linkpath)");
	const char *target = lua_tostring(L, 1);
	const char *linkpath = lua_tostring(L, 2);
	lua_pushboolean(L, dfio_link(target, linkpath) == 0);
	return 1;
}

static int
fio_eio_symlink(struct lua_State *L)
{
	if (lua_gettop(L) < 2)
		luaL_error(L, "Usage: fio.symlink(target, linkpath)");
	const char *target = lua_tostring(L, 1);
	const char *linkpath = lua_tostring(L, 2);
	lua_pushboolean(L, dfio_symlink(target, linkpath) == 0);
	return 1;
}

static int
fio_eio_readlink(struct lua_State *L)
{
	if (lua_gettop(L) < 1)
		luaL_error(L, "Usage: fio.readlink(pathname)");
	static __thread char path[PATH_MAX];
	const char *pathname = lua_tostring(L, 1);
	int res = dfio_readlink(pathname, path, sizeof(path));
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, path, res);
	return 1;
}


static int
fio_eio_tempdir(struct lua_State *L)
{
	const char *path = dfio_tempdir();
	if (path)
		lua_pushstring(L, path);
	else
		lua_pushnil(L);
	return 1;
}


static int
fio_eio_fsync(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, dfio_fsync(fd) == 0);
	return 1;
}

static int
fio_eio_fdatasync(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, dfio_fdatasync(fd) == 0);
	return 1;
}

static int
fio_eio_sync(struct lua_State *L)
{
	lua_pushboolean(L, dfio_sync() == 0);
	return 1;
}


static int
fio_eio_close(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, dfio_close(fd) == 0);
	return 1;
}

#define ADD_CONST(c)	{			\
		lua_pushliteral(L, # c);	\
		lua_pushinteger(L, c);		\
		lua_settable(L, -3);		\
	}





void
fio_lua_init(struct lua_State *L)
{
	static const struct luaL_Reg fio_methods[] = {
		{ "lstat",		fio_eio_lstat			},
		{ "stat",		fio_eio_stat			},
		{ "mkdir",		fio_eio_mkdir			},
		{ "rmdir",		fio_eio_rmdir			},
		{ "glob",		fio_eio_glob			},
		{ "link",		fio_eio_link			},
		{ "symlink",		fio_eio_symlink			},
		{ "readlink",		fio_eio_readlink		},
		{ "unlink",		fio_eio_unlink			},
		{ "rename",		fio_eio_rename			},
		{ "chown",		fio_eio_chown			},
		{ "chmod",		fio_eio_chmod			},
		{ "truncate",		fio_eio_truncate		},
		{ "tempdir",		fio_eio_tempdir			},
		{ "sync",		fio_eio_sync			},

		{ NULL,			NULL				}
	};

	luaL_register_module(L, "fio", fio_methods);



	/* internal table */
	lua_pushliteral(L, "internal");
	lua_newtable(L);
	static const struct luaL_Reg internal_methods[] = {
		{ "open",		fio_eio_open			},
		{ "close",		fio_eio_close			},
		{ "pwrite",		fio_eio_pwrite			},
		{ "pread",		fio_eio_pread			},
		{ "read",		fio_eio_read			},
		{ "write",		fio_eio_write			},
		{ "lseek",		fio_eio_lseek			},
		{ "ftruncate",		fio_eio_ftruncate		},
		{ "fsync",		fio_eio_fsync			},
		{ "fdatasync",		fio_eio_fdatasync		},

		{ "fstat",		fio_eio_fstat			},

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
