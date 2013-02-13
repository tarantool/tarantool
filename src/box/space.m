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
#include <cfg/tarantool_box_cfg.h>
#include <cfg/warning.h>
#include <tarantool.h>
#include <exception.h>
#include "tuple.h"
#include <pickle.h>
#include <palloc.h>
#include <assoc.h>

#include <box/box.h>

static struct mh_i32ptr_t *spaces;

bool secondary_indexes_enabled = false;
bool primary_indexes_enabled = false;

struct space *
space_create(u32 space_no, struct key_def *key_defs, u32 key_count, u32 arity)
{

	struct space *space = space_by_n(space_no);
	if (space)
		panic("Space %d is already exists", space_no);
	space = calloc(sizeof(struct space), 1);
	space->no = space_no;

	const struct mh_i32ptr_node_t node = { .key = space->no, .val = space };
	mh_i32ptr_put(spaces, &node, NULL, NULL, NULL);

	space->arity = arity;
	space->key_defs = key_defs;
	space->key_count = key_count;

	return space;
}


/* return space by its number */
struct space *
space_by_n(u32 n)
{
	const struct mh_i32ptr_node_t node = { .key = n };
	mh_int_t space = mh_i32ptr_get(spaces, &node, NULL, NULL);
	if (space == mh_end(spaces))
		return NULL;
	return mh_i32ptr_node(spaces, space)->val;
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
		struct space *space = mh_i32ptr_node(spaces, i)->val;
		func(space, udata);
	}
}

/** Set index by index no */
void
space_set_index(struct space *sp, u32 index_no, Index *idx)
{
	assert(index_no < BOX_INDEX_MAX);
	sp->index[index_no] = idx;
}

/** Free a key definition. */
static void
key_free(struct key_def *key_def)
{
	free(key_def->parts);
	free(key_def->cmp_order);
}

struct tuple *
space_replace(struct space *sp, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode)
{
	u32 i = 0;
	@try {
		/* Update the primary key */
		Index *pk = sp->index[0];
		assert(pk->key_def->is_unique);
		/*
		 * If old_tuple is not NULL, the index
		 * has to find and delete it, or raise an
		 * error.
		 */
		old_tuple = [pk replace: old_tuple :new_tuple :mode];

		assert(old_tuple || new_tuple);
		u32 n = index_count(sp);
		/* Update secondary keys */
		for (i = i + 1; i < n; i++) {
			Index *index = sp->index[i];
			[index replace: old_tuple :new_tuple :DUP_INSERT];
		}
		return old_tuple;
	} @catch (tnt_Exception *e) {
		/* Rollback all changes */
		for (; i > 0; i--) {
			Index *index = sp->index[i-1];
			[index replace: new_tuple: old_tuple: DUP_INSERT];
		}
		@throw;
	}
}

void
space_validate_tuple(struct space *sp, struct tuple *new_tuple)
{
	/* Check to see if the tuple has a sufficient number of fields. */
	if (new_tuple->field_count < sp->max_fieldno)
		tnt_raise(IllegalParams,
			  :"tuple must have all indexed fields");

	if (sp->arity > 0 && sp->arity != new_tuple->field_count)
		tnt_raise(IllegalParams,
			  :"tuple field count must match space cardinality");

	/* Sweep through the tuple and check the field sizes. */
	const u8 *data = new_tuple->data;
	for (u32 f = 0; f < sp->max_fieldno; ++f) {
		/* Get the size of the current field and advance. */
		u32 len = load_varint32((const void **) &data);
		data += len;
		/*
		 * Check fixed size fields (NUM and NUM64) and
		 * skip undefined size fields (STRING and UNKNOWN).
		 */
		if (sp->field_types[f] == NUM) {
			if (len != sizeof(u32))
				tnt_raise(ClientError, :ER_KEY_FIELD_TYPE,
					  "u32");
		} else if (sp->field_types[f] == NUM64) {
			if (len != sizeof(u64))
				tnt_raise(ClientError, :ER_KEY_FIELD_TYPE,
					  "u64");
		}
	}
}

void
space_free(void)
{
	mh_int_t i;

	mh_foreach(spaces, i) {
		struct space *space = mh_i32ptr_node(spaces, i)->val;
		mh_i32ptr_del(spaces, i, NULL, NULL);

		for (u32 j = 0 ; j < space->key_count; j++) {
			Index *index = space->index[j];
			[index free];
			key_free(&space->key_defs[j]);
		}

		free(space->key_defs);
		free(space->field_types);
		free(space);
	}

}

