/*
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "datetime.h"

#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

uint32_t CTID_DATETIME_TZ = 0;
uint32_t CTID_DURATION = 0;

void
tarantool_lua_datetime_init(struct lua_State *L)
{
	int rc = luaL_cdef(L, "struct t_datetime_tz {"
				"int secs;"
				"int nsec;"
				"int offset;"
			  "};");
	assert(rc == 0);
	(void) rc;
	CTID_DATETIME_TZ = luaL_ctypeid(L, "struct t_datetime_tz");
	assert(CTID_DATETIME_TZ != 0);


	rc = luaL_cdef(L, "struct t_datetime_duration {"
				"int secs;"
				"int nsec;"
			  "};");
	assert(rc == 0);
	(void) rc;
	CTID_DURATION = luaL_ctypeid(L, "struct t_datetime_duration");
	assert(CTID_DURATION != 0);
}
