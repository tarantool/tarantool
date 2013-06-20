#ifndef TARANTOOL_PLUGIN_H_INCLUDED
#define TARANTOOL_PLUGIN_H_INCLUDED
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

#define PLUGIN_API_VERSION		1

#include <rlist.h>
#include <tbuf.h>
#include <stddef.h>

struct lua_State;

typedef void(*plugin_init_cb)(struct lua_State *L);
typedef void(*plugin_stat_cb)(struct tbuf *out);

struct tarantool_plugin {
        int api_version;
        int version;
        const char *name;
        plugin_init_cb init;
        plugin_stat_cb stat;
        struct rlist list;
};

#define DECLARE_PLUGIN(name, version, init, stat)	        \
	extern "C" {						\
		struct tarantool_plugin plugin_meta = {		\
			PLUGIN_API_VERSION,			\
			version,				\
			name,					\
			init,                                 \
			stat,                                 \
			{ NULL, NULL }				\
		};						\
	}


#endif /* TARANTOOL_PLUGIN_H_INCLUDED */
