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
#include "trivia/util.h"
#include "tuple_dictionary.h"
#include "schema_def.h"
#include <stdlib.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Space options */
struct space_opts {
        /**
	 * The space is a temporary:
	 * - it is empty at server start
	 * - changes are not written to WAL
	 * - changes are not part of a snapshot
	 */
	bool temporary;
	/**
	 * If the space is a view, then it can't feature any
	 * indexes, and must have SQL statement. Moreover,
	 * this flag can't be changed after space creation.
	 */
	bool is_view;
	/**
	 * SQL statement that produced this space.
	 */
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
static inline void
space_opts_destroy(struct space_opts *opts)
{
	free(opts->sql);
	TRASH(opts);
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
	struct space_opts opts;
	char name[0];
};

/*
 * Free a default value syntax trees of @a defs.
 * @param fields Fields array to destroy.
 * @param field_count Length of @a fields.
 */
void
space_def_destroy_fields(struct field_def *fields, uint32_t field_count);

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