static void
key_init(struct key_def *def, struct tarantool_cfg_space_index *cfg_index)
{
	def->max_fieldno = 0;
	def->part_count = 0;

	def->type = STR2ENUM(index_type, cfg_index->type);
	if (def->type == index_type_MAX)
		panic("Wrong index type: %s", cfg_index->type);

	/* Calculate key part count and maximal field number. */
	for (u32 k = 0; cfg_index->key_field[k] != NULL; ++k) {
		typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		def->max_fieldno = MAX(def->max_fieldno, cfg_key->fieldno);
		def->part_count++;
	}

	/* init def array */
	def->parts = malloc(sizeof(struct key_part) * def->part_count);
	if (def->parts == NULL) {
		panic("can't allocate def parts array for index");
	}

	/* init compare order array */
	def->max_fieldno++;
	def->cmp_order = malloc(def->max_fieldno * sizeof(u32));
	if (def->cmp_order == NULL) {
		panic("can't allocate def cmp_order array for index");
	}
	for (u32 fieldno = 0; fieldno < def->max_fieldno; fieldno++) {
		def->cmp_order[fieldno] = BOX_FIELD_MAX;
	}

	/* fill fields and compare order */
	for (u32 k = 0; cfg_index->key_field[k] != NULL; ++k) {
		typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		/* fill keys */
		def->parts[k].fieldno = cfg_key->fieldno;
		def->parts[k].type = STR2ENUM(field_data_type, cfg_key->type);
		/* fill compare order */
		if (def->cmp_order[cfg_key->fieldno] == BOX_FIELD_MAX)
			def->cmp_order[cfg_key->fieldno] = k;
	}
	def->is_unique = cfg_index->unique;
}

/**
 * Extract all available field info from keys
 *
 * @param space		space to extract field info for
 * @param key_count	the number of keys
 * @param key_defs	key description array
 */
static void
space_init_field_types(struct space *space)
{
	u32 i, max_fieldno;
	u32 key_count = space->key_count;
	struct key_def *key_defs = space->key_defs;

	/* find max max field no */
	max_fieldno = 0;
	for (i = 0; i < key_count; i++) {
		max_fieldno= MAX(max_fieldno, key_defs[i].max_fieldno);
	}

	/* alloc & init field type info */
	space->max_fieldno = max_fieldno;
	space->field_types = calloc(max_fieldno, sizeof(enum field_data_type));

	/* extract field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (u32 pi = 0; pi < def->part_count; pi++) {
			struct key_part *part = &def->parts[pi];
			assert(part->fieldno < max_fieldno);
			space->field_types[part->fieldno] = part->type;
		}
	}

#ifndef NDEBUG
	/* validate field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (u32 pi = 0; pi < def->part_count; pi++) {
			struct key_part *part = &def->parts[pi];
			assert(space->field_types[part->fieldno] == part->type);
		}
	}
#endif
}

static void
space_config()
{
	/* exit if no spaces are configured */
	if (cfg.space == NULL) {
		return;
	}

	/* fill box spaces */
	for (u32 i = 0; cfg.space[i] != NULL; ++i) {
		tarantool_cfg_space *cfg_space = cfg.space[i];

		if (!CNF_STRUCT_DEFINED(cfg_space) || !cfg_space->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_space);

		struct space *space = space_by_n(i);
		if (space)
			panic("space %u is already exists", i);

		space = calloc(sizeof(struct space), 1);
		space->no = i;

		space->arity = (cfg_space->cardinality != -1) ?
					cfg_space->cardinality : 0;
		/*
		 * Collect key/field info. We need aggregate
		 * information on all keys before we can create
		 * indexes.
		 */
		space->key_count = 0;
		for (u32 j = 0; cfg_space->index[j] != NULL; ++j) {
			++space->key_count;
		}


		space->key_defs = malloc(space->key_count *
					    sizeof(struct key_def));
		if (space->key_defs == NULL) {
			panic("can't allocate key def array");
		}
		for (u32 j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			key_init(&space->key_defs[j], cfg_index);
		}
		space_init_field_types(space);

		/* fill space indexes */
		for (u32 j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			enum index_type type = STR2ENUM(index_type, cfg_index->type);
			struct key_def *key_def = &space->key_defs[j];
			Index *index = [[Index alloc :type :key_def :space]
					init :key_def :space];
			assert (index != NULL);
			space->index[j] = index;
		}

		const struct mh_i32ptr_node_t node =
			{ .key = space->no, .val = space };
		mh_i32ptr_put(spaces, &node, NULL, NULL, NULL);
		say_info("space %u successfully configured", i);
	}
}

void
space_init(void)
{
	spaces = mh_i32ptr_new();

	/* configure regular spaces */
	space_config();
}

