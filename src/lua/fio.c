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
#include "lua/fio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>
#include <errno.h>
#include "coeio.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua/utils.h"
#include "coeio_file.h"

static int
lbox_fio_open(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.open(path, flags, mode)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	int flags = lua_tointeger(L, 2);
	int mode = lua_tointeger(L, 3);

	int fh = coeio_open(pathname, flags, mode);
	lua_pushinteger(L, fh);
	return 1;
}

static int
lbox_fio_pwrite(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	const char *buf = lua_tostring(L, 2);
	if (buf == NULL)
		luaL_error(L, "fio.pwrite(): buffer is not a string");
	size_t len = lua_tonumber(L, 3);
	size_t offset = lua_tonumber(L, 4);

	int res = coeio_pwrite(fh, buf, len, offset);
	lua_pushinteger(L, res);
	return 1;
}

static int
lbox_fio_pread(struct lua_State *L)
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


	int res = coeio_pread(fh, buf, len, offset);


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
lbox_fio_rename(struct lua_State *L)
{
	const char *oldpath;
	const char *newpath;
	if (lua_gettop(L) < 2) {
usage:
		luaL_error(L, "Usage: fio.rename(oldpath, newpath)");
	}
	oldpath = lua_tostring(L, 1);
	newpath = lua_tostring(L, 2);

	if (oldpath == NULL || newpath == NULL)
		goto usage;

	int res = coeio_rename(oldpath, newpath);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
lbox_fio_unlink(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.unlink(pathname)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	int res = coeio_unlink(pathname);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
lbox_fio_ftruncate(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	off_t length = lua_tonumber(L, 2);
	int res = coeio_ftruncate(fd, length);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
lbox_fio_truncate(struct lua_State *L)
{
	const char *pathname;
	int top = lua_gettop(L);
	if (top < 1) {
usage:
		luaL_error(L, "Usage: fio.truncate(pathname[, newlen])");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	off_t length;
	if (top >= 2)
		length = lua_tonumber(L, 2);
	else
		length = 0;
	int res = coeio_truncate(pathname, length);

	lua_pushboolean(L, res == 0);
	return 1;
}

static int
lbox_fio_write(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	const char *buf = lua_tostring(L, 2);
	if (buf == NULL)
		luaL_error(L, "fio.write(): buffer is not a string");

	size_t len = lua_tonumber(L, 3);
	int res = coeio_write(fh, buf, len);
	lua_pushinteger(L, res);
	return 1;
}

static int
lbox_fio_chown(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 3) {
usage:
		luaL_error(L, "Usage: fio.chown(pathname, owner, group)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;

	uid_t owner;
	if (lua_isnumber(L, 2)) {
		owner = lua_tointeger(L, 2);
	} else {
		const char *username = lua_tostring(L, 2);
		if (username == NULL)
			username = "";
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
		if (groupname == NULL)
			groupname = "";
		struct group *entry = getgrnam(groupname);
		if (!entry) {
			errno = EINVAL;
			lua_pushnil(L);
			return 1;
		}
		group = entry->gr_gid;
	}
	int res = coeio_chown(pathname, owner, group);
	lua_pushboolean(L, res == 0);
	return 1;
}

static int
lbox_fio_chmod(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 2) {
usage:
		luaL_error(L, "Usage: fio.chmod(pathname, mode)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;

	mode_t mode = lua_tointeger(L, 2);
	lua_pushboolean(L, coeio_chmod(pathname, mode) == 0);
	return 1;
}

static int
lbox_fio_read(struct lua_State *L)
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


	int res = coeio_read(fh, buf, len);

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
lbox_fio_lseek(struct lua_State *L)
{
	int fh = lua_tointeger(L, 1);
	off_t offset = lua_tonumber(L, 2);
	int whence = lua_tointeger(L, 3);
	off_t res = coeio_lseek(fh, offset, whence);
	lua_pushnumber(L, res);
	return 1;
}

#if defined(__APPLE__)
static int
lbox_fio_pushtimespec(struct lua_State *L, const time_t *ts)
{
	lua_pushnumber(L, *ts);
	return 1;
}
#else
static int
lbox_fio_pushtimespec(struct lua_State *L, const struct timespec *ts)
{
	double nsec = ts->tv_nsec;
	nsec /= 1000000000;
	lua_pushnumber(L, ts->tv_sec + nsec);
	return 1;
}
#endif

#define PUSHTABLE(name, method, value)	{	\
	lua_pushliteral(L, name);		\
	method(L, value);			\
	lua_settable(L, -3);			\
}

#define DEF_STAT_METHOD(method_name, macro_name)		\
	static int						\
	lbox_fio_stat_##method_name(struct lua_State *L)		\
	{							\
		if (lua_gettop(L) < 1 || !lua_istable(L, 1))	\
			luaL_error(L, "usage: stat:" #method_name "()"); \
		lua_pushliteral(L, "mode");			\
		lua_gettable(L, 1);				\
		int mode = lua_tointeger(L, -1);		\
		lua_pop(L, 1);					\
		lua_pushboolean(L, macro_name(mode) ? 1 : 0);	\
		return 1;					\
	}

DEF_STAT_METHOD(is_reg, S_ISREG);
DEF_STAT_METHOD(is_dir, S_ISDIR);
DEF_STAT_METHOD(is_chr, S_ISCHR);
DEF_STAT_METHOD(is_blk, S_ISBLK);
DEF_STAT_METHOD(is_fifo, S_ISFIFO);
#ifdef S_ISLNK
DEF_STAT_METHOD(is_link, S_ISLNK);
#endif
#ifdef S_ISSOCK
DEF_STAT_METHOD(is_sock, S_ISSOCK);
#endif

static int
lbox_fio_pushstat(struct lua_State *L, const struct stat *stat)
{
	lua_newtable(L);

	PUSHTABLE("dev", lua_pushinteger, stat->st_dev);
	PUSHTABLE("inode", lua_pushinteger, stat->st_ino);
	PUSHTABLE("mode", lua_pushinteger, stat->st_mode);
	PUSHTABLE("nlink", lua_pushinteger, stat->st_nlink);
	PUSHTABLE("uid", lua_pushinteger, stat->st_uid);
	PUSHTABLE("gid", lua_pushinteger, stat->st_gid);
	PUSHTABLE("rdev", lua_pushinteger, stat->st_rdev);
	PUSHTABLE("size", lua_pushinteger, stat->st_size);
	PUSHTABLE("blksize", lua_pushinteger, stat->st_blksize);
	PUSHTABLE("blocks", lua_pushinteger, stat->st_blocks);
	#if defined(__APPLE__)
		PUSHTABLE("ctime", lbox_fio_pushtimespec, &stat->st_ctime);
		PUSHTABLE("mtime", lbox_fio_pushtimespec, &stat->st_mtime);
		PUSHTABLE("atime", lbox_fio_pushtimespec, &stat->st_atime);
	#else
		PUSHTABLE("ctime", lbox_fio_pushtimespec, &stat->st_ctim);
		PUSHTABLE("mtime", lbox_fio_pushtimespec, &stat->st_mtim);
		PUSHTABLE("atime", lbox_fio_pushtimespec, &stat->st_atim);
	#endif


	int top = lua_gettop(L);
	/* metatable for tables *stat */
	lua_newtable(L);

	lua_pushliteral(L, "__index");
	lua_newtable(L);
	static const struct luaL_Reg stat_methods[] = {
		{ "is_reg", lbox_fio_stat_is_reg },
		{ "is_dir", lbox_fio_stat_is_dir },
		{ "is_chr", lbox_fio_stat_is_chr },
		{ "is_blk", lbox_fio_stat_is_blk },
		{ "is_fifo", lbox_fio_stat_is_fifo },
#ifdef S_ISLNK
		{ "is_link", lbox_fio_stat_is_link },
#endif
#ifdef S_ISSOCK
		{ "is_sock", lbox_fio_stat_is_sock },
#endif
		{ NULL,			NULL				}
	};
	luaL_register(L, NULL, stat_methods);
	lua_settable(L, -3);

	lua_setmetatable(L, top);
	lua_settop(L, top);

	return 1;
}

static int
lbox_fio_lstat(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "pathname is absent");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	struct stat stat;

	int res = coeio_lstat(pathname, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lbox_fio_pushstat(L, &stat);
}

static int
lbox_fio_stat(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.stat(pathname)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	struct stat stat;

	int res = coeio_stat(pathname, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lbox_fio_pushstat(L, &stat);
}

static int
lbox_fio_fstat(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	struct stat stat;
	int res = coeio_fstat(fd, &stat);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	return lbox_fio_pushstat(L, &stat);
}


static int
lbox_fio_mkdir(struct lua_State *L)
{
	const char *pathname;
	int top = lua_gettop(L);

	if (top < 1) {
usage:
		luaL_error(L, "Usage fio.mkdir(pathname[, mode])");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;

	mode_t mode;

	if (top >= 2)
		mode = lua_tointeger(L, 2);
	else
		mode = 0;
	lua_pushboolean(L, coeio_mkdir(pathname, mode) == 0);
	return 1;
}

static int
lbox_fio_rmdir(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.rmdir(pathname)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;

	lua_pushboolean(L, coeio_rmdir(pathname) == 0);
	return 1;
}

static int
lbox_fio_glob(struct lua_State *L)
{
	const char *pattern;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.glob(pattern)");
	}

	pattern = lua_tostring(L, 1);
	if (pattern == NULL)
		goto usage;

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
lbox_fio_link(struct lua_State *L)
{
	const char *target;
	const char *linkpath;
	if (lua_gettop(L) < 2) {
usage:
		luaL_error(L, "Usage: fio.link(target, linkpath)");
	}
	target = lua_tostring(L, 1);
	linkpath = lua_tostring(L, 2);
	if (target == NULL || linkpath == NULL)
		goto usage;
	lua_pushboolean(L, coeio_link(target, linkpath) == 0);
	return 1;
}

static int
lbox_fio_symlink(struct lua_State *L)
{
	const char *target;
	const char *linkpath;
	if (lua_gettop(L) < 2) {
usage:
		luaL_error(L, "Usage: fio.symlink(target, linkpath)");
	}
	target = lua_tostring(L, 1);
	linkpath = lua_tostring(L, 2);
	if (target == NULL || linkpath == NULL)
		goto usage;
	lua_pushboolean(L, coeio_symlink(target, linkpath) == 0);
	return 1;
}

static int
lbox_fio_readlink(struct lua_State *L)
{
	const char *pathname;
	if (lua_gettop(L) < 1) {
usage:
		luaL_error(L, "Usage: fio.readlink(pathname)");
	}
	pathname = lua_tostring(L, 1);
	if (pathname == NULL)
		goto usage;
	char *path = (char *)lua_newuserdata(L, PATH_MAX);
	int res = coeio_readlink(pathname, path, PATH_MAX);
	if (res < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, path, res);
	lua_remove(L, -2);
	return 1;
}

static int
lbox_fio_tempdir(struct lua_State *L)
{
	char *buf = (char *)lua_newuserdata(L, PATH_MAX);
	if (!buf) {
		errno = ENOMEM;
		lua_pushnil(L);
		return 1;
	}


	if (coeio_tempdir(buf, PATH_MAX) == 0) {
		lua_pushstring(L, buf);
		lua_remove(L, -2);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int
lbox_fio_cwd(struct lua_State *L)
{
	char *buf = (char *)lua_newuserdata(L, PATH_MAX);
	if (!buf) {
		errno = ENOMEM;
		lua_pushnil(L);
		return 1;
	}


	if (getcwd(buf, PATH_MAX)) {
		lua_pushstring(L, buf);
		lua_remove(L, -2);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int
lbox_fio_fsync(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, coeio_fsync(fd) == 0);
	return 1;
}

static int
lbox_fio_fdatasync(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, coeio_fdatasync(fd) == 0);
	return 1;
}

static int
lbox_fio_sync(struct lua_State *L)
{
	lua_pushboolean(L, coeio_sync() == 0);
	return 1;
}

static int
lbox_fio_close(struct lua_State *L)
{
	int fd = lua_tointeger(L, 1);
	lua_pushboolean(L, coeio_close(fd) == 0);
	return 1;
}




void
tarantool_lua_fio_init(struct lua_State *L)
{
	static const struct luaL_Reg fio_methods[] = {
		{ "lstat",		lbox_fio_lstat			},
		{ "stat",		lbox_fio_stat			},
		{ "mkdir",		lbox_fio_mkdir			},
		{ "rmdir",		lbox_fio_rmdir			},
		{ "glob",		lbox_fio_glob			},
		{ "link",		lbox_fio_link			},
		{ "symlink",		lbox_fio_symlink		},
		{ "readlink",		lbox_fio_readlink		},
		{ "unlink",		lbox_fio_unlink			},
		{ "rename",		lbox_fio_rename			},
		{ "chown",		lbox_fio_chown			},
		{ "chmod",		lbox_fio_chmod			},
		{ "truncate",		lbox_fio_truncate		},
		{ "tempdir",		lbox_fio_tempdir		},
		{ "cwd",		lbox_fio_cwd			},
		{ "sync",		lbox_fio_sync			},
		{ NULL,			NULL				}
	};

	luaL_register_module(L, "fio", fio_methods);

	/* internal table */
	lua_pushliteral(L, "internal");
	lua_newtable(L);
	static const struct luaL_Reg internal_methods[] = {
		{ "open",		lbox_fio_open			},
		{ "close",		lbox_fio_close			},
		{ "pwrite",		lbox_fio_pwrite			},
		{ "pread",		lbox_fio_pread			},
		{ "read",		lbox_fio_read			},
		{ "write",		lbox_fio_write			},
		{ "lseek",		lbox_fio_lseek			},
		{ "ftruncate",		lbox_fio_ftruncate		},
		{ "fsync",		lbox_fio_fsync			},
		{ "fdatasync",		lbox_fio_fdatasync		},
		{ "fstat",		lbox_fio_fstat			},
		{ NULL,			NULL				}
	};
	luaL_register(L, NULL, internal_methods);
	lua_settable(L, -3);


	lua_pushliteral(L, "c");
	lua_newtable(L);


	lua_pushliteral(L, "flag");
	lua_newtable(L);
#ifdef O_APPEND
	PUSHTABLE("O_APPEND", lua_pushinteger, O_APPEND);
#endif
#ifdef O_ASYNC
	PUSHTABLE("O_ASYNC", lua_pushinteger, O_ASYNC);
#endif
#ifdef O_CLOEXEC
	PUSHTABLE("O_CLOEXEC", lua_pushinteger, O_CLOEXEC);
#endif
#ifdef O_CREAT
	PUSHTABLE("O_CREAT", lua_pushinteger, O_CREAT);
#endif
#ifdef O_DIRECT
	PUSHTABLE("O_DIRECT", lua_pushinteger, O_DIRECT);
#endif
#ifdef O_DIRECTORY
	PUSHTABLE("O_DIRECTORY", lua_pushinteger, O_DIRECTORY);
#endif
#ifdef O_EXCL
	PUSHTABLE("O_EXCL", lua_pushinteger, O_EXCL);
#endif
#ifdef O_LARGEFILE
	PUSHTABLE("O_LARGEFILE", lua_pushinteger, O_LARGEFILE);
#endif
#ifdef O_NOATIME
	PUSHTABLE("O_NOATIME", lua_pushinteger, O_NOATIME);
#endif
#ifdef O_NOCTTY
	PUSHTABLE("O_NOCTTY", lua_pushinteger, O_NOCTTY);
#endif
#ifdef O_NOFOLLOW
	PUSHTABLE("O_NOFOLLOW", lua_pushinteger, O_NOFOLLOW);
#endif
#ifdef O_NONBLOCK
	PUSHTABLE("O_NONBLOCK", lua_pushinteger, O_NONBLOCK);
#endif
#ifdef O_NDELAY
	PUSHTABLE("O_NDELAY", lua_pushinteger, O_NDELAY);
#endif
#ifdef O_PATH
	PUSHTABLE("O_PATH", lua_pushinteger, O_PATH);
#endif
#ifdef O_SYNC
	PUSHTABLE("O_SYNC", lua_pushinteger, O_SYNC);
#endif
#ifdef O_TMPFILE
	PUSHTABLE("O_TMPFILE", lua_pushinteger, O_TMPFILE);
#endif
#ifdef O_TRUNC
	PUSHTABLE("O_TRUNC", lua_pushinteger, O_TRUNC);
#endif
	PUSHTABLE("O_RDONLY", lua_pushinteger, O_RDONLY);
	PUSHTABLE("O_WRONLY", lua_pushinteger, O_WRONLY);
	PUSHTABLE("O_RDWR", lua_pushinteger, O_RDWR);
	lua_settable(L, -3);

	lua_pushliteral(L, "mode");
	lua_newtable(L);
	PUSHTABLE("S_IRWXU", lua_pushinteger, S_IRWXU);
	PUSHTABLE("S_IRUSR", lua_pushinteger, S_IRUSR);
	PUSHTABLE("S_IWUSR", lua_pushinteger, S_IWUSR);
	PUSHTABLE("S_IXUSR", lua_pushinteger, S_IXUSR);
	PUSHTABLE("S_IRWXG", lua_pushinteger, S_IRWXG);
	PUSHTABLE("S_IRGRP", lua_pushinteger, S_IRGRP);
	PUSHTABLE("S_IWGRP", lua_pushinteger, S_IWGRP);
	PUSHTABLE("S_IXGRP", lua_pushinteger, S_IXGRP);
	PUSHTABLE("S_IRWXO", lua_pushinteger, S_IRWXO);
	PUSHTABLE("S_IROTH", lua_pushinteger, S_IROTH);
	PUSHTABLE("S_IWOTH", lua_pushinteger, S_IWOTH);
	PUSHTABLE("S_IXOTH", lua_pushinteger, S_IXOTH);
	lua_settable(L, -3);



	lua_pushliteral(L, "seek");
	lua_newtable(L);
	PUSHTABLE("SEEK_SET", lua_pushinteger, SEEK_SET);
	PUSHTABLE("SEEK_CUR", lua_pushinteger, SEEK_CUR);
	PUSHTABLE("SEEK_END", lua_pushinteger, SEEK_END);
#ifdef SEEK_DATA
	PUSHTABLE("SEEK_DATA", lua_pushinteger, SEEK_DATA);
#endif
#ifdef SEEK_HOLE
	PUSHTABLE("SEEK_HOLE", lua_pushinteger, SEEK_HOLE);
#endif
	lua_settable(L, -3);


	lua_settable(L, -3);
	lua_pop(L, 1);
}
