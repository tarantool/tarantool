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
#include "space.h"
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <cfg/warning.h>
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */
#include <tarantool.h>
#include <exception.h>
#include "tuple.h"
#include <pickle.h>
#include <palloc.h>
#include <assoc.h>

#include <box/box.h>

static struct mh_i32ptr_t *spaces;

/**
 * Secondary indexes are built in bulk after all data is
 * recovered. This flag indicates that the indexes are
 * already built and ready for use.
 */
static bool secondary_indexes_enabled = false;
/**
 * Primary indexes are enabled only after reading the snapshot.
 */
static bool primary_indexes_enabled = false;


static void
space_create(struct space *space, uint32_t space_no,
	     struct key_def *key_defs, uint32_t key_count,
	     uint32_t arity)
{
	memset(space, 0, sizeof(struct space));
	space->no = space_no;
	space->arity = arity;
	space->key_defs = key_defs;
	space->key_count = key_count;
	space->format = tuple_format_new(key_defs, key_count);
	/* fill space indexes */
	for (uint32_t j = 0; j < key_count; ++j) {
		struct key_def *key_def = &space->key_defs[j];
		Index *index = Index::factory(key_def->type, key_def, space);
		if (index == NULL) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE,
				  "class Index", "malloc");
		}
		space->index[j] = index;
	}
}

static void
space_destroy(struct space *space)
{
	for (uint32_t j = 0 ; j < space->key_count; j++) {
		Index *index = space->index[j];
		delete index;
		key_def_destroy(&space->key_defs[j]);
	}
	free(space->key_defs);
}

struct space *
space_new(uint32_t space_no, struct key_def *key_defs,
	  uint32_t key_count, uint32_t arity)
{
	struct space *space = space_by_n(space_no);
	if (space)
		tnt_raise(LoggedError, ER_SPACE_EXISTS, space_no);

	space = (struct space *) malloc(sizeof(struct space));

	space_create(space, space_no, key_defs, key_count, arity);

	const struct mh_i32ptr_node_t node = { space->no, space };
	mh_i32ptr_put(spaces, &node, NULL, NULL);

	return space;
}

static void
space_delete(struct space *space)
{
	const struct mh_i32ptr_node_t node = { space->no, NULL };
	mh_int_t k = mh_i32ptr_get(spaces, &node, NULL);
	assert(k != mh_end(spaces));
	mh_i32ptr_del(spaces, k, NULL);
	space_destroy(space);
	free(space);
}

/* return space by its number */
struct space *
space_by_n(uint32_t n)
{
	const struct mh_i32ptr_node_t node = { n, NULL };
	mh_int_t space = mh_i32ptr_get(spaces, &node, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *) mh_i32ptr_node(spaces, space)->val;
}

/** Return the number of active indexes in a space. */
static inline int
index_count(struct space *sp)
{
	if (!secondary_indexes_enabled) {
		/* If secondary indexes are not enabled yet,
		   we can use only the primary index. So return
		   1 if there is at least one index (which
		   must be primary) and return 0 otherwise. */
		return sp->key_count > 0;
	} else {
		/* Return the actual number of indexes. */
		return sp->key_count;
	}
}

/**
 * Visit all enabled spaces and apply 'func'.
 */
void
space_foreach(void (*func)(struct space *sp, void *udata), void *udata) {

	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		func(space, udata);
	}
}

struct tuple *
space_replace(struct space *sp, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode)
{
	uint32_t i = 0;
	try {
		/* Update the primary key */
		Index *pk = sp->index[0];
		assert(pk->key_def->is_unique);
		/*
		 * If old_tuple is not NULL, the index
		 * has to find and delete it, or raise an
		 * error.
		 */
		old_tuple = pk->replace(old_tuple, new_tuple, mode);

		assert(old_tuple || new_tuple);
		uint32_t n = index_count(sp);
		/* Update secondary keys */
		for (i = i + 1; i < n; i++) {
			Index *index = sp->index[i];
			index->replace(old_tuple, new_tuple, DUP_INSERT);
		}
		return old_tuple;
	} catch (const Exception& e) {
		/* Rollback all changes */
		for (; i > 0; i--) {
			Index *index = sp->index[i-1];
			index->replace(new_tuple, old_tuple, DUP_INSERT);
		}
		throw;
	}

	assert(false);
	return NULL;
}

