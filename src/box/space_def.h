#ifndef TARANTOOL_BOX_SPACE_DEF_H_INCLUDED
#define TARANTOOL_BOX_SPACE_DEF_H_INCLUDED
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
#include "tuple_dictionary.h"
#include "schema_def.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Space options */
struct space_opts {
	/**
	 * Replication group identifier. Defines how changes
	 * made to a space are replicated.
	 */
	uint32_t group_id;
        /**
	 * The space is a temporary:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
         * - in SQL: space_def memory is allocated on region and
         *   does not require manual release.
	 */
	bool is_temporary;
	/**
	 * This flag is set if space is ephemeral and hence
	 * its format might be re-used.
	 */
	bool is_ephemeral;
	/**
	 * If the space is a view, then it can't feature any
	 * indexes, and must have SQL statement. Moreover,
	 * this flag can't be changed after space creation.
	 */
	bool is_view;
	/**
	 * Synchronous space makes all transactions, affecting its
	 * data, synchronous. That means they are not applied
	 * until replicated to a quorum of replicas.
	 */
	bool is_sync;
	/**
	 * Setting this flag for a Vinyl space defers generation of DELETE
	 * statements for secondary indexes till the primary index compaction,
	 * which should speed up writes, but may also slow down reads.
	 */
	bool defer_deletes;
	/** SQL statement that produced this space. */
	char *sql;
};

extern const struct space_opts space_opts_default;
extern const struct opt_def space_opts_reg[];

/**
 * Create space options using default values.
 */
static inline void
space_opts_create(struct space_opts *opts)
{
	/* default values of opts */
	*opts = space_opts_default;
}

/**
 * Destroy space options
 */
void
space_opts_destroy(struct space_opts *opts);

/** Space metadata. */
struct space_def {
	/** Space id. */
	uint32_t id;
	/** User id of the creator of the space */
	uint32_t uid;
	/**
	 * If not set (is 0), any tuple in the
	 * space can have any number of fields.
	 * If set, each tuple
	 * must have exactly this many fields.
	 */
	uint32_t exact_field_count;
	char engine_name[ENGINE_NAME_MAX + 1];
	/**
	 * Tuple field names dictionary, shared with a space's
	 * tuple format.
	 */
	struct tuple_dictionary *dict;
	/** Space fields, specified by a user. */
	struct field_def *fields;
	/** Length of @a fields. */
	uint32_t field_count;
	/** Number of SQL views which refer to this space. */
	uint32_t view_ref_count;
	struct space_opts opts;
	char name[0];
};

/*
 * Free a default value syntax trees of @a defs.
 * @param fields Fields array to destroy.
 * @param field_count Length of @a fields.
 * @param extern_alloc Fields expression AST allocated externally.
 *                     (specify false when sql_expr_delete should
 *                      release default_value_expr memory,
 *                      true - when shouldn't)
 */
void
space_def_destroy_fields(struct field_def *fields, uint32_t field_count,
			 bool extern_alloc);

/**
 * Delete the space_def object.
 * @param def Def to delete.
 */
void
space_def_delete(struct space_def *def);

/**
 * Duplicate space_def object.
 * @param src Def to duplicate.
 * @retval Copy of the @src.
 */
struct space_def *
space_def_dup(const struct space_def *src);

/**
 * Create a new space definition.
 * @param id Space identifier.
 * @param uid Owner identifier.
 * @param exact_field_count Space tuples field count.
 *        0 for any count.
 * @param name Space name.
 * @param name_len Length of the @name.
 * @param engine_name Engine name.
 * @param engine_len Length of the @engine.
 * @param opts Space options.
 * @param fields Field definitions.
 * @param field_count Length of @a fields.
 *
 * @retval Space definition.
 */
struct space_def *
space_def_new(uint32_t id, uint32_t uid, uint32_t exact_field_count,
	      const char *name, uint32_t name_len,
	      const char *engine_name, uint32_t engine_len,
	      const struct space_opts *opts, const struct field_def *fields,
	      uint32_t field_count);

/**
 * Create a new ephemeral space definition.
 * @param exact_field_count Number of fields in the space.
 * @param fields Field definitions.
 *
 * @retval Space definition.
 */
struct space_def *
space_def_new_ephemeral(uint32_t exact_field_count, struct field_def *fields);

/**
 * Size of the space_def.
 * @param name_len Length of the space name.
 * @param fields Fields array of space format.
 * @param field_count Space field count.
 * @param[out] names_offset Offset from the beginning of a def to
 *             a field names memory.
 * @param[out] fields_offset Offset from the beginning of a def to
 *             a fields array.
 * @param[out] def_expr_offset Offset from the beginning of a def
 *             to a def_value_expr array.
 * @retval Size in bytes.
 */
size_t
space_def_sizeof(uint32_t name_len, const struct field_def *fields,
		 uint32_t field_count, uint32_t *names_offset,
		 uint32_t *fields_offset, uint32_t *def_expr_offset);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline struct space_def *
space_def_dup_xc(const struct space_def *src)
{
	struct space_def *ret = space_def_dup(src);
	if (ret == NULL)
		diag_raise();
	return ret;
}

static inline struct space_def *
space_def_new_xc(uint32_t id, uint32_t uid, uint32_t exact_field_count,
		 const char *name, uint32_t name_len,
		 const char *engine_name, uint32_t engine_len,
		 const struct space_opts *opts, const struct field_def *fields,
		 uint32_t field_count)
{
	struct space_def *ret = space_def_new(id, uid, exact_field_count, name,
					      name_len, engine_name, engine_len,
					      opts, fields, field_count);
	if (ret == NULL)
		diag_raise();
	return ret;
}

#endif /* __cplusplus */

#endif /* TARANTOOL_BOX_SPACE_DEF_H_INCLUDED */
