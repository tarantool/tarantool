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

const char *index_type_strs[] = { "HASH", "TREE", "BITSET", "RTREE" };

const char *rtree_index_distance_type_strs[] = { "EUCLID", "MANHATTAN" };

const struct index_opts index_opts_default = {
	/* .unique              = */ true,
	/* .dimension           = */ 2,
	/* .distance            = */ RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* .range_size          = */ 1073741824,
	/* .page_size           = */ 8192,
	/* .run_count_per_level = */ 2,
	/* .run_size_ratio      = */ 3.5,
	/* .bloom_fpr           = */ 0.05,
	/* .lsn                 = */ 0,
	/* .sql                 = */ NULL,
};

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
	OPT_DEF("sql", OPT_STRPTR, struct index_opts, sql),
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
	if (pk_def != NULL) {
		def->cmp_def = key_def_merge(key_def, pk_def);
		if (! opts->is_unique) {
			def->cmp_def->unique_part_count =
				def->cmp_def->part_count;
		} else {
			def->cmp_def->unique_part_count =
				def->key_def->part_count;
		}
	} else {
		def->cmp_def = key_def_dup(key_def);
	}
	if (def->key_def == NULL || def->cmp_def == NULL) {
		index_def_delete(def);
		return NULL;
	}
	def->type = type;
	def->space_id = space_id;
	def->iid = iid;
	def->opts = *opts;
	if (opts->sql != NULL) {
		def->opts.sql = strdup(opts->sql);
		if (def->opts.sql == NULL) {
			diag_set(OutOfMemory, strlen(opts->sql) + 1, "strdup",
				 "def->opts.sql");
			index_def_delete(def);
			return NULL;
		}
	}
	return def;
}

struct index_def *
index_def_dup(const struct index_def *def)
{
	struct index_def *dup = (struct index_def *) malloc(sizeof(*dup));
	if (dup == NULL) {
		diag_set(OutOfMemory, sizeof(*dup), "malloc",
			 "struct index_def");
		return NULL;
	}
	*dup = *def;
	dup->name = strdup(def->name);
	if (dup->name == NULL) {
		free(dup);
		diag_set(OutOfMemory, strlen(def->name) + 1, "malloc",
			 "index_def name");
		return NULL;
	}
	dup->key_def = key_def_dup(def->key_def);
	dup->cmp_def = key_def_dup(def->cmp_def);
	if (dup->key_def == NULL || dup->cmp_def == NULL) {
		index_def_delete(dup);
		return NULL;
	}
	rlist_create(&dup->link);
	dup->opts = def->opts;
	if (def->opts.sql != NULL) {
		dup->opts.sql = strdup(def->opts.sql);
		if (dup->opts.sql == NULL) {
			diag_set(OutOfMemory, strlen(def->opts.sql) + 1,
				 "strdup", "dup->opts.sql");
			index_def_delete(dup);
			return NULL;
		}
	}
	return dup;
}

/** Free a key definition. */
void
index_def_delete(struct index_def *index_def)
{
	index_opts_destroy(&index_def->opts);
	free(index_def->name);

	if (index_def->key_def) {
		TRASH(index_def->key_def);
		free(index_def->key_def);
	}

	if (index_def->cmp_def) {
		TRASH(index_def->cmp_def);
		free(index_def->cmp_def);
	}

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

bool
index_def_is_valid(struct index_def *index_def, const char *space_name)

{
	if (index_def->iid >= BOX_INDEX_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "index id too big");
		return false;
	}
	if (index_def->iid == 0 && index_def->opts.is_unique == false) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "primary key must be unique");
		return false;
	}
	if (index_def->key_def->part_count == 0) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "part count must be positive");
		return false;
	}
	if (index_def->key_def->part_count > BOX_INDEX_PART_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
			 space_name, "too many key parts");
		return false;
	}
	for (uint32_t i = 0; i < index_def->key_def->part_count; i++) {
		assert(index_def->key_def->parts[i].type < field_type_MAX);
		if (index_def->key_def->parts[i].fieldno > BOX_INDEX_FIELD_MAX) {
			diag_set(ClientError, ER_MODIFY_INDEX, index_def->name,
				 space_name, "field no is too big");
			return false;
		}
		for (uint32_t j = 0; j < i; j++) {
			/*
			 * Courtesy to a user who could have made
			 * a typo.
			 */
			if (index_def->key_def->parts[i].fieldno ==
			    index_def->key_def->parts[j].fieldno) {
				diag_set(ClientError, ER_MODIFY_INDEX,
					 index_def->name, space_name,
					 "same key part is indexed twice");
				return false;
			}
		}
	}
	return true;
}