void
begin_build_primary_indexes(void)
{
	assert(primary_indexes_enabled == false);

	mh_int_t i;

	mh_foreach(spaces, i) {
		struct space *space = mh_i32ptr_node(spaces, i)->val;
		Index *index = space->index[0];
		[index beginBuild];
	}
}

void
end_build_primary_indexes(void)
{
	mh_int_t i;
	mh_foreach(spaces, i) {
		struct space *space = mh_i32ptr_node(spaces, i)->val;
		Index *index = space->index[0];
		[index endBuild];
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
		struct space *space = mh_i32ptr_node(spaces, i)->val;

		if (space->key_count <= 1)
			continue; /* no secondary keys */

		say_info("Building secondary keys in space %d...", space->no);

		Index *pk = space->index[0];
		for (u32 j = 1; j < space->key_count; j++) {
			Index *index = space->index[j];
			[index build: pk];
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
		typeof(conf->space[i]) space = conf->space[i];

		if (i >= BOX_SPACE_MAX) {
			out_warning(0, "(space = %zu) invalid id, (maximum=%u)",
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
			out_warning(0, "Space %zu is already used as "
				    "memcached_space.", i);
			return -1;
		}

		/* at least one index in space must be defined
		 * */
		if (space->index == NULL) {
			out_warning(0, "(space = %zu) "
				    "at least one index must be defined", i);
			return -1;
		}

		u32 max_key_fieldno = 0;

		/* check spaces indexes */
		for (size_t j = 0; space->index[j] != NULL; ++j) {
			typeof(space->index[j]) index = space->index[j];
			u32 key_part_count = 0;
			enum index_type index_type;

			/* check index bound */
			if (j >= BOX_INDEX_MAX) {
				/* maximum index in space reached */
				out_warning(0, "(space = %zu index = %zu) "
					    "too many indexed (%u maximum)", i, j, BOX_INDEX_MAX);
				return -1;
			}

			/* at least one key in index must be defined */
			if (index->key_field == NULL) {
				out_warning(0, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			/* check unique property */
			if (index->unique == -1) {
				/* unique property undefined */
				out_warning(0, "(space = %zu index = %zu) "
					    "unique property is undefined", i, j);
			}

			for (size_t k = 0; index->key_field[k] != NULL; ++k) {
				typeof(index->key_field[k]) key = index->key_field[k];

				if (key->fieldno == -1) {
					/* last key reached */
					break;
				}

				if (key->fieldno >= BOX_FIELD_MAX) {
					/* maximum index in space reached */
					out_warning(0, "(space = %zu index = %zu) "
						    "invalid field number (%u maximum)",
						    i, j, BOX_FIELD_MAX);
					return -1;
				}

				/* key must has valid type */
				if (STR2ENUM(field_data_type, key->type) == field_data_type_MAX) {
					out_warning(0, "(space = %zu index = %zu) "
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
				out_warning(0, "(space = %zu index = %zu) "
					    "at least one field must be defined", i, j);
				return -1;
			}

			index_type = STR2ENUM(index_type, index->type);

			/* check index type */
			if (index_type == index_type_MAX) {
				out_warning(0, "(space = %zu index = %zu) "
					    "unknown index type '%s'", i, j, index->type);
				return -1;
			}

			/* First index must be unique. */
			if (j == 0 && index->unique == false) {
				out_warning(0, "(space = %zu) space first index must be unique", i);
				return -1;
			}

			switch (index_type) {
			case HASH:
				/* check hash index */
				/* hash index must has single-field key */
				if (key_part_count != 1) {
					out_warning(0, "(space = %zu index = %zu) "
					            "hash index must has a single-field key", i, j);
					return -1;
				}
				/* hash index must be unique */
				if (!index->unique) {
					out_warning(0, "(space = %zu index = %zu) "
					            "hash index must be unique", i, j);
					return -1;
				}
				break;
			case TREE:
				/* extra check for tree index not needed */
				break;
			default:
				assert(false);
			}
		}

		/* Check for index field type conflicts */
		if (max_key_fieldno > 0) {
			char *types = alloca(max_key_fieldno);
			memset(types, 0, max_key_fieldno);
			for (size_t j = 0; space->index[j] != NULL; ++j) {
				typeof(space->index[j]) index = space->index[j];
				for (size_t k = 0; index->key_field[k] != NULL; ++k) {
					typeof(index->key_field[k]) key = index->key_field[k];
					if (key->fieldno == -1)
						break;

					u32 f = key->fieldno;
					enum field_data_type t = STR2ENUM(field_data_type, key->type);
					assert(t != field_data_type_MAX);
					if (types[f] != t) {
						if (types[f] == UNKNOWN) {
							types[f] = t;
						} else {
							out_warning(0, "(space = %zu fieldno = %zu) "
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

