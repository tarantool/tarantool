#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
#include "sio.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

struct cfg_uri_array;
struct lua_State;
struct cfg_uri_array_vtab;
TT_PLUGIN_REGISTER(cfg_uri_array);

struct cfg_uri_array_vtab {
	struct cfg_uri_array *
	(*cfg_uri_array_new)(struct lua_State *L, const char *option_name);
	void
	(*cfg_uri_array_delete)(struct cfg_uri_array *uri_array);
	int
	(*cfg_uri_array_size)(const struct cfg_uri_array *uri_array);
	const char *
	(*cfg_uri_array_get_uri)(const struct cfg_uri_array *uri_array,
				 int idx);
	int
	(*cfg_uri_array_check_uri)(const struct cfg_uri_array *uri_array,
				   int (*check_uri)(const char *, const char *),
				   const char *option_name);
};

API_EXPORT int
cfg_get_uri_array(struct lua_State *L, const char *param);

/** \endcond public */

extern struct cfg_uri_array_vtab *cfg_uri_array_vtab_ptr;

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
