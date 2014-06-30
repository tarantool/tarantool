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
#include "box/lua/index.h"
#include "lua/utils.h"
#include "box/index.h"
#include "box/space.h"
#include "box/schema.h"
#include "box/access.h"
#include "box/lua/tuple.h"
#include "fiber.h"
#include "tbuf.h"

/** {{{ box.index Lua library: access to spaces and indexes
 */

static inline Index *
check_index(uint32_t space_id, uint32_t index_id)
{
	struct space *space = space_cache_find(space_id);
	space_check_access(space, PRIV_R);
	return index_find(space, index_id);
}

size_t
boxffi_index_len(uint32_t space_id, uint32_t index_id)
{
	try {
		return check_index(space_id, index_id)->size();
	} catch (Exception *) {
		return (size_t) -1; /* handled by box.raise() in Lua */
	}
}

struct tuple *
boxffi_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd)
{
	try {
		return check_index(space_id, index_id)->random(rnd);
	}  catch (Exception *) {
		return (struct tuple *) -1; /* handled by box.raise() in Lua */
	}
}

static void
box_index_init_iterator_types(struct lua_State *L, int idx)
{
	for (int i = 0; i < iterator_type_MAX; i++) {
		assert(strncmp(iterator_type_strs[i], "ITER_", 5) == 0);
		lua_pushnumber(L, i);
		/* cut ITER_ prefix from enum name */
		lua_setfield(L, idx, iterator_type_strs[i] + 5);
	}
}

/* }}} */

/* {{{ box.index.iterator Lua library: index iterators */

struct iterator *
boxffi_index_iterator(uint32_t space_id, uint32_t index_id, int type,
		      const char *key)
{
	struct iterator *it = NULL;
	enum iterator_type itype = (enum iterator_type) type;
	try {
		Index *index = check_index(space_id, index_id);
		assert(mp_typeof(*key) == MP_ARRAY); /* checked by Lua */
		uint32_t part_count = mp_decode_array(&key);
		key_validate(index->key_def, itype, key, part_count);
		it = index->allocIterator();
		index->initIterator(it, itype, key, part_count);
		return it;
	} catch (Exception *) {
		if (it)
			it->free(it);
		/* will be hanled by box.raise() in Lua */
		return NULL;
	}
}

/* }}} */

void
box_lua_index_init(struct lua_State *L)
{
	static const struct luaL_reg indexlib [] = {
		{NULL, NULL}
	};

	/* box.index */
	luaL_register(L, "box.index", indexlib);
	box_index_init_iterator_types(L, -2);
	lua_pop(L, 1);
}
