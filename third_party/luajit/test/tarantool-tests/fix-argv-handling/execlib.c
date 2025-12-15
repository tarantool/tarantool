#define _GNU_SOURCE
#include "lua.h"
#include "lauxlib.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/* 1Kb should be enough. */
#define BUF_SIZE 1024
#define CHECKED(call) \
do { \
	if ((call) == -1) { \
		perror(#call); \
		exit(1); \
	} \
} while(0)

static int empty_argv_exec(struct lua_State *L)
{
	const char *path = luaL_checkstring(L, -1);
	int pipefds[2] = {};
	char *const argv[] = {NULL};
	char buf[BUF_SIZE];

	CHECKED(pipe2(pipefds, O_CLOEXEC));

	pid_t pid = fork();
	CHECKED(pid);

	if (pid == 0) {
		/*
		 * Mock the `luaL_newstate` with an error-injected
		 * version.
		 */
		setenv("LD_PRELOAD", "mynewstate.so", 1);
		CHECKED(dup2(pipefds[1], STDOUT_FILENO));
		CHECKED(dup2(pipefds[1], STDERR_FILENO));
		/*
		 * Pipes are closed on the exec call because of
		 * the O_CLOEXEC flag.
		 */
		CHECKED(execvp(path, argv));
	}

	close(pipefds[1]);
	CHECKED(waitpid(pid, NULL, 0));

	CHECKED(read(pipefds[0], buf, BUF_SIZE));
	close(pipefds[0]);

	lua_pushstring(L, buf);
	return 1;
}

static const struct luaL_Reg execlib[] = {
	{"empty_argv_exec", empty_argv_exec},
	{NULL, NULL}
};

LUA_API int luaopen_execlib(lua_State *L)
{
	luaL_register(L, "execlib", execlib);
	return 1;
}
