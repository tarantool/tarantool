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
#include "func_cache.h"
#include "space_cache.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct func;

/**
 * See `box_schema_version`.
 */
extern uint64_t schema_version;
extern uint32_t dd_version_id;

/** Triggers invoked after schema initialization. */
extern struct rlist on_schema_init;

/**
 * Returns true if data dictionary checks may be skipped by the current fiber.
 *
 * We disable some data dictionary checks for schema upgrade and downgrade, for
 * example, we allow dropping a system space.
 */
bool
dd_check_is_disabled(void);

/** \cond public */

/**
 * Returns the current version of the database schema, an unsigned number
 * that goes up when there is a major change in the schema, i.e., on DDL
 * operations (\sa IPROTO_SCHEMA_VERSION).
 */
API_EXPORT uint64_t
box_schema_version(void);

/** \endcond public */

/** Return current persistent schema version. */
uint32_t
box_dd_version_id(void);

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
