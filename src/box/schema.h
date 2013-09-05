#ifndef INCLUDES_TARANTOOL_BOX_SCHEMA_H
#define INCLUDES_TARANTOOL_BOX_SCHEMA_H
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
#include "exception.h"

enum schema_id {
	/** Start of the reserved range of system spaces. */
	SC_SYSTEM_ID_MIN = 256,
	/** Space id of _schema. */
	SC_SCHEMA_ID = 272,
	/** Space id of _space. */
	SC_SPACE_ID = 280,
	/** Space id of _index. */
	SC_INDEX_ID = 288,
	/** End of the reserved range of system spaces. */
	SC_SYSTEM_ID_MAX = 511
};

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

static inline struct space *
space_find(uint32_t id)
{
	struct space *space = space_by_id(id);
	if (space)
		return space;

	tnt_raise(ClientError, ER_NO_SUCH_SPACE, id);
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

/**
 * Called at the end of recovery from snapshot.
 * Build primary keys in all spaces.
 * */
void
space_end_recover_snapshot();

/**
 * Called at the end of recovery.
 * Build secondary keys in all spaces.
 */
void
space_end_recover();

struct space *schema_space(uint32_t id);

#endif /* INCLUDES_TARANTOOL_BOX_SCHEMA_H */
