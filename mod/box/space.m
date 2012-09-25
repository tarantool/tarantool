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

#define BOX_SPACE_MAX 256

static struct space *spaces = NULL;

bool secondary_indexes_enabled = false;
bool primary_indexes_enabled = false;

struct space {
	Index *index[BOX_INDEX_MAX];
	/** If not set (is 0), any tuple in the
	 * space can have any number of fields (but
	 * @sa max_fieldno). If set, Each tuple
	 * must have exactly this many fields.
	 */
	int arity;

	/**
	 * The number of indexes in the space.
	 *
	 * It is equal to the number of non-nil members of the index
	 * array and defines the key_defs array size as well.
	 */
	int key_count;

	/**
	 * The descriptors for all indexes that belong to the space.
	 */
	struct key_def *key_defs;

	/**
	 * Field types of indexed fields. This is an array of size
	 * field_count. If there are gaps, i.e. fields that do not
	 * participate in any index and thus we cannot infer their
	 * type, then respective array members have value UNKNOWN.
	 * XXX: right now UNKNOWN is also set for fields which types
	 * in two indexes contradict each other.
	 */
	enum field_data_type *field_types;

	/**
	 * Max field no which participates in any of the space indexes.
	 * Each tuple in this space must have, therefore, at least
	 * field_count fields.
	 */
	int max_fieldno;

	bool enabled;
};


bool
space_number_is_valid(i32 space_no)
{
	return space_no >= 0 && space_no < BOX_SPACE_MAX;
}

int
space_max_fieldno(struct space *sp)
{
	return sp->max_fieldno;
}

enum field_data_type
space_field_type(struct space *sp, int no)
{
	return sp->field_types[no];
}

struct space *
space_create(i32 space_no, struct key_def *key, int key_count, int arity)
{
	if (space_no < 0 || space_no >= BOX_SPACE_MAX)
		panic("Can't create space %d", space_no);
	if (spaces[space_no].enabled)
		panic("Space %d is already enabled", space_no);
	struct space *space = spaces + space_no;
	space->enabled = true;
	space->arity = arity;
	space->key_defs = key;
	space->key_count = key_count;

	return space;
}


/* return space by its number */
struct space *
space_by_n(i32 n)
{
	if (n < 0)
		return NULL;
	if (n >= BOX_SPACE_MAX)
		return NULL;
	return spaces + n;
}

/** Get key_def ordinal number. */
int
key_def_n(struct space *sp, struct key_def *kp)
{
	assert(kp >= sp->key_defs && kp < (sp->key_defs + sp->key_count));
	return kp - sp->key_defs;
}

int
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

int
space_n(struct space *sp)
{
	assert(sp >= spaces && sp < (spaces + BOX_SPACE_MAX));
	return sp - spaces;
}

/* look through all enabled spaces */
int
space_foreach(int (*space_i)(struct space *sp, void *udata), void *udata) {
	unsigned i, res = 0;
	for (i = 0; i < BOX_SPACE_MAX; i++) {
		if (!spaces[i].enabled)
			continue;
		res = space_i(spaces + i, udata);
		if (res)
			break;
	}
	return res;
}

/* Get index by index no */
Index *
space_index(struct space *sp, int index_no)
{
	if (index_no >= BOX_INDEX_MAX)
		return NULL;
	if (index_no < 0)
		return NULL;
	return sp->index[index_no];
}

/* Set index by index no */
Index *
space_set_index(struct space *sp, int index_no, Index *idx)
{
	if (index_no >= BOX_INDEX_MAX)
		return NULL;
	if (index_no < 0)
		return NULL;
	sp->index[index_no] = idx;

	return idx;
}

struct space *
space_find(i32 space_no)
{
	struct space *s = space_by_n(space_no);
	if (s) {
		if (!s->enabled)
			tnt_raise(ClientError, :ER_SPACE_DISABLED, space_no);
		return s;
	}

	tnt_raise(ClientError, :ER_NO_SUCH_SPACE, space_no);
}

/** Free a key definition. */
static void
key_free(struct key_def *key_def)
{
	free(key_def->parts);
	free(key_def->cmp_order);
}

void
space_replace(struct space *sp, struct tuple *old_tuple,
	      struct tuple *new_tuple)
{
	int n = index_count(sp);
	for (int i = 0; i < n; i++) {
		Index *index = sp->index[i];
		[index replace: old_tuple :new_tuple];
	}
}

void
space_validate(struct space *sp, struct tuple *old_tuple,
	       struct tuple *new_tuple)
{
	int n = index_count(sp);

	/* Only secondary indexes are validated here. So check to see
	   if there are any.*/
	if (n <= 1) {
		return;
	}

