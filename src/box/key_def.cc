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

#include <stdlib.h>
#include <stdio.h>

#include <msgpuck/msgpuck.h>

#include "trivia/util.h"
#include "scoped_guard.h"

#include "tuple_compare.h"
#include "tuple_hash.h"
#include "column_mask.h"

const char *field_type_strs[] = {
	/* [FIELD_TYPE_ANY]      = */ "any",
	/* [FIELD_TYPE_UNSIGNED] = */ "unsigned",
	/* [FIELD_TYPE_STRING]   = */ "string",
	/* [FIELD_TYPE_ARRAY]    = */ "array",
	/* [FIELD_TYPE_NUMBER]   = */ "number",
	/* [FIELD_TYPE_INTEGER]  = */ "integer",
	/* [FIELD_TYPE_SCALAR]   = */ "scalar",
};

enum field_type
field_type_by_name(const char *name)
{
	enum field_type field_type = STR2ENUM(field_type, name);
	/*
	 * FIELD_TYPE_ANY can't be used as type of indexed field,
	 * because it is internal type used only for filling
	 * struct tuple_format.fields array.
	 */
	if (field_type != field_type_MAX && field_type != FIELD_TYPE_ANY)
		return field_type;
	/* 'num' and 'str' in _index are deprecated since Tarantool 1.7 */
	if (strcasecmp(name, "num") == 0)
		return FIELD_TYPE_UNSIGNED;
	else if (strcasecmp(name, "str") == 0)
		return FIELD_TYPE_STRING;
	return field_type_MAX;
}

const char *mp_type_strs[] = {
	/* .MP_NIL    = */ "nil",
	/* .MP_UINT   = */ "unsigned",
	/* .MP_INT    = */ "integer",
	/* .MP_STR    = */ "string",
	/* .MP_BIN    = */ "blob",
	/* .MP_ARRAY  = */ "array",
	/* .MP_MAP    = */ "map",
	/* .MP_BOOL   = */ "boolean",
	/* .MP_FLOAT  = */ "float",
	/* .MP_DOUBLE = */ "double",
	/* .MP_EXT    = */ "extension",
};

const char *index_type_strs[] = { "HASH", "TREE", "BITSET", "RTREE" };

const char *rtree_index_distance_type_strs[] = { "EUCLID", "MANHATTAN" };

const char *func_language_strs[] = {"LUA", "C"};

const uint32_t key_mp_type[] = {
	/* [FIELD_TYPE_ANY]      =  */ UINT32_MAX,
	/* [FIELD_TYPE_UNSIGNED] =  */ 1U << MP_UINT,
	/* [FIELD_TYPE_STRING]   =  */ 1U << MP_STR,
	/* [FIELD_TYPE_ARRAY]    =  */ 1U << MP_ARRAY,
	/* [FIELD_TYPE_NUMBER]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE),
	/* [FIELD_TYPE_INTEGER]  =  */ (1U << MP_UINT) | (1U << MP_INT),
	/* [FIELD_TYPE_SCALAR]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE) | (1U << MP_STR) |
		(1U << MP_BIN) | (1U << MP_BOOL),
};

const char *opt_type_strs[] = {
	/* [OPT_BOOL]	= */ "boolean",
	/* [OPT_INT]	= */ "integer",
	/* [OPT_FLOAT]	= */ "float",
	/* [OPT_STR]	= */ "string",
};

const struct index_opts index_opts_default = {
	/* .unique              = */ true,
	/* .dimension           = */ 2,
	/* .distancebuf         = */ { '\0' },
	/* .distance            = */ RTREE_INDEX_DISTANCE_TYPE_EUCLID,
	/* .range_size          = */ 0,
	/* .page_size           = */ 0,
	/* .run_count_per_level = */ 2,
	/* .run_size_ratio      = */ 3.5,
	/* .bloom_fpr           = */ 0.05,
	/* .lsn                 = */ 0,
};

