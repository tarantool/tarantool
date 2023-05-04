#ifndef INCLUDES_TARANTOOL_LUA_H
#define INCLUDES_TARANTOOL_LUA_H
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
#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct luaL_Reg;
extern bool start_loop;

extern struct lua_State *tarantool_L;

#define O_INTERACTIVE 0x1
#define O_BYTECODE    0x2
#define O_DEBUGGING   0x4

/**
 * Create tarantool_L and initialize built-in Lua modules.
 */
void
tarantool_lua_init(const char *tarantool_bin, const char *script, int argc,
		   char **argv);

/**
 * A code that runs after loading of all built-in modules.
 */
int
tarantool_lua_postinit(struct lua_State *L);

/** Free Lua subsystem resources. */
void
tarantool_lua_free();

/**
 * Load and execute start-up file
 *
 * @param path path to the script to be run
 * @param opt_mask mask for forcing of an interactive or debugging mode
 * @param optc the number of lua interpreter command line arguments
 * @param optv separate list of arguments for lua interpreter
 * @param argc argc the number of command line arguments, beyond those in optc
 * @param argv argv command line arguments, beyond those in optv
 *
 * @retval 0 The script is successfully finished.
 * @retval -1 Error during the script execution. Diagnostics area
 *        error is set.
 */
int
tarantool_lua_run_script(char *path, const char *instance_name,
			 const char* config_path, uint32_t opt_mask,
			 int optc, const char **optv,
			 int argc, char **argv);

extern char *history;

struct slab_cache *
tarantool_lua_slab_cache();

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LUA_H */
