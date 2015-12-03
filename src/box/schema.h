#ifndef INCLUDES_TARANTOOL_BOX_SCHEMA_H
#define INCLUDES_TARANTOOL_BOX_SCHEMA_H
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

/** \cond public */
enum {
	/** Start of the reserved range of system spaces. */
	BOX_SYSTEM_ID_MIN = 256,
	/** Space id of _schema. */
	BOX_SCHEMA_ID = 272,
	/** Space id of _space. */
	BOX_SPACE_ID = 280,
	/** Space id of _vspace view. */
	BOX_VSPACE_ID = 281,
	/** Space id of _index. */
	BOX_INDEX_ID = 288,
	/** Space id of _vindex view. */
	BOX_VINDEX_ID = 289,
	/** Space id of _func. */
	BOX_FUNC_ID = 296,
	/** Space id of _vfunc view. */
	BOX_VFUNC_ID = 297,
	/** Space id of _user. */
	BOX_USER_ID = 304,
	/** Space id of _vuser view. */
	BOX_VUSER_ID = 305,
	/** Space id of _priv. */
	BOX_PRIV_ID = 312,
	/** Space id of _vpriv view. */
	BOX_VPRIV_ID = 313,
	/** Space id of _cluster. */
	BOX_CLUSTER_ID = 320,
	/** End of the reserved range of system spaces. */
	BOX_SYSTEM_ID_MAX = 511,
	BOX_ID_NIL = 2147483647
};
/** \endcond public */

#if defined(__cplusplus)

#include "error.h"
#include <stdio.h> /* snprintf */

extern int sc_version;

struct space;

/** Call a visitor function on every space in the space cache. */
void
space_foreach(void (*func)(struct space *sp, void *udata), void *udata);

/**
 * Try to look up a space by space number in the space cache.
 * FFI-friendly no-exception-thrown space lookup function.
 *
 * @return NULL if space not found, otherwise space object.
 */
extern "C" struct space *
space_by_id(uint32_t id);

/** No-throw conversion of space id to space name */
extern "C" const char *
space_name_by_id(uint32_t id);

static inline struct space *
space_cache_find(uint32_t id)
{
	struct space *space = space_by_id(id);
	if (space)
		return space;
	tnt_raise(ClientError, ER_NO_SUCH_SPACE, int2str(id));
}

/**
 * Update contents of the space cache.  Typically the new space is
 * an altered version of the original space.
 * Returns the old space, if any.
 */
struct space *
space_cache_replace(struct space *space);

/** Delete a space from the space cache. */
struct space *
space_cache_delete(uint32_t id);

bool
space_is_system(struct space *space);

void
schema_init();

void
schema_free();

struct space *schema_space(uint32_t id);

/*
 * Find object id by object name.
 */
uint32_t
schema_find_id(uint32_t system_space_id, uint32_t index_id,
	       const char *name, uint32_t len);

void
func_cache_replace(struct func_def *def);

void
func_cache_delete(uint32_t fid);

struct func;

struct func *
func_by_id(uint32_t fid);

static inline struct func *
func_cache_find(uint32_t fid)
{
	struct func *func = func_by_id(fid);
	if (func == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FUNCTION, int2str(fid));
	return func;
}

struct func *
func_by_name(const char *name, uint32_t name_len);


/**
 * Check whether or not an object has grants on it (restrict
 * constraint in drop object).
 * _priv space to look up by space id
 * @retval true object has grants
 * @retval false object has no grants
 */
bool
schema_find_grants(const char *type, uint32_t id);

#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_SCHEMA_H */
