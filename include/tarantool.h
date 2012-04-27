#ifndef TARANTOOL_H_INCLUDED
#define TARANTOOL_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <tbuf.h>
#include <util.h>
#include <log_io.h>

struct lua_State;
struct luaL_Reg;

void mod_init(void);
void mod_free(void);
struct tarantool_cfg;

extern const char *mod_name;
i32 mod_check_config(struct tarantool_cfg *conf);
i32 mod_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf);
int mod_cat(const char *filename);
void mod_snapshot(struct log_io_iter *);
void mod_info(struct tbuf *out);
/**
 * This is a callback used by tarantool_lua_init() to open
 * module-specific libraries into given Lua state.
 *
 * @return  L on success, 0 if out of memory
 */
struct lua_State *mod_lua_init(struct lua_State *L);

void
tarantool_lua_register_type(struct lua_State *L, const char *type_name,
			    const struct luaL_Reg *methods);

/**
 * Create an instance of Lua interpreter and load it with
 * Tarantool modules.  Creates a Lua state, imports global
 * Tarantool modules, then calls mod_lua_init(), which performs
 * module-specific imports. The created state can be freed as any
 * other, with lua_close().
 *
 * @return  L on success, 0 if out of memory
 */
struct lua_State *tarantool_lua_init();
void tarantool_lua_close(struct lua_State *L);

/*
 * Single global lua_State shared by core and modules.
 * Created with tarantool_lua_init().
 */
extern struct lua_State *tarantool_L;
/* Call Lua 'tostring' built-in to print userdata nicely. */
const char *
tarantool_lua_tostring(struct lua_State *L, int index);

/* Convert Lua string, number or cdata (u64) to 64bit value */
uint64_t
tarantool_lua_tointeger64(struct lua_State *L, int idx);

/* Make a new configuration available in Lua */
void tarantool_lua_load_cfg(struct lua_State *L,
			    struct tarantool_cfg *cfg);

/**
 * Load and execute start-up file
 *
 * @param L is Lua State
 */
void tarantool_lua_load_init_script(struct lua_State *L);

extern struct tarantool_cfg cfg;
extern const char *cfg_filename;
extern char *cfg_filename_fullpath;
extern bool init_storage, booting;
extern char *binary_filename;
extern char *custom_proc_title;
i32 reload_cfg(struct tbuf *out);
int snapshot(void * /* ev */, int /* events */);
const char *tarantool_version(void);
double tarantool_uptime(void);
void tarantool_free(void);

char **init_set_proc_title(int argc, char **argv);
void free_proc_title(int argc, char **argv);
void set_proc_title(const char *format, ...);

void
tarantool_lua(struct lua_State *L,
	      struct tbuf *out, const char *str);

#endif /* TARANTOOL_H_INCLUDED */
