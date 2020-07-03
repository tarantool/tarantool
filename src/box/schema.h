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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

extern uint32_t schema_version;
extern uint32_t space_cache_version;

/** Triggers invoked after schema initialization. */
extern struct rlist on_schema_init;

/**
 * Try to look up a space by space number in the space cache.
 * FFI-friendly no-exception-thrown space lookup function.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *
space_by_id(uint32_t id);

/**
 * Try to look up a space by space name in the space name cache.
 *
 * @return NULL if space not found, otherwise space object.
 */
struct space *
space_by_name(const char *name);

uint32_t
box_schema_version(void);

static inline struct space *
space_cache_find(uint32_t id)
{
	static uint32_t prev_space_cache_version = 0;
	static struct space *space = NULL;
	if (prev_space_cache_version != space_cache_version)
		space = NULL;
	if (space && space->def->id == id)
		return space;
	if ((space = space_by_id(id))) {
		prev_space_cache_version = space_cache_version;
		return space;
	}
	diag_set(ClientError, ER_NO_SUCH_SPACE, int2str(id));
	return NULL;
}

/**
 * Insert a new function object in the function cache.
 * @param func Function object to insert.
 */
void
func_cache_insert(struct func *func);

void
func_cache_delete(uint32_t fid);

struct func *
func_by_id(uint32_t fid);

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

bool
space_is_system(struct space *space);

/**
 * Find a sequence by id. Return NULL if the sequence was
 * not found.
 */
struct sequence *
sequence_by_id(uint32_t id);

/**
 * Find object id by name in specified system space with index.
 *
 * @param system_space_id identifier of the system object.
 * @param index_id identifier of the index to lookup.
 * @param name of object to lookup.
 * @param len length of a name.
 * @param object_id[out] object_id or BOX_ID_NIL - not found.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
schema_find_id(uint32_t system_space_id, uint32_t index_id, const char *name,
	       uint32_t len, uint32_t *object_id);

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
 * Update contents of the space cache.
 *
 * If @old_space is NULL, insert @new_space into the cache.
 * If @new_space is NULL, delete @old_space from the cache.
 * If neither @old_space nor @new_space is NULL, replace
 * @old_space with @new_space in the cache (both spaces must
 * have the same id).
 */
void
space_cache_replace(struct space *old_space, struct space *new_space);

void
schema_init(void);

void
schema_free(void);

struct space *schema_space(uint32_t id);


/**
 * Check whether or not an object has grants on it (restrict
 * constraint in drop object).
 * _priv space to look up by space id
 * @retval (bool *out) true object has grants
 * @retval (bool *out) false object has no grants
 */
int
schema_find_grants(const char *type, uint32_t id, bool *out);

/**
 * A wrapper around sequence_by_id() that raises an exception
 * if the sequence was not found in the cache.
 */
struct sequence *
sequence_cache_find(uint32_t id);

/**
 * Insert a new sequence object into the cache.
 * There must not be a sequence with the same id
 * in the cache.
 */
void
sequence_cache_insert(struct sequence *seq);

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
 * Triggers fired after committing a change in _func space.
 */
extern struct rlist on_alter_func;

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

/** Global grants to classes of objects. */
struct entity_access {
       struct access space[BOX_USER_MAX];
       struct access function[BOX_USER_MAX];
       struct access user[BOX_USER_MAX];
       struct access role[BOX_USER_MAX];
       struct access sequence[BOX_USER_MAX];
};

/** A single instance of the global entities. */
extern struct entity_access entity_access;

static inline
struct access *
entity_access_get(enum schema_object_type type)
{
	switch (type) {
	case SC_SPACE:
	case SC_ENTITY_SPACE:
		return entity_access.space;
	case SC_FUNCTION:
	case SC_ENTITY_FUNCTION:
		return entity_access.function;
	case SC_USER:
	case SC_ENTITY_USER:
		return entity_access.user;
	case SC_ROLE:
	case SC_ENTITY_ROLE:
		return entity_access.role;
	case SC_SEQUENCE:
	case SC_ENTITY_SEQUENCE:
		return entity_access.sequence;
	default:
		return NULL;
	}
}

#endif /* INCLUDES_TARANTOOL_BOX_SCHEMA_H */