const struct opt_def index_opts_reg[] = {
	OPT_DEF("unique", OPT_BOOL, struct index_opts, is_unique),
	OPT_DEF("dimension", OPT_INT, struct index_opts, dimension),
	OPT_DEF("distance", OPT_STR, struct index_opts, distancebuf),
	OPT_DEF("range_size", OPT_INT, struct index_opts, range_size),
	OPT_DEF("page_size", OPT_INT, struct index_opts, page_size),
	OPT_DEF("run_count_per_level", OPT_INT, struct index_opts, run_count_per_level),
	OPT_DEF("run_size_ratio", OPT_FLOAT, struct index_opts, run_size_ratio),
	OPT_DEF("bloom_fpr", OPT_FLOAT, struct index_opts, bloom_fpr),
	OPT_DEF("lsn", OPT_INT, struct index_opts, lsn),
	{ NULL, opt_type_MAX, 0, 0 },
};

static const char *object_type_strs[] = {
	"unknown", "universe", "space", "function", "user", "role" };

enum schema_object_type
schema_object_type(const char *name)
{
	/**
	 * There may be other places in which we look object type by
	 * name, and they are case-sensitive, so be case-sensitive
	 * here too.
	 */
	int n_strs = sizeof(object_type_strs)/sizeof(*object_type_strs);
	int index = strindex(object_type_strs, name, n_strs);
	return (enum schema_object_type) (index == n_strs ? 0 : index);
}

const char *
schema_object_name(enum schema_object_type type)
{
	return object_type_strs[type];
}

struct key_def *
key_def_dup(const struct key_def *src)
{
	size_t sz = key_def_sizeof(src->part_count);
	struct key_def *res = (struct key_def *)malloc(sz);
	if (res == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "res");
		return NULL;
	}
	memcpy(res, src, sz);
	return res;
}

static void
key_def_set_cmp(struct key_def *def)
{
	def->tuple_compare = tuple_compare_create(def);
	def->tuple_compare_with_key = tuple_compare_with_key_create(def);
	tuple_hash_func_set(def);
	tuple_extract_key_set(def);
}

static size_t
key_def_size(uint32_t part_count)
{
	return sizeof(struct key_def) + sizeof(struct key_part) * part_count;
}

box_key_def_t *
box_key_def_new(uint32_t *fields, uint32_t *types, uint32_t part_count)
{
	size_t sz = key_def_size(part_count);
	box_key_def_t *key_def = (box_key_def_t *)malloc(sz);
	if (key_def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct key_def");
		return NULL;
	}
	key_def->part_count = part_count;

	for (uint32_t item = 0; item < part_count; ++item)
		key_def_set_part(key_def, item, fields[item],
				 (enum field_type)types[item]);
	return key_def;
}

void
box_key_def_delete(box_key_def_t *key_def)
{
	free(key_def);
}

struct index_def *
index_def_new(uint32_t space_id, const char *space_name,
	      uint32_t iid, const char *name, uint32_t name_length,
	      enum index_type type, const struct index_opts *opts,
	      uint32_t part_count)
{
	if (name_length > BOX_NAME_MAX) {
		diag_set(ClientError, ER_MODIFY_INDEX,
			 tt_cstr(name, name_length), space_name,
			 "index name is too long");
		error_log(diag_last_error(diag_get()));
		return NULL;
	}
	size_t sz = index_def_sizeof(part_count, name_length);
	/*
	 * Use calloc for nullifying all struct index_def attributes including
	 * comparator pointers.
	 */
	struct index_def *def = (struct index_def *) calloc(1, sz);
	if (def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct index_def");
		return NULL;
	}
	def->name = (char *)def + sz - name_length - 1;
	strncpy(def->name, name, name_length);
	if (!identifier_is_valid(def->name)) {
		diag_set(ClientError, ER_IDENTIFIER, def->name);
		free(def);
		return NULL;
	}
	def->type = type;
	def->space_id = space_id;
	def->iid = iid;
	def->opts = *opts;
	def->key_def.part_count = part_count;
	return def;
}

