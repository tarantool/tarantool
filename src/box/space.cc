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
#include "lua/init.h"
#include "box_lua_space.h"

static struct mh_i32ptr_t *spaces;

struct space *
space_new(struct space_def *space_def, struct key_def *key_defs,
	  uint32_t key_count)
{
	struct space *space = space_by_id(space_def->id);
	if (space)
		tnt_raise(LoggedError, ER_SPACE_EXISTS, space_def->id);

	uint32_t index_id_max = 0;
	for (uint32_t j = 0; j < key_count; ++j)
		index_id_max = MAX(index_id_max, key_defs[j].id);

	size_t sz = sizeof(struct space) +
		(key_count + index_id_max + 1) * sizeof(Index *);
	space = (struct space *) calloc(1, sz);

	space->index_map = (Index **)((char *) space + sizeof(*space) +
				      key_count * sizeof(Index *));
	space->def = *space_def;
	space->format = tuple_format_new(key_defs, key_count);
	space->index_id_max = index_id_max;
	/* fill space indexes */
	for (uint32_t j = 0; j < key_count; ++j) {
		struct key_def *key_def = &key_defs[j];
		Index *index = Index::factory(key_def);
		if (index == NULL) {
			tnt_raise(LoggedError, ER_MEMORY_ISSUE,
				  "class Index", "malloc");
		}
		space->index_map[key_def->id] = index;
	}
	/*
	 * Initialize the primary key, but do not the secondary
	 * keys - they are built by space_build_secondary_keys().
	 */
	space->index[space->index_count++] = space->index_map[0];

	const struct mh_i32ptr_node_t node = { space_id(space), space };
	mh_i32ptr_put(spaces, &node, NULL, NULL);
	/*
	 * Must be after the space is put into the hash, since
	 * box.bless_space() uses hash look up to find the space
	 * and create userdata objects for space objects.
	 */
	box_lua_space_new(tarantool_L, space);
	return space;
}

void
space_build_secondary_keys(struct space *space)
{
	if (space->index_id_max == 0)
		return; /* no secondary keys */

	say_info("Building secondary keys in space %d...",
		 space_id(space));

	Index *pk = space->index_map[0];

	for (uint32_t j = 1; j <= space->index_id_max; j++) {
		Index *index = space->index_map[j];
		if (index) {
			index_build(index, pk);
			space->index[space->index_count++] = index;
		}
	}

	say_info("Space %d: done", space_id(space));
}

static void
space_delete(struct space *space)
{
	if (tarantool_L)
		box_lua_space_delete(tarantool_L, space);
	mh_int_t k = mh_i32ptr_find(spaces, space_id(space), NULL);
	assert(k != mh_end(spaces));
	mh_i32ptr_del(spaces, k, NULL);
	for (uint32_t j = 0 ; j <= space->index_id_max; j++)
		delete space->index_map[j];
	free(space);
}

/* return space by its number */
struct space *
space_by_id(uint32_t id)
{
	mh_int_t space = mh_i32ptr_find(spaces, id, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *) mh_i32ptr_node(spaces, space)->val;
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
space_replace(struct space *space, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode)
{
	uint32_t i = 0;
	try {
		/* Update the primary key */
		Index *pk = space->index[0];
		assert(pk->key_def.is_unique);
		/*
		 * If old_tuple is not NULL, the index
		 * has to find and delete it, or raise an
		 * error.
		 */
		old_tuple = pk->replace(old_tuple, new_tuple, mode);

		assert(old_tuple || new_tuple);
		/*
		 * Update secondary keys. When loading data from
		 * the WAL secondary keys are not enabled
		 * (index_count is 1).
		 */
		for (i++; i < space->index_count; i++) {
			Index *index = space->index[i];
			index->replace(old_tuple, new_tuple, DUP_INSERT);
		}
		return old_tuple;
	} catch (const Exception &e) {
		/* Rollback all changes */
		for (; i > 0; i--) {
			Index *index = space->index[i-1];
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
	if (sp->def.arity > 0 && sp->def.arity != new_tuple->field_count)
		tnt_raise(IllegalParams,
			  "tuple field count must match space arity");

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

void
key_def_create_from_cfg(struct key_def *def, uint32_t id,
	       struct tarantool_cfg_space_index *cfg_index)
{
	uint32_t part_count = 0;
	enum index_type type = STR2ENUM(index_type, cfg_index->type);

	if (type == index_type_MAX)
		tnt_raise(LoggedError, ER_INDEX_TYPE, cfg_index->type);

	/* Find out key part count. */
	for (uint32_t k = 0; cfg_index->key_field[k] != NULL; ++k) {
		auto cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}
		part_count++;
	}

	key_def_create(def, id, type, cfg_index->unique, part_count);

	for (uint32_t k = 0; k < part_count; k++) {
		auto cfg_key = cfg_index->key_field[k];

		key_def_set_part(def, k, cfg_key->fieldno,
				 STR2ENUM(field_type, cfg_key->type));
	}
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
		struct space_def space_def;
		space_def.id = i;
		tarantool_cfg_space *cfg_space = cfg.space[i];

		if (!CNF_STRUCT_DEFINED(cfg_space) || !cfg_space->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_space);

		space_def.arity = (cfg_space->arity != -1 ?
				   cfg_space->arity : 0);
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
			key_def_create_from_cfg(&key_defs[j], j, cfg_index);
		}
		(void) space_new(&space_def, key_defs, key_count);
		free(key_defs);

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
}

void
build_secondary_indexes(void)
{
	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;

		space_build_secondary_keys(space);
	}
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

