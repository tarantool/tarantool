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
#include "schema_def.h"

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
 * List of schema features, which are not available only since some schema
 * version and should be blocked until box.schema.upgrade() is called.
 *
 * Feature description is:
 *   _(<token>, <number>, <major>, <minor>, <patch>)
 * where:
 *   token			- the enum constant for the feature, also
 *				  used in the error message.
 *   number			- sequential number of the feature
 *   major, minor, patch	- version number
 *
 * If schema version is less than 2.11.1, then all DDL is blocked until
 * user upgrades at least to 2.11.1. This list consists of features,
 * which appeared after 2.11.1 and which are blocked until some version.
 *
 * The only exception for now is persistent names feature. Even though
 * they appeared in 3.0.0, we allow using them on schema version 2.11.5
 * to simplify the upgrade process.
 */
#define SCHEMA_FEATURES(_) \
	_(SCHEMA_FEATURE_DDL_BEFORE_UPGRADE, 0, 2, 11, 1) \
	_(SCHEMA_FEATURE_PERSISTENT_NAMES, 1, 2, 11, 5) \
	_(SCHEMA_FEATURE_PERSISTENT_TRIGGERS, 2, 3, 1, 0) \

ENUM(schema_feature, SCHEMA_FEATURES);
extern const char *schema_feature_strs[];
extern const struct version schema_feature_version[];

/**
 * Checks whether the feature is available on the current schema.
 *
 * @param feature identifier of the feature.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
schema_check_feature(enum schema_feature feature);

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
 * operations.
 * \sa IPROTO_SCHEMA_VERSION
 */
API_EXPORT uint64_t
box_schema_version(void);

/** \endcond public */

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
 * Triggers fired after committing a change in _func space.
 */
extern struct rlist on_alter_func;

/** Global grants to classes of objects. */
struct entity_access {
	struct accesses space;
	struct accesses function;
	struct accesses user;
	struct accesses role;
	struct accesses sequence;
};

/** A single instance of the global entities. */
extern struct entity_access entity_access;

/* Get user access for an object class. */
static inline struct access
entity_access_get(enum schema_object_type type, auth_token_t auth_token)
{
	switch (type) {
	case SC_SPACE:
		return accesses_get(&entity_access.space, auth_token);
	case SC_FUNCTION:
		return accesses_get(&entity_access.function, auth_token);
	case SC_USER:
		return accesses_get(&entity_access.user, auth_token);
	case SC_ROLE:
		return accesses_get(&entity_access.role, auth_token);
	case SC_SEQUENCE:
		return accesses_get(&entity_access.sequence, auth_token);
	default:
		return (struct access){0, 0};
	}
}

#endif /* INCLUDES_TARANTOOL_BOX_SCHEMA_H */