struct index_def *
index_def_dup(const struct index_def *def)
{
	uint32_t name_length = strlen(def->name);
	size_t sz = index_def_sizeof(def->key_def.part_count, name_length);
	struct index_def *dup = (struct index_def *) malloc(sz);
	if (dup == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct index_def");
		return NULL;
	}
	memcpy(dup, def, sz);
	rlist_create(&dup->link);
	dup->name = (char *)dup + sz - name_length - 1;
	return dup;
}

/** Free a key definition. */
void
index_def_delete(struct index_def *index_def)
{
	free(index_def);
}

bool
index_def_change_requires_rebuild(struct index_def *old_index_def,
				  struct index_def *new_index_def)
{
	if (old_index_def->iid != new_index_def->iid ||
	    old_index_def->type != new_index_def->type ||
	    old_index_def->opts.is_unique != new_index_def->opts.is_unique ||
	    key_part_cmp(old_index_def->key_def.parts,
			 old_index_def->key_def.part_count,
			 new_index_def->key_def.parts,
			 new_index_def->key_def.part_count) != 0) {
		return true;
	}
	if (old_index_def->type == RTREE) {
		if (old_index_def->opts.dimension != new_index_def->opts.dimension
		    || old_index_def->opts.distance != new_index_def->opts.distance)
			return true;
	}
	return false;
}

int
key_part_cmp(const struct key_part *parts1, uint32_t part_count1,
	     const struct key_part *parts2, uint32_t part_count2)
{
	const struct key_part *part1 = parts1;
	const struct key_part *part2 = parts2;
	uint32_t part_count = MIN(part_count1, part_count2);
	const struct key_part *end = parts1 + part_count;
	for (; part1 != end; part1++, part2++) {
		if (part1->fieldno != part2->fieldno)
			return part1->fieldno < part2->fieldno ? -1 : 1;
		if ((int) part1->type != (int) part2->type)
			return (int) part1->type < (int) part2->type ? -1 : 1;
	}
	return part_count1 < part_count2 ? -1 : part_count1 > part_count2;
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

	return key_part_cmp(key1->key_def.parts, key1->key_def.part_count,
			    key2->key_def.parts, key2->key_def.part_count);
}

void
index_def_check(struct index_def *index_def, const char *space_name)

{
	if (index_def->iid >= BOX_INDEX_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  index_def->name, space_name,
			  "index id too big");
	}
	if (index_def->iid == 0 && index_def->opts.is_unique == false) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  index_def->name,
			  space_name,
			  "primary key must be unique");
	}
	if (index_def->key_def.part_count == 0) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  index_def->name,
			  space_name,
			  "part count must be positive");
	}
	if (index_def->key_def.part_count > BOX_INDEX_PART_MAX) {
		tnt_raise(ClientError, ER_MODIFY_INDEX,
			  index_def->name,
			  space_name,
			  "too many key parts");
	}
	for (uint32_t i = 0; i < index_def->key_def.part_count; i++) {
		assert(index_def->key_def.parts[i].type > FIELD_TYPE_ANY &&
		       index_def->key_def.parts[i].type < field_type_MAX);
		if (index_def->key_def.parts[i].fieldno > BOX_INDEX_FIELD_MAX) {
			tnt_raise(ClientError, ER_MODIFY_INDEX,
				  index_def->name,
				  space_name,
				  "field no is too big");
		}
		for (uint32_t j = 0; j < i; j++) {
			/*
			 * Courtesy to a user who could have made
			 * a typo.
			 */
			if (index_def->key_def.parts[i].fieldno ==
			    index_def->key_def.parts[j].fieldno) {
				tnt_raise(ClientError, ER_MODIFY_INDEX,
					  index_def->name,
					  space_name,
					  "same key part is indexed twice");
			}
		}
	}
}

