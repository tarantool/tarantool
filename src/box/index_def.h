#ifndef TARANTOOL_BOX_INDEX_DEF_H_INCLUDED
#define TARANTOOL_BOX_INDEX_DEF_H_INCLUDED
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

#include "key_def.h"
#include "opt_def.h"
#include "schema_def.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum index_type {
	HASH = 0, /* HASH Index */
	TREE,     /* TREE Index */
	BITSET,   /* BITSET Index */
	RTREE,    /* R-Tree Index */
	index_type_MAX,
};

extern const char *index_type_strs[];

/** Settings for the hint config option. */
enum index_hint_cfg {
	INDEX_HINT_DEFAULT = 0,
	INDEX_HINT_ON,
	INDEX_HINT_OFF
};

enum rtree_index_distance_type {
	 /* Euclid distance, sqrt(dx*dx + dy*dy) */
	RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* Manhattan distance, fabs(dx) + fabs(dy) */
	RTREE_INDEX_DISTANCE_TYPE_MANHATTAN,
	rtree_index_distance_type_MAX
};
extern const char *rtree_index_distance_type_strs[];

/** Index options */
struct index_opts {
	/**
	 * Is this index unique or not - relevant to HASH/TREE
	 * index
	 */
	bool is_unique;
	/**
	 * RTREE index dimension.
	 */
	int64_t dimension;
	/**
	 * RTREE distance type.
	 */
	enum rtree_index_distance_type distance;
	/**
	 * Vinyl index options.
	 */
	int64_t range_size;
	int64_t page_size;
	/**
	 * Maximal number of runs that can be created in a level
	 * of the LSM tree before triggering compaction.
	 */
	int64_t run_count_per_level;
	/**
	 * The LSM tree multiplier. Each subsequent level of
	 * the LSM tree is run_size_ratio times larger than
	 * previous one.
	 */
	double run_size_ratio;
	/* Bloom filter false positive rate. */
	double bloom_fpr;
	/**
	 * LSN from the time of index creation.
	 */
	int64_t lsn;
	/** Identifier of the functional index function. */
	uint32_t func_id;
	/**
	 * Use hint optimization for tree index.
	 */
	enum index_hint_cfg hint;
	/**
	 * Engine dependent. For engines supporting covering indexes means
	 * explicitly covered fields. That is fields other then fields of
	 * index key and primary index key. The latter fields are always
	 * covered. Sorted in ascending order.
	 */
	uint32_t *covered_fields;
	/**
	 * Number of covered fields.
	 */
	uint32_t covered_field_count;
	/**
	 * Engine dependent. For engines supporting various layouts means a
	 * string with the layout options.
	 */
	char *layout;
};

extern const struct index_opts index_opts_default;
extern const struct opt_def index_opts_reg[];

/**
 * Create index options using default values
 */
static inline void
index_opts_create(struct index_opts *opts)
{
	*opts = index_opts_default;
}

/**
 * Destroy index options
 */
static inline void
index_opts_destroy(struct index_opts *opts)
{
	free(opts->covered_fields);
	free(opts->layout);
	TRASH(opts);
}

static inline bool
index_opts_is_equal(const struct index_opts *o1, const struct index_opts *o2)
{
	if (o1->is_unique != o2->is_unique)
		return false;
	if (o1->dimension != o2->dimension)
		return false;
	if (o1->distance != o2->distance)
		return false;
	if (o1->range_size != o2->range_size)
		return false;
	if (o1->page_size != o2->page_size)
		return false;
	if (o1->run_count_per_level != o2->run_count_per_level)
		return false;
	if (o1->run_size_ratio != o2->run_size_ratio)
		return false;
	if (o1->bloom_fpr != o2->bloom_fpr)
		return false;
	if (o1->func_id != o2->func_id)
		return false;
	if (o1->hint != o2->hint)
		return false;
	if (o1->covered_field_count != o2->covered_field_count)
		return false;
	for (uint32_t i = 0; i < o1->covered_field_count; i++) {
		if (o1->covered_fields[i] != o2->covered_fields[i])
			return false;
	}
	if (o1->layout != NULL && o2->layout != NULL) {
		if (strcmp(o1->layout, o2->layout) != 0)
			return false;
	} else if (o1->layout != NULL || o2->layout != NULL) {
		return false;
	}
	return true;
}

