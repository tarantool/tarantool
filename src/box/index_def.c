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
#include "index_def.h"
#include "schema_def.h"
#include "identifier.h"
#include "tuple_format.h"
#include "json/json.h"
#include "fiber.h"
#include "tt_static.h"

const char *index_type_strs[] = { "HASH", "TREE", "BITSET", "RTREE" };

const char *rtree_index_distance_type_strs[] = { "EUCLID", "MANHATTAN" };

const struct index_opts index_opts_default = {
	/* .unique              = */ true,
	/* .dimension           = */ 2,
	/* .distance            = */ RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* .range_size          = */ 0,
	/* .page_size           = */ 8192,
	/* .run_count_per_level = */ 2,
	/* .run_size_ratio      = */ 3.5,
	/* .bloom_fpr           = */ 0.05,
	/* .lsn                 = */ 0,
	/* .func                = */ 0,
	/* .hint                = */ INDEX_HINT_DEFAULT,
};

/**
 * Parse index hint option from msgpack.
 * Used as callback to parse a boolean value with 'hint' key in index options.
 * Move @a data msgpack pointer to the end of msgpack value.
 * By convention @a opts must point to corresponding struct index_opts.
 * Return 0 on success or -1 on error (diag is set to IllegalParams).
 */
static int
index_opts_parse_hint(const char **data, void *opts, struct region *region)
{
	(void)region;
	struct index_opts *index_opts = (struct index_opts *)opts;
	if (mp_typeof(**data) != MP_BOOL) {
		diag_set(IllegalParams, "'hint' must be boolean");
		return -1;
	}
	bool hint = mp_decode_bool(data);
	index_opts->hint = hint ? INDEX_HINT_ON : INDEX_HINT_OFF;
	return 0;
}

const struct opt_def index_opts_reg[] = {
	OPT_DEF("unique", OPT_BOOL, struct index_opts, is_unique),
	OPT_DEF("dimension", OPT_INT64, struct index_opts, dimension),
	OPT_DEF_ENUM("distance", rtree_index_distance_type, struct index_opts,
		     distance, NULL),
	OPT_DEF("range_size", OPT_INT64, struct index_opts, range_size),
	OPT_DEF("page_size", OPT_INT64, struct index_opts, page_size),
	OPT_DEF("run_count_per_level", OPT_INT64, struct index_opts, run_count_per_level),
	OPT_DEF("run_size_ratio", OPT_FLOAT, struct index_opts, run_size_ratio),
	OPT_DEF("bloom_fpr", OPT_FLOAT, struct index_opts, bloom_fpr),
	OPT_DEF("lsn", OPT_INT64, struct index_opts, lsn),
	OPT_DEF("func", OPT_UINT32, struct index_opts, func_id),
	OPT_DEF_LEGACY("sql"),
	OPT_DEF_CUSTOM("hint", index_opts_parse_hint),
	OPT_END,
};

struct index_def *
index_def_new(uint32_t space_id, uint32_t iid, const char *name,
	      uint32_t name_len, enum index_type type,
	      const struct index_opts *opts,
	      struct key_def *key_def, struct key_def *pk_def)
{
	assert(name_len <= BOX_NAME_MAX);
	/* Use calloc to make index_def_delete() safe at all times. */
	struct index_def *def = (struct index_def *) calloc(1, sizeof(*def));
	if (def == NULL) {
		diag_set(OutOfMemory, sizeof(*def), "malloc", "struct index_def");
		return NULL;
	}
	def->name = strndup(name, name_len);
	if (def->name == NULL) {
		index_def_delete(def);
		diag_set(OutOfMemory, name_len + 1, "malloc", "index_def name");
		return NULL;
	}
	if (identifier_check(def->name, name_len)) {
		index_def_delete(def);
		return NULL;
	}
	def->key_def = key_def_dup(key_def);
	if (iid != 0) {
		assert(pk_def != NULL);
		def->pk_def = key_def_dup(pk_def);
		def->cmp_def = key_def_merge(key_def, pk_def);
		if (opts->is_unique)
			def->cmp_def->unique_part_count =
				def->key_def->part_count;
		if (def->cmp_def == NULL) {
			index_def_delete(def);
			return NULL;
		}
	} else {
		def->cmp_def = key_def_dup(key_def);
		def->pk_def = key_def_dup(key_def);
	}
	def->type = type;
	def->space_id = space_id;
	def->iid = iid;
	def->opts = *opts;
	return def;
}

