#ifndef INCLUDES_TARANTOOL_BOX_SCHEMA_H
#define INCLUDES_TARANTOOL_BOX_SCHEMA_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdint.h>
#include <stdio.h> /* snprintf */
#include "error.h"
#include "space.h"
#include "latch.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern uint32_t schema_version;

/**
 * Persistent version of the schema, stored in _schema["version"].
 */
extern uint32_t dd_version_id;

/**
 * Lock of schema modification
 */
extern struct latch schema_lock;

/**
 * Try to look up a space by space number in the space cache.
 * FFI-friendly no-exception-thrown space lookup function.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *
space_by_id(uint32_t id);

uint32_t
box_schema_version();

static inline struct space *
space_cache_find(uint32_t id)
{
	static uint32_t prev_schema_version = 0;
	static struct space *space = NULL;
	if (prev_schema_version != schema_version)
		space = NULL;
	if (space && space->def->id == id)
		return space;
	if ((space = space_by_id(id))) {
		prev_schema_version = schema_version;
		return space;
	}
	diag_set(ClientError, ER_NO_SUCH_SPACE, int2str(id));
	return NULL;
}

struct func *
func_by_name(const char *name, uint32_t name_len);

/** Call a visitor function on every space in the space cache. */
int
space_foreach(int (*func)(struct space *sp, void *udata), void *udata);

/**
 * Try to look up object name by id and type of object.
 *
 * @return NULL if object of type not found, otherwise name of object.
 */
const char *
schema_find_name(enum schema_object_type type, uint32_t object_id);

/**
 * Find a sequence by id. Return NULL if the sequence was
 * not found.
 */
struct sequence *
sequence_by_id(uint32_t id);
#if defined(__cplusplus)
} /* extern "C" */

static inline struct space *
space_cache_find_xc(uint32_t id)
{
	struct space *space = space_cache_find(id);
	if (space == NULL)
		diag_raise();
	return space;
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

/**
 * Insert a new function or update the old one.
 *
 * @param def Function definition. In a case of success the ownership
 *        of @a def is transfered to the data dictionary, thus the caller
 *        must not delete it.
 */
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


/**
 * Check whether or not an object has grants on it (restrict
 * constraint in drop object).
 * _priv space to look up by space id
 * @retval true object has grants
 * @retval false object has no grants
 */
bool
schema_find_grants(const char *type, uint32_t id);

/**
 * A wrapper around sequence_by_id() that raises an exception
 * if the sequence was not found in the cache.
 */
struct sequence *
sequence_cache_find(uint32_t id);

/**
 * Insert a new sequence object into the cache or update
 * an existing one if there's already a sequence with
 * the given id in the cache.
 */
void
sequence_cache_replace(struct sequence_def *def);

/** Delete a sequence from the sequence cache. */
void
sequence_cache_delete(uint32_t id);

#endif /* defined(__cplusplus) */

/**
 * Triggers fired after committing a change in space definition.
 * The space is passed to the trigger callback in the event
 * argument. It is the new space in case of create/update or
 * the old space in case of drop.
 */
extern struct rlist on_alter_space;

/**
 * Triggers fired after committing a change in sequence definition.
 * It is passed the txn statement that altered the sequence.
 */
extern struct rlist on_alter_sequence;

/**
 * Triggers fired after access denied error is created.
 */
extern struct rlist on_access_denied;

/**
 * Context passed to on_access_denied trigger.
 */
struct on_access_denied_ctx {
	/** Type of declined access */
	const char *access_type;
	/** Type of object the required access was denied to */
	const char *object_type;
	/** Name of object the required access was denied to */
	const char *object_name;
};

#endif /* INCLUDES_TARANTOOL_BOX_SCHEMA_H */