/* Definition of an index. */
struct index_def {
	/** A link in key list. */
	struct rlist link;
	/** Ordinal index number in the index array. */
	uint32_t iid;
	/** Space id. */
	uint32_t space_id;
	/** Space name. */
	char *space_name;
	/** Engine name. */
	char engine_name[ENGINE_NAME_MAX + 1];
	/** Index name. */
	char *name;
	/** Index type. */
	enum index_type type;
	/** Index options. */
	struct index_opts opts;
	/** Index key definition. */
	struct key_def *key_def;
	/**
	 * User-defined key definition, merged with the primary
	 * key parts. Used by non-unique keys to uniquely identify
	 * iterator position.
	 */
	struct key_def *cmp_def;
	/**
	 * Primary key definition. Despite the fact that cmp_def already
	 * contains primary key definition, our key_def machinery does not
	 * allow to work with it in any convenient way. This field allows
	 * to use primary key definition easily without any dependencies on
	 * space and its primary index.
	 */
	struct key_def *pk_def;
};

struct index_def *
index_def_dup(const struct index_def *def);

/* Destroy and free an index_def. */
void
index_def_delete(struct index_def *def);

/**
 * Update 'has_optional_parts' property of key definitions.
 * @param def Index def, containing key definitions to update.
 * @param min_field_count Minimal field count. All parts out of
 *        this value are optional.
 */
static inline void
index_def_update_optionality(struct index_def *def, uint32_t min_field_count)
{
	key_def_update_optionality(def->key_def, min_field_count);
	key_def_update_optionality(def->cmp_def, min_field_count);
}

/**
 * Update func pointer for a functional index key definition.
 * @param def Index def, containing key definitions to update.
 * @param func The functional index function pointer.
 */
static inline void
index_def_set_func(struct index_def *def, struct func *func)
{
	assert(def->opts.func_id > 0 &&
	       def->key_def->for_func_index && def->cmp_def->for_func_index);
	/*
	 * def->key_def is used in key_list module to build a key
	 * a key for given tuple.
	 */
	def->key_def->func_index_func = func;
	/* The functional index doesn't use cmp_def, so do not set it. */
	def->cmp_def->func_index_func = NULL;
}

/**
 * Get a func pointer by index definition.
 * @param def Index def, containing key definitions.
 * @returns not NULL function pointer when index definition
 *          refers to function and NULL otherwise.
 */
static inline struct func *
index_def_get_func(struct index_def *def)
{
	return def->key_def->func_index_func;
}

/**
 * Add an index definition to a list, preserving the
 * first position of the primary key.
 *
 * In non-unique indexes, secondary keys must contain key parts
 * of the primary key. This is necessary to make ordered
 * retrieval from a secondary key useful to SQL
 * optimizer and make iterators over secondary keys stable
 * in presence of concurrent updates.
 * Thus we always create the primary key first, and put
 * the primary key key_def first in the index_def list.
 */
static inline void
index_def_list_add(struct rlist *index_def_list, struct index_def *index_def)
{
	/** Preserve the position of the primary key */
	if (index_def->iid == 0)
		rlist_add_entry(index_def_list, index_def, link);
	else
		rlist_add_tail_entry(index_def_list, index_def, link);
}

/**
 * Create a new index definition.
 * Does not validate identifier - caller must do it manually.
 *
 * @param key_def  key definition, must be fully built
 * @param pk_def   primary key definition, pass non-NULL
 *                 for secondary keys to construct
 *                 index_def::cmp_def
 * Never fails, always returns non-NULL value.
 */
struct index_def *
index_def_new(uint32_t space_id, uint32_t iid, const char *name,
	      uint32_t name_len, const char *space_name,
	      const char *engine_name, enum index_type type,
	      const struct index_opts *opts,
	      struct key_def *key_def, struct key_def *pk_def);

/**
 * Create an array (on a region) of key_defs from list of index
 * definitions.
 *
 * @param index_defs List head.
 * @param[out] size  Array size.
 * @retval Array of pointers to key_def (NULL if size == 0).
 */
struct key_def **
index_def_to_key_def(struct rlist *index_defs, int *size);

/**
 * Check whether index definitions def1 and def2 are equals.
 *
 * @param def1 index definition 1.
 * @param def2 index definition 2.
 * @retval true if definitions are equal, false if not.
 */
bool
index_def_is_equal(const struct index_def *def1, const struct index_def *def2);

/**
 * Check a key definition for violation of various limits.
 *
 * @param index_def index definition
 * @param space_name space name
 * @retval 0 Success.
 * @retval -1 Error. Diag is set.
 */
int
index_def_check(struct index_def *index_def, const char *space_name);

/**
 * Check types of tuple fields indexed by `index_def'.
 *
 * @param index_def index definition
 * @param space_name space name
 * @retval 0 Success.
 * @retval -1 Error. Diag is set.
 */
int
index_def_check_field_types(struct index_def *index_def,
			    const char *space_name);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_INDEX_DEF_H_INCLUDED */