	/* Check to see if the tuple has a sufficient number of fields. */
	if (new_tuple->field_count < sp->max_fieldno)
		tnt_raise(IllegalParams, :"tuple must have all indexed fields");

	if (sp->arity > 0 && sp->arity != new_tuple->field_count)
		tnt_raise(IllegalParams, :"tuple field count must match space cardinality");

	/* Sweep through the tuple and check the field sizes. */
	u8 *data = new_tuple->data;
	for (int f = 0; f < sp->max_fieldno; ++f) {
		/* Get the size of the current field and advance. */
		u32 len = load_varint32((void **) &data);
		data += len;

		/* Check fixed size fields (NUM and NUM64) and skip undefined
		   size fields (STRING and UNKNOWN). */
		if (sp->field_types[f] == NUM) {
			if (len != sizeof(u32))
				tnt_raise(IllegalParams, :"field must be NUM");
		} else if (sp->field_types[f] == NUM64) {
			if (len != sizeof(u64))
				tnt_raise(IllegalParams, :"field must be NUM64");
		}
	}

	/* Check key uniqueness */
	for (int i = 1; i < n; ++i) {
		Index *index = sp->index[i];
		if (index->key_def->is_unique) {
			struct tuple *tuple = [index findByTuple: new_tuple];
			if (tuple != NULL && tuple != old_tuple)
				tnt_raise(ClientError, :ER_INDEX_VIOLATION);
		}
	}
}

void
space_remove(struct space *sp, struct tuple *tuple)
{
	int n = index_count(sp);
	for (int i = 0; i < n; i++) {
		Index *index = sp->index[i];
		[index remove: tuple];
	}
}

void
space_free(void)
{
	int i;
	for (i = 0 ; i < BOX_SPACE_MAX ; i++) {
		if (!spaces[i].enabled)
			continue;

		int j;
		for (j = 0 ; j < spaces[i].key_count; j++) {
			Index *index = spaces[i].index[j];
			[index free];
			key_free(&spaces[i].key_defs[j]);
		}

		free(spaces[i].key_defs);
		free(spaces[i].field_types);
	}
}

