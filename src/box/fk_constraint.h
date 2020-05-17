#ifndef TARANTOOL_BOX_FK_CONSTRAINT_H_INCLUDED
#define TARANTOOL_BOX_FK_CONSTRAINT_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include <stdint.h>
#include "trivia/util.h"
#include "small/slab_arena.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct sql;

enum fk_constraint_action {
	FKEY_NO_ACTION = 0,
	FKEY_ACTION_SET_NULL,
	FKEY_ACTION_SET_DEFAULT,
	FKEY_ACTION_CASCADE,
	FKEY_ACTION_RESTRICT,
	fk_constraint_action_MAX
};

enum fk_constraint_match {
	FKEY_MATCH_SIMPLE = 0,
	FKEY_MATCH_PARTIAL,
	FKEY_MATCH_FULL,
	fk_constraint_match_MAX
};

enum {
	FIELD_LINK_PARENT = 0,
	FIELD_LINK_CHILD = 1,
};

extern const char *fk_constraint_action_strs[];

extern const char *fk_constraint_match_strs[];

/** Structure describing field dependencies for foreign keys. */
struct field_link {
	/**
	 * There are two ways to access parent/child fields -
	 * as array of two elements and as named fields.
	 */
	union {
		struct {
			uint32_t parent_field;
			uint32_t child_field;
		};
		uint32_t fields[2];
	};
};

/** Definition of foreign key constraint. */
struct fk_constraint_def {
	/** Id of space containing the REFERENCES clause (child). */
	uint32_t child_id;
	/** Id of space that the key points to (parent). */
	uint32_t parent_id;
	/** Number of fields in this key. */
	uint32_t field_count;
	/** True if constraint checking is deferred till COMMIT. */
	bool is_deferred;
	/** Match condition for foreign key. SIMPLE by default. */
	enum fk_constraint_match match;
	/** ON DELETE action. NO ACTION by default. */
	enum fk_constraint_action on_delete;
	/** ON UPDATE action. NO ACTION by default. */
	enum fk_constraint_action on_update;
	/** Mapping of fields in child to fields in parent. */
	struct field_link *links;
	/** Name of the constraint. */
	char name[0];
};

/** Structure representing foreign key relationship. */
struct fk_constraint {
	struct fk_constraint_def *def;
	/** Index id of referenced index in parent space. */
	uint32_t index_id;
	/** Triggers for actions. */
	struct sql_trigger *on_delete_trigger;
	struct sql_trigger *on_update_trigger;
	/** Links for parent and child lists. */
	struct rlist in_parent_space;
	struct rlist in_child_space;
};

/**
 * Alongside with struct fk_constraint_def itself, we reserve memory for
 * string containing its name and for array of links.
 * Memory layout:
 * +----------------------------------+ <- Allocated memory starts here
 * |     struct fk_constraint_def     |
 * |----------------------------------|
 * |             name + \0            |
 * |----------------------------------|
 * |       memory align padding       |
 * |----------------------------------|
 * |             links                |
 * +----------------------------------+
 */
static inline size_t
fk_constraint_def_sizeof(uint32_t link_count, uint32_t name_len,
			 uint32_t *links_offset)
{
	*links_offset = small_align(sizeof(struct fk_constraint_def) +
				    name_len + 1, alignof(struct field_link));
	return *links_offset + link_count * sizeof(struct field_link);
}

static inline bool
fk_constraint_is_self_referenced(const struct fk_constraint_def *fk_c)
{
	return fk_c->child_id == fk_c->parent_id;
}

/** Release memory for foreign key and its triggers, if any. */
void
fk_constraint_delete(struct fk_constraint *fkey);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* __cplusplus */

#endif /* TARANTOOL_BOX_FK_CONSTRAINT_H_INCLUDED */