struct index_def *
index_def_dup(const struct index_def *def)
{
	struct index_def *dup = xmalloc(sizeof(*dup));
	*dup = *def;
	dup->name = xstrdup(def->name);
	dup->key_def = key_def_dup(def->key_def);
	dup->cmp_def = key_def_dup(def->cmp_def);
	dup->pk_def = key_def_dup(def->pk_def);
	rlist_create(&dup->link);
	dup->opts = def->opts;
	return dup;
}

void
index_def_delete(struct index_def *index_def)
{
	index_opts_destroy(&index_def->opts);
	free(index_def->name);

	if (index_def->key_def)
		key_def_delete(index_def->key_def);

	if (index_def->cmp_def)
		key_def_delete(index_def->cmp_def);

	if (index_def->pk_def)
		key_def_delete(index_def->pk_def);

	TRASH(index_def);
	free(index_def);
}

int
index_def_cmp(const struct index_def *key1, const struct index_def *key2)
{
	assert(key1->space_id == key2->space_id);
	if (key1->iid != key2->iid)
		return key1->iid < key2->iid ? -1 : 1;
	if (strcmp(key1->name, key2->name))
		return strcmp(key1->name, key2->name);
	if (key1->type != key2->type)
		return (int) key1->type < (int) key2->type ? -1 : 1;
	if (index_opts_cmp(&key1->opts, &key2->opts))
		return index_opts_cmp(&key1->opts, &key2->opts);

	return key_part_cmp(key1->key_def->parts, key1->key_def->part_count,
			    key2->key_def->parts, key2->key_def->part_count);
}

struct key_def **
index_def_to_key_def(struct rlist *index_defs, int *size)
{
	int key_count = 0;
	struct index_def *index_def;
	rlist_foreach_entry(index_def, index_defs, link)
		key_count++;
	*size = key_count;
	if (key_count == 0)
		return NULL;
	struct key_def **keys =
		xregion_alloc_array(&fiber()->gc, typeof(keys[0]), key_count);
	key_count = 0;
	rlist_foreach_entry(index_def, index_defs, link)
		keys[key_count++] = index_def->key_def;
	return keys;
}

int
index_def_check(struct index_def *index_def, const char *space_name)
{
	if (index_def->iid >= BOX_INDEX_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "index id too big");
		return -1;
	}
	if (index_def->iid == 0 && index_def->opts.is_unique == false) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "primary key must be unique");
		return -1;
	}
	if (index_def->iid == 0 && index_def->key_def->is_multikey) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "primary key cannot be multikey");
		return -1;
	}
	if (index_def->iid == 0 && index_def->key_def->for_func_index) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			space_name, "primary key can not use a function");
		return -1;
	}
	for (uint32_t i = 0; i < index_def->key_def->part_count; i++) {
		assert(index_def->key_def->parts[i].type < field_type_MAX);
		if (index_def->key_def->parts[i].fieldno > BOX_INDEX_FIELD_MAX) {
			diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
				 space_name, "field no is too big");
			return -1;
		}
		for (uint32_t j = 0; j < i; j++) {
			/*
			 * Courtesy to a user who could have made
			 * a typo.
			 */
			struct key_part *part_a = &index_def->key_def->parts[i];
			struct key_part *part_b = &index_def->key_def->parts[j];
			if (part_a->fieldno == part_b->fieldno &&
			    json_path_cmp(part_a->path, part_a->path_len,
					  part_b->path, part_b->path_len,
					  TUPLE_INDEX_BASE) == 0) {
				diag_set(ClientError, ER_MODIFY_INDEX,
					 index_def->name, space_name,
					 "same key part is indexed twice");
				return -1;
			}
		}
	}
	return 0;
}

int
index_def_check_field_types(struct index_def *index_def, const char *space_name)
{
	struct key_def *key_def = index_def->key_def;

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		enum field_type type = key_def->parts[i].type;

		if (type == FIELD_TYPE_ANY || type == FIELD_TYPE_INTERVAL ||
		    type == FIELD_TYPE_ARRAY || type == FIELD_TYPE_MAP) {
			diag_set(ClientError, ER_MODIFY_INDEX,
				 index_def->name, space_name,
				 tt_sprintf("field type '%s' is not supported",
					    field_type_strs[type]));
			return -1;
		}
	}
	return 0;
}