static void
key_init(struct key_def *def, struct tarantool_cfg_space_index *cfg_index)
{
	def->max_fieldno = 0;
	def->part_count = 0;

	/* Calculate key part count and maximal field number. */
	for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
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
	memset(def->cmp_order, -1, def->max_fieldno * sizeof(u32));

	/* fill fields and compare order */
	for (int k = 0; cfg_index->key_field[k] != NULL; ++k) {
		typeof(cfg_index->key_field[k]) cfg_key = cfg_index->key_field[k];

		if (cfg_key->fieldno == -1) {
			/* last filled key reached */
			break;
		}

		/* fill keys */
		def->parts[k].fieldno = cfg_key->fieldno;
		def->parts[k].type = STR2ENUM(field_data_type, cfg_key->type);
		/* fill compare order */
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
	int i, max_fieldno;
	int key_count = space->key_count;
	struct key_def *key_defs = space->key_defs;

	/* find max max field no */
	max_fieldno = 0;
	for (i = 0; i < key_count; i++) {
		max_fieldno= MAX(max_fieldno, key_defs[i].max_fieldno);
	}

	/* alloc & init field type info */
	space->max_fieldno = max_fieldno;
	space->field_types = malloc(max_fieldno * sizeof(enum field_data_type));
	for (i = 0; i < max_fieldno; i++) {
		space->field_types[i] = UNKNOWN;
	}

	/* extract field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (int pi = 0; pi < def->part_count; pi++) {
			struct key_part *part = &def->parts[pi];
			assert(part->fieldno < max_fieldno);
			space->field_types[part->fieldno] = part->type;
		}
	}

#ifndef NDEBUG
	/* validate field type info */
	for (i = 0; i < key_count; i++) {
		struct key_def *def = &key_defs[i];
		for (int pi = 0; pi < def->part_count; pi++) {
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
	for (int i = 0; cfg.space[i] != NULL; ++i) {
		tarantool_cfg_space *cfg_space = cfg.space[i];

		if (!CNF_STRUCT_DEFINED(cfg_space) || !cfg_space->enabled)
			continue;

		assert(cfg.memcached_port == 0 || i != cfg.memcached_space);

		spaces[i].enabled = true;
		spaces[i].arity = cfg_space->cardinality;

		/*
		 * Collect key/field info. We need aggregate
		 * information on all keys before we can create
		 * indexes.
		 */
		spaces[i].key_count = 0;
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			++spaces[i].key_count;
		}

		spaces[i].key_defs = malloc(spaces[i].key_count *
					    sizeof(struct key_def));
		if (spaces[i].key_defs == NULL) {
			panic("can't allocate key def array");
		}
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			key_init(&spaces[i].key_defs[j], cfg_index);
		}
		space_init_field_types(&spaces[i]);

		/* fill space indexes */
		for (int j = 0; cfg_space->index[j] != NULL; ++j) {
			typeof(cfg_space->index[j]) cfg_index = cfg_space->index[j];
			enum index_type type = STR2ENUM(index_type, cfg_index->type);
			struct key_def *key_def = &spaces[i].key_defs[j];
			Index *index = [Index alloc: type :key_def :&spaces[i]];
			[index init: key_def :&spaces[i]];
			spaces[i].index[j] = index;
		}
		say_info("space %i successfully configured", i);
	}
}

void
space_init(void)
{
	/* Allocate and initialize space memory. */
	spaces = p0alloc(eter_pool, sizeof(struct space) * BOX_SPACE_MAX);

	/* configure regular spaces */
	space_config();
}

void
begin_build_primary_indexes(void)
{
	assert(primary_indexes_enabled == false);
	for (u32 n = 0; n < BOX_SPACE_MAX; ++n) {
		if (spaces[n].enabled == false)
			continue;

		Index *pk = spaces[n].index[0];
		[pk beginBuild];
	}
}

void
end_build_primary_indexes(void)
{
	for (u32 n = 0; n < BOX_SPACE_MAX; ++n) {
		if (spaces[n].enabled == false)
			continue;

		Index *pk = spaces[n].index[0];
		[pk endBuild];
	}
	primary_indexes_enabled = true;
}

void
build_secondary_indexes(void)
{
	assert(primary_indexes_enabled == true);
	assert(secondary_indexes_enabled == false);

	for (u32 n = 0; n < BOX_SPACE_MAX; ++n) {
		if (spaces[n].enabled == false)
			continue;
		if (spaces[n].key_count <= 1)
			continue; /* no secondary keys */

		say_info("Building secondary keys in space %" PRIu32 "...", n);

		Index *pk = spaces[n].index[0];
		for (int i = 1; i < spaces[n].key_count; i++) {
			Index *index = spaces[n].index[i];
			[index build: pk];
		}

		say_info("Space %"PRIu32": done", n);
	}

	/* enable secondary indexes now */
	secondary_indexes_enabled = true;
}

i32
check_spaces(struct tarantool_cfg *conf)
{
	/* exit if no spaces are configured */
	if (conf->space == NULL) {
		return 0;
	}

	for (size_t i = 0; conf->space[i] != NULL; ++i) {
		typeof(conf->space[i]) space = conf->space[i];

		if (!CNF_STRUCT_DEFINED(space)) {
			/* space undefined, skip it */
			continue;
		}

		if (!space->enabled) {
			/* space disabled, skip it */
			continue;
		}

		/* check space bound */
		if (i >= BOX_SPACE_MAX) {
			/* maximum space is reached */
			out_warning(0, "(space = %zu) "
				    "too many spaces (%i maximum)", i, space);
			return -1;
		}

		if (conf->memcached_port && i == conf->memcached_space) {
			out_warning(0, "Space %i is already used as "
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

		int max_key_fieldno = -1;

		/* check spaces indexes */
		for (size_t j = 0; space->index[j] != NULL; ++j) {
			typeof(space->index[j]) index = space->index[j];
			u32 key_part_count = 0;
			enum index_type index_type;

			/* check index bound */
			if (j >= BOX_INDEX_MAX) {
				/* maximum index in space reached */
				out_warning(0, "(space = %zu index = %zu) "
					    "too many indexed (%i maximum)", i, j, BOX_INDEX_MAX);
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

				/* key must has valid type */
				if (STR2ENUM(field_data_type, key->type) == field_data_type_MAX) {
					out_warning(0, "(space = %zu index = %zu) "
						    "unknown field data type: `%s'", i, j, key->type);
					return -1;
				}

				if (max_key_fieldno < key->fieldno) {
					max_key_fieldno = key->fieldno;
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
		if (max_key_fieldno >= 0) {
			char *types = alloca(max_key_fieldno + 1);
			memset(types, UNKNOWN, max_key_fieldno + 1);
			for (size_t j = 0; space->index[j] != NULL; ++j) {
				typeof(space->index[j]) index = space->index[j];
				for (size_t k = 0; index->key_field[k] != NULL; ++k) {
					typeof(index->key_field[k]) key = index->key_field[k];
					int f = key->fieldno;
					if (f == -1) {
						break;
					}
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