void
key_def_set_part(struct key_def *def, uint32_t part_no,
		 uint32_t fieldno, enum field_type type)
{
	assert(part_no < def->part_count);
	assert(type > FIELD_TYPE_ANY && type < field_type_MAX);
	def->parts[part_no].fieldno = fieldno;
	def->parts[part_no].type = type;
	column_mask_set_fieldno(&def->column_mask, fieldno);
	/**
	 * When all parts are set, initialize the tuple
	 * comparator function.
	 */
	/* Last part is set, initialize the comparators. */
	bool all_parts_set = true;
	for (uint32_t i = 0; i < def->part_count; i++) {
		if (def->parts[i].type == FIELD_TYPE_ANY)
			all_parts_set = false;
	}
	if (all_parts_set)
		key_def_set_cmp(def);
}

int
key_def_snprint(char *buf, int size, const struct key_def *key_def)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "[");
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		const struct key_part *part = &key_def->parts[i];
		assert(part->type < field_type_MAX);
		SNPRINT(total, snprintf, buf, size, "%d, '%s'",
			(int)part->fieldno, field_type_strs[part->type]);
		if (i < key_def->part_count - 1)
			SNPRINT(total, snprintf, buf, size, ", ");
	}
	SNPRINT(total, snprintf, buf, size, "]");
	return total;
}

size_t
key_def_sizeof_parts(const struct key_def *key_def)
{
	size_t size = 0;
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		const struct key_part *part = &key_def->parts[i];
		size += mp_sizeof_array(2);
		size += mp_sizeof_uint(part->fieldno);
		assert(part->type < field_type_MAX);
		size += mp_sizeof_str(strlen(field_type_strs[part->type]));
	}
	return size;
}

char *
key_def_encode_parts(char *data, const struct key_def *key_def)
{
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		const struct key_part *part = &key_def->parts[i];
		data = mp_encode_array(data, 2);
		data = mp_encode_uint(data, part->fieldno);
		assert(part->type < field_type_MAX);
		const char *type_str = field_type_strs[part->type];
		data = mp_encode_str(data, type_str, strlen(type_str));
	}
	return data;
}

int
key_def_decode_parts(struct key_def *key_def, const char **data)
{
	char buf[BOX_NAME_MAX];
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		if (mp_typeof(**data) != MP_ARRAY) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "expected an array");
			return -1;
		}
		uint32_t item_count = mp_decode_array(data);
		if (item_count < 1) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "expected a non-empty array");
			return -1;
		}
		if (item_count < 2) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "a field type is missing");
			return -1;
		}
		if (mp_typeof(**data) != MP_UINT) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "field id must be an integer");
			return -1;
		}
		uint32_t field_no = (uint32_t) mp_decode_uint(data);
		if (mp_typeof(**data) != MP_STR) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "field type must be a string");
			return -1;
		}
		uint32_t len;
		const char *str = mp_decode_str(data, &len);
		for (uint32_t j = 2; j < item_count; j++)
			mp_next(data);
		snprintf(buf, sizeof(buf), "%.*s", len, str);
		enum field_type field_type = field_type_by_name(buf);
		if (field_type == field_type_MAX) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "unknown field type");
			return -1;
		}
		key_def_set_part(key_def, i, field_no, field_type);
	}
	return 0;
}

int
key_def_decode_parts_165(struct key_def *key_def, const char **data)
{
	char buf[BOX_NAME_MAX];
	for (uint32_t i = 0; i < key_def->part_count; i++) {
		uint32_t field_no = (uint32_t) mp_decode_uint(data);
		uint32_t len;
		const char *str = mp_decode_str(data, &len);
		snprintf(buf, sizeof(buf), "%.*s", len, str);
		enum field_type field_type = field_type_by_name(buf);
		if (field_type == field_type_MAX) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "unknown field type");
			return -1;
		}
		key_def_set_part(key_def, i, field_no, field_type);
	}
	return 0;
}