void
space_validate_tuple(struct space *sp, struct tuple *new_tuple)
{
	if (sp->arity > 0 && sp->arity != new_tuple->field_count)
		tnt_raise(IllegalParams,
			  "tuple field count must match space cardinality");

}

void
space_free(void)
{
	while (mh_size(spaces) > 0) {
		mh_int_t i = mh_first(spaces);

		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		space_delete(space);
	}
	tuple_free();
}


static void
space_config()
{
	/* exit if no spaces are configured */
	if (cfg.space == NULL) {
		return;
	}

	/* fill box spaces */
	for (uint32_t i = 0; cfg.space[i] != NULL; ++i) {
		tarantool_cfg_space *cfg_space = cfg.space[i];

		if (!CNF_STRUCT_DEFINED(cfg_space) || !cfg_space->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_space);

		uint32_t arity = (cfg_space->cardinality != -1 ?
				  cfg_space->cardinality : 0);
		/*
		 * Collect key/field info. We need aggregate
		 * information on all keys before we can create
		 * indexes.
		 */
		uint32_t key_count = 0;
		while (cfg_space->index[key_count] != NULL)
			key_count++;

		struct key_def *key_defs = (struct key_def *)
			malloc(key_count * sizeof(struct key_def));

		for (uint32_t j = 0; cfg_space->index[j] != NULL; ++j) {
			auto cfg_index = cfg_space->index[j];
			key_def_create(&key_defs[j], cfg_index);
		}
		(void) space_new(i, key_defs, key_count, arity);

		say_info("space %i successfully configured", i);
	}
}

void
space_init(void)
{
	spaces = mh_i32ptr_new();
	tuple_init();

	/* configure regular spaces */
	space_config();
}

void
begin_build_primary_indexes(void)
{
	assert(primary_indexes_enabled == false);

	mh_int_t i;

	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		Index *index = space->index[0];
		index->beginBuild();
	}
}

void
end_build_primary_indexes(void)
{
	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		Index *index = space->index[0];
		index->endBuild();
	}
	primary_indexes_enabled = true;
}

void
build_secondary_indexes(void)
{
	assert(primary_indexes_enabled == true);
	assert(secondary_indexes_enabled == false);

	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;

		if (space->key_count <= 1)
			continue; /* no secondary keys */

		say_info("Building secondary keys in space %d...", space->no);

		Index *pk = space->index[0];
		for (uint32_t j = 1; j < space->key_count; j++) {
			Index *index = space->index[j];
			index->build(pk);
		}

		say_info("Space %d: done", space->no);
	}

	/* enable secondary indexes now */
	secondary_indexes_enabled = true;
}

