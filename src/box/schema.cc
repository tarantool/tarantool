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
#include "schema.h"
#include "space.h"
#include "assoc.h"
#include "lua/init.h"
#include "box_lua_space.h"
#include "key_def.h"
extern "C" {
#include <cfg/warning.h>
#include <cfg/tarantool_box_cfg.h>
} /* extern "C" */
/**
 * @module Data Dictionary
 *
 * The data dictionary is responsible for storage and caching
 * of system metadata, such as information about existing
 * spaces, indexes, tuple formats.
 *
 * struct space is an in-memory instance representing a single
 * space with its metadata, space data, and methods to manage
 * it.
 */

/** All existing spaces. */
static struct mh_i32ptr_t *spaces;

static void
space_config();

/** Return space by its number */
struct space *
space_by_id(uint32_t id)
{
	mh_int_t space = mh_i32ptr_find(spaces, id, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return (struct space *) mh_i32ptr_node(spaces, space)->val;
}

/**
 * Visit all spaces and apply 'func'.
 */
void
space_foreach(void (*func)(struct space *sp, void *udata), void *udata)
{
	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		func(space, udata);
	}
}

/** Delete a space from the space cache and Lua. */
struct space *
space_cache_delete(uint32_t id)
{
	if (tarantool_L)
		box_lua_space_delete(tarantool_L, id);
	mh_int_t k = mh_i32ptr_find(spaces, id, NULL);
	assert(k != mh_end(spaces));
	struct space *space = (struct space *)mh_i32ptr_node(spaces, k)->val;
	mh_i32ptr_del(spaces, k, NULL);
	return space;
}

/**
 * Update the space in the space cache and in Lua. Returns
 * the old space instance, if any, or NULL if it's a new space.
 */
struct space *
space_cache_replace(struct space *space)
{
	const struct mh_i32ptr_node_t node = { space_id(space), space };
	struct mh_i32ptr_node_t old, *p_old = &old;
	mh_int_t k = mh_i32ptr_put(spaces, &node, &p_old, NULL);
	if (k == mh_end(spaces)) {
		panic_syserror("Out of memory for the data "
			       "dictionary cache.");
	}
	/*
	 * Must be after the space is put into the hash, since
	 * box.schema.space.bless() uses hash look up to find the
	 * space and create userdata objects for space objects.
	 */
	box_lua_space_new(tarantool_L, space);
	return p_old ? (struct space *) p_old->val : NULL;
}

static void
do_one_recover_step(struct space *space, void * /* param */)
{
	if (space_index(space, 0))
		space->engine.recover(space);
	else
		space->engine = engine_no_keys;
}

/**
 * Initialize a prototype for the two mandatory data
 * dictionary spaces and create a cache entry for them.
 * When restoring data from the snapshot these spaces
 * will get altered automatically to their actual format.
 */
void
schema_init()
{
	/* Initialize the space cache. */
	spaces = mh_i32ptr_new();
	space_config();
	space_foreach(do_one_recover_step, NULL);
}

void
space_end_recover_snapshot()
{
	/*
	 * For all new spaces created from now on, when the
	 * PRIMARY key is added, enable it right away.
	 */
	engine_no_keys.recover = space_build_primary_key;
	space_foreach(do_one_recover_step, NULL);
}

void
space_end_recover()
{
	/*
	 * For all new spaces created after recovery is complete,
	 * when the primary key is added, enable all keys.
	 */
	engine_no_keys.recover = space_build_all_keys;
	space_foreach(do_one_recover_step, NULL);
}

void
schema_free(void)
{
	while (mh_size(spaces) > 0) {
		mh_int_t i = mh_first(spaces);

		struct space *space = (struct space *)
				mh_i32ptr_node(spaces, i)->val;
		space_cache_delete(space_id(space));
		space_delete(space);
	}
	mh_i32ptr_delete(spaces);
}

struct key_def *
key_def_new_from_cfg(uint32_t id,
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

	struct key_def *key= key_def_new(id, type, cfg_index->unique,
					 part_count);

	for (uint32_t k = 0; k < part_count; k++) {
		auto cfg_key = cfg_index->key_field[k];

		key_def_set_part(key, k, cfg_key->fieldno,
				 STR2ENUM(field_type, cfg_key->type));
	}
	return key;
}

static void
space_config()
{
	extern tarantool_cfg cfg;
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

		struct rlist key_defs;
		rlist_create(&key_defs);
		struct key_def *key;

		for (uint32_t j = 0; cfg_space->index[j] != NULL; ++j) {
			auto cfg_index = cfg_space->index[j];
			key = key_def_new_from_cfg(j, cfg_index);
			key_list_add_key(&key_defs, key);
		}
		space_cache_replace(space_new(&space_def, &key_defs));

		say_info("space %i successfully configured", i);
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

