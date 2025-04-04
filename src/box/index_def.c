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
	/* .covered_fields      = */ NULL,
	/* .covered_field_count = */ 0,
	/* .layout              = */ NULL,
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

/**
 * Parse index covers options given as MsgPack in `data' into `opts'. Covered
 * fields array is allocated on `region'.
 */
static int
index_opts_parse_covered_fields(const char **data, void *opts,
				struct region *region)
{
	struct index_opts *index_opts = (struct index_opts *)opts;
	if (mp_typeof(**data) != MP_ARRAY) {
		diag_set(IllegalParams, "'covers' must be array");
		return -1;
	}
	index_opts->covered_field_count = mp_decode_array(data);
	if (index_opts->covered_field_count != 0)
		index_opts->covered_fields =
			xregion_alloc_array(region,
					    typeof(*index_opts->covered_fields),
					    index_opts->covered_field_count);
	for (uint32_t i = 0; i < index_opts->covered_field_count; i++) {
		if (mp_typeof(**data) != MP_UINT) {
			diag_set(IllegalParams,
				 "'covers' elements must be unsigned");
			return -1;
		}
		uint64_t fieldno = mp_decode_uint(data);
		if (fieldno > UINT32_MAX) {
			diag_set(IllegalParams,
				 "'covers' elements must be unsigned");
			return -1;
		}
		index_opts->covered_fields[i] = fieldno;
	}
	return 0;
}

/** Parse index layout option given as MsgPack in `data' into `opts'. */
static int
index_opts_parse_layout(const char **data, void *opts, struct region *region)
{
	struct index_opts *index_opts = (struct index_opts *)opts;
	if (mp_typeof(**data) != MP_STR) {
		diag_set(IllegalParams, "'layout' must be string");
		return -1;
	}
	uint32_t len = 0;
	const char *str = mp_decode_str(data, &len);
	if (len > 0) {
		index_opts->layout = xregion_alloc(region, len + 1);
		strlcpy(index_opts->layout, str, len + 1);
	}
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
	OPT_DEF_CUSTOM("covers", index_opts_parse_covered_fields),
	OPT_DEF_CUSTOM("layout", index_opts_parse_layout),
	OPT_END,
};

/**
 * Normalize index options:
 *
 * - remove implicitly covered fields.
 * - sort covered fields in ascending order.
 *
 * The implicitly covered fields are the fields of index key and pk index key.
 *
 * The result is allocated with malloc.
 */
static void
index_opts_normalize(struct index_opts *opts, const struct key_def *cmp_def)
{
	if (opts->layout != NULL)
		opts->layout = xstrdup(opts->layout);

	if (opts->covered_field_count == 0)
		return;
	uint32_t *fields = xmalloc(sizeof(*fields) * opts->covered_field_count);
	uint32_t j = 0;
	for (uint32_t i = 0; i < opts->covered_field_count; i++) {
		if (key_def_find_by_fieldno(
				cmp_def, opts->covered_fields[i]) == NULL)
			fields[j++] = opts->covered_fields[i];
	}
	qsort(fields, j, sizeof(*fields), cmp_u32);
	opts->covered_field_count = j;
	if (opts->covered_field_count != 0) {
		opts->covered_fields = fields;
	} else {
		opts->covered_fields = NULL;
		free(fields);
	}
}

struct index_def *
index_def_new(uint32_t space_id, uint32_t iid, const char *name,
	      uint32_t name_len, const char *space_name,
	      const char *engine_name, enum index_type type,
	      const struct index_opts *opts,
	      struct key_def *key_def, struct key_def *pk_def)
{
	assert(name_len <= BOX_NAME_MAX);
	/* Use calloc to make index_def_delete() safe at all times. */
	struct index_def *def = xcalloc(1, sizeof(*def));
	def->name = xstrndup(name, name_len);
	if (space_name != NULL)
		def->space_name = xstrdup(space_name);
	memset(def->engine_name, 0, sizeof(def->engine_name));
	if (engine_name != NULL)
		strlcpy(def->engine_name, engine_name, ENGINE_NAME_MAX);
	def->key_def = key_def_dup(key_def);
	if (iid != 0) {
		assert(pk_def != NULL);
		def->pk_def = key_def_dup(pk_def);
		def->cmp_def = key_def_merge(key_def, pk_def);
		if (opts->is_unique)
			def->cmp_def->unique_part_count =
				def->key_def->part_count;
	} else {
		def->cmp_def = key_def_dup(key_def);
		def->pk_def = key_def_dup(key_def);
	}
	def->type = type;
	def->space_id = space_id;
	def->iid = iid;
	def->opts = *opts;
	index_opts_normalize(&def->opts, def->cmp_def);
	return def;
}

/** Duplicate index options. */
static void
index_opts_dup(const struct index_opts *opts, struct index_opts *dup)
{
	*dup = *opts;
	if (dup->covered_fields != NULL) {
		uint32_t *fields = dup->covered_fields;
		dup->covered_fields =
			xmalloc(dup->covered_field_count *
				sizeof(*dup->covered_fields));
		memcpy(dup->covered_fields, fields,
		       dup->covered_field_count *
		       sizeof(*dup->covered_fields));
	}
	if (dup->layout != NULL)
		dup->layout = xstrdup(dup->layout);
}

struct index_def *
index_def_dup(const struct index_def *def)
{
	struct index_def *dup = xmalloc(sizeof(*dup));
	*dup = *def;
	dup->name = xstrdup(def->name);
	if (def->space_name != NULL)
		dup->space_name = xstrdup(def->space_name);
	strlcpy(dup->engine_name, def->engine_name, ENGINE_NAME_MAX);
	dup->key_def = key_def_dup(def->key_def);
	dup->cmp_def = key_def_dup(def->cmp_def);
	dup->pk_def = key_def_dup(def->pk_def);
	rlist_create(&dup->link);
	index_opts_dup(&def->opts, &dup->opts);
	return dup;
}

void
index_def_delete(struct index_def *index_def)
{
	index_opts_destroy(&index_def->opts);
	free(index_def->name);
	free(index_def->space_name);

	if (index_def->key_def)
		key_def_delete(index_def->key_def);

	if (index_def->cmp_def)
		key_def_delete(index_def->cmp_def);

	if (index_def->pk_def)
		key_def_delete(index_def->pk_def);

	TRASH(index_def);
	free(index_def);
}

bool
index_def_is_equal(const struct index_def *def1, const struct index_def *def2)
{
	assert(def1->space_id == def2->space_id);
	if (def1->iid != def2->iid)
		return false;
	if (strcmp(def1->name, def2->name) != 0)
		return false;
	if (def1->type != def2->type)
		return false;
	if (!index_opts_is_equal(&def1->opts, &def2->opts))
		return false;
	return key_part_cmp(
			def1->key_def->parts, def1->key_def->part_count,
			def2->key_def->parts, def2->key_def->part_count) == 0;
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