int
check_spaces(struct tarantool_cfg *conf)
{
	/* exit if no spaces are configured */
	if (conf->space == NULL) {
		return 0;
	}

	for (size_t i = 0; conf->space[i] != NULL; ++i) {
		auto space = conf->space[i];

		if (i >= BOX_SPACE_MAX) {
			out_warning(CNF_OK, "(space = %zu) invalid id, (maximum=%u)",
				    i, BOX_SPACE_MAX);
			return -1;
		}

		if (!CNF_STRUCT_DEFINED(space)) {
			/* space undefined, skip it */
			continue;
		}

		if (!space->enabled) {
			/* space disabled, skip it */
			continue;
		}

		if (conf->memcached_port && i == conf->memcached_space) {
			out_warning(CNF_OK, "Space %zu is already used as "
				    "memcached_space.", i);
			return -1;
		}

		/* at least one index in space must be defined
		 * */
		if (space->index == NULL) {
			out_warning(CNF_OK, "(space = %zu) "
				    "at least one index must be defined", i);
			return -1;
		}

		uint32_t max_key_fieldno = 0;

		/* check spaces indexes */
		for (size_t j = 0; space->index[j] != NULL; ++j) {
			auto index = space->index[j];
			uint32_t key_part_count = 0;
			enum index_type index_type;

			/* check index bound */
			if (j >= BOX_INDEX_MAX) {
				/* maximum index in space reached */
				out_warning(CNF_OK, "(space = %zu index = %zu) "
					    "too many indexed (%u maximum)", i, j, BOX_INDEX_MAX);
				return -1;
			}

			/* at least one key in index must be defined */
			if (index->key_field == NULL) {
				out_warning(CNF_OK, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			/* check unique property */
			if (index->unique == -1) {
				/* unique property undefined */
				out_warning(CNF_OK, "(space = %zu index = %zu) "
					    "unique property is undefined", i, j);
			}

			for (size_t k = 0; index->key_field[k] != NULL; ++k) {
				auto key = index->key_field[k];

				if (key->fieldno == -1) {
					/* last key reached */
					break;
				}

				if (key->fieldno >= BOX_FIELD_MAX) {
					/* maximum index in space reached */
					out_warning(CNF_OK, "(space = %zu index = %zu) "
						    "invalid field number (%u maximum)",
						    i, j, BOX_FIELD_MAX);
					return -1;
				}

				/* key must has valid type */
				if (STR2ENUM(field_type, key->type) == field_type_MAX) {
					out_warning(CNF_OK, "(space = %zu index = %zu) "
						    "unknown field data type: `%s'", i, j, key->type);
					return -1;
				}

				if (max_key_fieldno < key->fieldno + 1) {
					max_key_fieldno = key->fieldno + 1;
				}

				++key_part_count;
			}

			/* Check key part count. */
			if (key_part_count == 0) {
				out_warning(CNF_OK, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			index_type = STR2ENUM(index_type, index->type);

			/* check index type */
			if (index_type == index_type_MAX) {
				out_warning(CNF_OK, "(space = %zu index = %zu) "
					    "unknown index type '%s'", i, j, index->type);
				return -1;
			}

			/* First index must be unique. */
			if (j == 0 && index->unique == false) {
				out_warning(CNF_OK, "(space = %zu) space first index must be unique", i);
				return -1;
			}

			switch (index_type) {
			case HASH:
				/* check hash index */
				/* hash index must be unique */
				if (!index->unique) {
					out_warning(CNF_OK, "(space = %zu index = %zu) "
						    "hash index must be unique", i, j);
					return -1;
				}
				break;
			case TREE:
				/* extra check for tree index not needed */
				break;
			case BITSET:
				/* check bitset index */
				/* bitset index must has single-field key */
				if (key_part_count != 1) {
					out_warning(CNF_OK, "(space = %zu index = %zu) "
						    "bitset index must has a single-field key", i, j);
					return -1;
				}
				/* bitset index must not be unique */
				if (index->unique) {
					out_warning(CNF_OK, "(space = %zu index = %zu) "
						    "bitset index must be non-unique", i, j);
					return -1;
				}
				break;
			default:
				assert(false);
			}
		}

		/* Check for index field type conflicts */
		if (max_key_fieldno > 0) {
			char *types = (char *) alloca(max_key_fieldno);
			memset(types, 0, max_key_fieldno);
			for (size_t j = 0; space->index[j] != NULL; ++j) {
				auto index = space->index[j];
				for (size_t k = 0; index->key_field[k] != NULL; ++k) {
					auto key = index->key_field[k];
					if (key->fieldno == -1)
						break;

					uint32_t f = key->fieldno;
					enum field_type t = STR2ENUM(field_type, key->type);
					assert(t != field_type_MAX);
					if (types[f] != t) {
						if (types[f] == UNKNOWN) {
							types[f] = t;
						} else {
							out_warning(CNF_OK, "(space = %zu fieldno = %zu) "
								    "index field type mismatch", i, f);
							return -1;
						}
					}
				}

			}
		}
	}

	return 0;
}