const struct key_part *
key_def_find(const struct key_def *key_def, uint32_t fieldno)
{
	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + key_def->part_count;
	for (; part != end; part++) {
		if (part->fieldno == fieldno)
			return part;
	}
	return NULL;
}

struct key_def *
key_def_merge(const struct key_def *first, const struct key_def *second)
{
	uint32_t new_part_count = first->part_count + second->part_count;
	/*
	 * Find and remove part duplicates, i.e. parts counted
	 * twice since they are present in both key defs.
	 */
	const struct key_part *part = second->parts;
	const struct key_part *end = part + second->part_count;
	for (; part != end; part++) {
		if (key_def_find(first, part->fieldno))
			--new_part_count;
	}

	struct key_def *new_def;
	new_def =  (struct key_def *)calloc(1, key_def_sizeof(new_part_count));
	if (new_def == NULL) {
		diag_set(OutOfMemory, key_def_sizeof(new_part_count), "malloc",
			 "new_def");
		return NULL;
	}
	new_def->part_count = new_part_count;
	/* Write position in the new key def. */
	uint32_t pos = 0;
	/* Append first key def's parts to the new index_def. */
	part = first->parts;
	end = part + first->part_count;
	for (; part != end; part++)
	     key_def_set_part(new_def, pos++, part->fieldno, part->type);

	/* Set-append second key def's part to the new key def. */
	part = second->parts;
	end = part + second->part_count;
	for (; part != end; part++) {
		if (key_def_find(first, part->fieldno))
			continue;
		key_def_set_part(new_def, pos++, part->fieldno, part->type);
	}
	return new_def;
}

int
key_validate_parts(struct key_def *key_def, const char *key,
		   uint32_t part_count)
{
	for (uint32_t part = 0; part < part_count; part++) {
		enum mp_type mp_type = mp_typeof(*key);
		mp_next(&key);

		if (key_mp_type_validate(key_def->parts[part].type, mp_type,
					 ER_KEY_PART_TYPE, part))
			return -1;
	}
	return 0;
}

const struct space_opts space_opts_default = {
	/* .temporary = */ false,
};

const struct opt_def space_opts_reg[] = {
	OPT_DEF("temporary", OPT_BOOL, struct space_opts, temporary),
	{ NULL, opt_type_MAX, 0, 0 }
};

void
space_def_check(struct space_def *def, uint32_t namelen, uint32_t engine_namelen,
                int32_t errcode)
{
	if (def->id > BOX_SPACE_MAX) {
		tnt_raise(ClientError, errcode,
			  def->name,
			  "space id is too big");
	}
	if (namelen >= sizeof(def->name)) {
		tnt_raise(ClientError, errcode,
			  def->name,
			  "space name is too long");
	}
	identifier_check(def->name);
	if (engine_namelen >= sizeof(def->engine_name)) {
		tnt_raise(ClientError, errcode,
			  def->name,
			  "space engine name is too long");
	}
	identifier_check(def->engine_name);
}

bool
identifier_is_valid(const char *str)
{
	mbstate_t state;
	memset(&state, 0, sizeof(state));
	wchar_t w;
	ssize_t len = mbrtowc(&w, str, MB_CUR_MAX, &state);
	if (len <= 0)
		return false; /* invalid character or zero-length string */
	if (!iswalpha(w) && w != L'_')
		return false; /* fail to match [a-zA-Z_] */

	while ((len = mbrtowc(&w, str, MB_CUR_MAX, &state)) > 0) {
		if (!iswalnum(w) && w != L'_')
			return false; /* fail to match [a-zA-Z0-9_]* */
		str += len;
	}

	if (len < 0)
		return false; /* invalid character  */

	return true;
}

void
identifier_check(const char *str)
{
	if (! identifier_is_valid(str))
		tnt_raise(ClientError, ER_IDENTIFIER, str);
}

