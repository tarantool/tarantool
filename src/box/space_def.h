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

struct space_upgrade_def;

/** Space type names. */
extern const char *space_type_strs[];

/** See space_opts::type. */
enum space_type {
	/**
	 * SPACE_TYPE_DEFAULT is a special value which is used when decoding
	 * space options from a tuple. After the options have been parsed
	 * SPACE_TYPE_DEFAULT will be replaced with SPACE_TYPE_NORMAL.
	 * No live space should ever have this type.
	 */
	SPACE_TYPE_DEFAULT = -1,
	SPACE_TYPE_NORMAL = 0,
	SPACE_TYPE_DATA_TEMPORARY = 1,
	space_type_MAX,
};

static inline const char *
space_type_name(enum space_type space_type)
{
	assert(space_type != SPACE_TYPE_DEFAULT);
	return space_type_strs[space_type];
}

/** Space options */
struct space_opts {
	/**
	 * Replication group identifier. Defines how changes
	 * made to a space are replicated.
	 */
	uint32_t group_id;
	/**
	 * If set to SPACE_TYPE_DATA_TEMPORARY:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
	 * - in SQL: space_def memory is allocated on region and
	 *   does not require manual release.
	 */
	enum space_type type;
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
	/** Array of constraints. Can be NULL if constraints_count == 0. */
	struct tuple_constraint_def *constraint_def;
	/** Number of constraints. */
	uint32_t constraint_count;
	/** Space upgrade definition or NULL. */
	struct space_upgrade_def *upgrade_def;
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
 * Check if the space is temporary.
 */
static inline bool
space_opts_is_temporary(const struct space_opts *opts)
{
	assert(opts->type != SPACE_TYPE_DEFAULT);
	return opts->type != SPACE_TYPE_NORMAL;
}

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
	/**
	 * Encoding of original (i.e., user-provided) format clause to MsgPack,
	 * allocated via malloc.
	 */
	char *format_data;
	/** Length of MsgPack encoded format clause. */
	size_t format_data_len;
	char name[0];
};

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
 *
 * The function never fails (never returns NULL).
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
	      uint32_t field_count, const char *format_data,
	      size_t format_data_len);

/**
 * Create a new ephemeral space definition.
 * @param exact_field_count Number of fields in the space.
 * @param fields Field definitions.
 *
 * @retval Space definition.
 */
struct space_def *
space_def_new_ephemeral(uint32_t exact_field_count, struct field_def *fields);

struct tuple_format;
struct tuple_format_vtab;
struct key_def;
/**
 * Convenient @sa tuple_format_new helper.
 * @param vtab Virtual function table for specific engines.
 * @param engine Pointer to storage engine.
 * @param keys Array of key_defs of a space.
 * @param key_count The number of keys in @a keys array.
 * @param def Source of the rest tuple_format_new arguments
 */
struct tuple_format *
space_tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
		       struct key_def *const *keys, uint16_t key_count,
		       const struct space_def *def);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

static inline struct space_def *
space_def_new_xc(uint32_t id, uint32_t uid, uint32_t exact_field_count,
		 const char *name, uint32_t name_len,
		 const char *engine_name, uint32_t engine_len,
		 const struct space_opts *opts, const struct field_def *fields,
		 uint32_t field_count, const char *format_data,
		 size_t format_data_len)
{
	struct space_def *ret = space_def_new(id, uid, exact_field_count, name,
					      name_len, engine_name, engine_len,
					      opts, fields, field_count,
					      format_data, format_data_len);
	if (ret == NULL)
		diag_raise();
	return ret;
}

#endif /* __cplusplus */

#endif /* TARANTOOL_BOX_SPACE_DEF_H_INCLUDED */
