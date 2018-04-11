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
#include "tuple_compare.h"
#include "tuple_extract_key.h"
#include "tuple_hash.h"
#include "column_mask.h"
#include "schema_def.h"
#include "coll_cache.h"

static const struct key_part_def key_part_def_default = {
	0,
	field_type_MAX,
	COLL_NONE,
	false,
	ON_CONFLICT_ACTION_ABORT
};

static int64_t
part_type_by_name_wrapper(const char *str, uint32_t len)
{
	return field_type_by_name(str, len);
}

#define PART_OPT_TYPE		 "type"
#define PART_OPT_FIELD		 "field"
#define PART_OPT_COLLATION	 "collation"
#define PART_OPT_NULLABILITY	 "is_nullable"
#define PART_OPT_NULLABLE_ACTION "nullable_action"

const struct opt_def part_def_reg[] = {
	OPT_DEF_ENUM(PART_OPT_TYPE, field_type, struct key_part_def, type,
		     part_type_by_name_wrapper),
	OPT_DEF(PART_OPT_FIELD, OPT_UINT32, struct key_part_def, fieldno),
	OPT_DEF(PART_OPT_COLLATION, OPT_UINT32, struct key_part_def, coll_id),
	OPT_DEF(PART_OPT_NULLABILITY, OPT_BOOL, struct key_part_def,
		is_nullable),
	OPT_DEF_ENUM(PART_OPT_NULLABLE_ACTION, on_conflict_action,
		     struct key_part_def, nullable_action, NULL),
	OPT_END,
};

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

const uint32_t key_mp_type[] = {
	/* [FIELD_TYPE_ANY]      =  */ UINT32_MAX,
	/* [FIELD_TYPE_UNSIGNED] =  */ 1U << MP_UINT,
	/* [FIELD_TYPE_STRING]   =  */ 1U << MP_STR,
	/* [FIELD_TYPE_NUMBER]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE),
	/* [FIELD_TYPE_INTEGER]  =  */ (1U << MP_UINT) | (1U << MP_INT),
	/* [FIELD_TYPE_BOOLEAN]  =  */ 1U << MP_BOOL,
	/* [FIELD_TYPE_SCALAR]   =  */ (1U << MP_UINT) | (1U << MP_INT) |
		(1U << MP_FLOAT) | (1U << MP_DOUBLE) | (1U << MP_STR) |
		(1U << MP_BIN) | (1U << MP_BOOL),
	/* [FIELD_TYPE_ARRAY]    =  */ 1U << MP_ARRAY,
	/* [FIELD_TYPE_MAP]      =  */ (1U << MP_MAP),
};

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

void
key_def_swap(struct key_def *old_def, struct key_def *new_def)
{
	assert(old_def->part_count == new_def->part_count);
	for (uint32_t i = 0; i < new_def->part_count; i++)
		SWAP(old_def->parts[i], new_def->parts[i]);
	SWAP(*old_def, *new_def);
}

void
key_def_delete(struct key_def *def)
{
	free(def);
}

static void
key_def_set_cmp(struct key_def *def)
{
	def->tuple_compare = tuple_compare_create(def);
	def->tuple_compare_with_key = tuple_compare_with_key_create(def);
	tuple_hash_func_set(def);
	tuple_extract_key_set(def);
}

struct key_def *
key_def_new(uint32_t part_count)
{
	size_t sz = key_def_sizeof(part_count);
	/** Use calloc() to zero comparator function pointers. */
	struct key_def *key_def = (struct key_def *) calloc(1, sz);
	if (key_def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct key_def");
		return NULL;
	}
	key_def->part_count = part_count;
	key_def->unique_part_count = part_count;
	return key_def;
}

struct key_def *
key_def_new_with_parts(struct key_part_def *parts, uint32_t part_count)
{
	struct key_def *def = key_def_new(part_count);
	if (def == NULL)
		return NULL;

	for (uint32_t i = 0; i < part_count; i++) {
		struct key_part_def *part = &parts[i];
		struct coll *coll = NULL;
		if (part->coll_id != COLL_NONE) {
			coll = coll_by_id(part->coll_id);
			if (coll == NULL) {
				diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
					 i + 1, "collation was not found by ID");
				key_def_delete(def);
				return NULL;
			}
		}
		key_def_set_part(def, i, part->fieldno, part->type,
				 part->nullable_action, coll);
	}
	return def;
}

void
key_def_dump_parts(const struct key_def *def, struct key_part_def *parts)
{
	for (uint32_t i = 0; i < def->part_count; i++) {
		const struct key_part *part = &def->parts[i];
		struct key_part_def *part_def = &parts[i];
		part_def->fieldno = part->fieldno;
		part_def->type = part->type;
		part_def->is_nullable = key_part_is_nullable(part);
		part_def->nullable_action = part->nullable_action;
		part_def->coll_id = (part->coll != NULL ?
				     part->coll->id : COLL_NONE);
	}
}

box_key_def_t *
box_key_def_new(uint32_t *fields, uint32_t *types, uint32_t part_count)
{
	struct key_def *key_def = key_def_new(part_count);
	if (key_def == NULL)
		return key_def;

	for (uint32_t item = 0; item < part_count; ++item) {
		key_def_set_part(key_def, item, fields[item],
				 (enum field_type)types[item],
				 key_part_def_default.nullable_action, NULL);
	}
	return key_def;
}

void
box_key_def_delete(box_key_def_t *key_def)
{
	key_def_delete(key_def);
}

int
box_tuple_compare(const box_tuple_t *tuple_a, const box_tuple_t *tuple_b,
		  const box_key_def_t *key_def)
{
	return tuple_compare(tuple_a, tuple_b, key_def);
}

int
box_tuple_compare_with_key(const box_tuple_t *tuple_a, const char *key_b,
			   const box_key_def_t *key_def)
{
	uint32_t part_count = mp_decode_array(&key_b);
	return tuple_compare_with_key(tuple_a, key_b, part_count, key_def);

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
		if (part1->coll != part2->coll)
			return (uintptr_t) part1->coll <
			       (uintptr_t) part2->coll ? -1 : 1;
		if (key_part_is_nullable(part1) != key_part_is_nullable(part2))
			return key_part_is_nullable(part1) <
			       key_part_is_nullable(part2) ? -1 : 1;
	}
	return part_count1 < part_count2 ? -1 : part_count1 > part_count2;
}

void
key_def_set_part(struct key_def *def, uint32_t part_no, uint32_t fieldno,
		 enum field_type type, enum on_conflict_action nullable_action,
		 struct coll *coll)
{
	assert(part_no < def->part_count);
	assert(type < field_type_MAX);
	def->is_nullable |= (nullable_action == ON_CONFLICT_ACTION_NONE);
	def->parts[part_no].nullable_action = nullable_action;
	def->parts[part_no].fieldno = fieldno;
	def->parts[part_no].type = type;
	def->parts[part_no].coll = coll;
	column_mask_set_fieldno(&def->column_mask, fieldno);
	/**
	 * When all parts are set, initialize the tuple
	 * comparator function.
	 */
	/* Last part is set, initialize the comparators. */
	bool all_parts_set = true;
	for (uint32_t i = 0; i < def->part_count; i++) {
		if (def->parts[i].type == FIELD_TYPE_ANY) {
			all_parts_set = false;
			break;
		}
	}
	if (all_parts_set)
		key_def_set_cmp(def);
}

void
key_def_update_optionality(struct key_def *def, uint32_t min_field_count)
{
	def->has_optional_parts = false;
	for (uint32_t i = 0; i < def->part_count; ++i) {
		struct key_part *part = &def->parts[i];
		def->has_optional_parts |= key_part_is_nullable(part) &&
					   min_field_count < part->fieldno + 1;
		/*
		 * One optional part is enough to switch to new
		 * comparators.
		 */
		if (def->has_optional_parts)
			break;
	}
	key_def_set_cmp(def);
}

int
key_def_snprint_parts(char *buf, int size, const struct key_part_def *parts,
		      uint32_t part_count)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "[");
	for (uint32_t i = 0; i < part_count; i++) {
		const struct key_part_def *part = &parts[i];
		assert(part->type < field_type_MAX);
		SNPRINT(total, snprintf, buf, size, "%d, '%s'",
			(int)part->fieldno, field_type_strs[part->type]);
		if (i < part_count - 1)
			SNPRINT(total, snprintf, buf, size, ", ");
	}
	SNPRINT(total, snprintf, buf, size, "]");
	return total;
}

size_t
key_def_sizeof_parts(const struct key_part_def *parts, uint32_t part_count)
{
	size_t size = 0;
	for (uint32_t i = 0; i < part_count; i++) {
		const struct key_part_def *part = &parts[i];
		int count = 2;
		if (part->coll_id != COLL_NONE)
			count++;
		if (part->is_nullable)
			count++;
		size += mp_sizeof_map(count);
		size += mp_sizeof_str(strlen(PART_OPT_FIELD));
		size += mp_sizeof_uint(part->fieldno);
		assert(part->type < field_type_MAX);
		size += mp_sizeof_str(strlen(PART_OPT_TYPE));
		size += mp_sizeof_str(strlen(field_type_strs[part->type]));
		if (part->coll_id != COLL_NONE) {
			size += mp_sizeof_str(strlen(PART_OPT_COLLATION));
			size += mp_sizeof_uint(part->coll_id);
		}
		if (part->is_nullable) {
			size += mp_sizeof_str(strlen(PART_OPT_NULLABILITY));
			size += mp_sizeof_bool(part->is_nullable);
		}
	}
	return size;
}

char *
key_def_encode_parts(char *data, const struct key_part_def *parts,
		     uint32_t part_count)
{
	for (uint32_t i = 0; i < part_count; i++) {
		const struct key_part_def *part = &parts[i];
		int count = 2;
		if (part->coll_id != COLL_NONE)
			count++;
		if (part->is_nullable)
			count++;
		data = mp_encode_map(data, count);
		data = mp_encode_str(data, PART_OPT_FIELD,
				     strlen(PART_OPT_FIELD));
		data = mp_encode_uint(data, part->fieldno);
		data = mp_encode_str(data, PART_OPT_TYPE,
				     strlen(PART_OPT_TYPE));
		assert(part->type < field_type_MAX);
		const char *type_str = field_type_strs[part->type];
		data = mp_encode_str(data, type_str, strlen(type_str));
		if (part->coll_id != COLL_NONE) {
			data = mp_encode_str(data, PART_OPT_COLLATION,
					     strlen(PART_OPT_COLLATION));
			data = mp_encode_uint(data, part->coll_id);
		}
		if (part->is_nullable) {
			data = mp_encode_str(data, PART_OPT_NULLABILITY,
					     strlen(PART_OPT_NULLABILITY));
			data = mp_encode_bool(data, part->is_nullable);
		}
	}
	return data;
}

/**
 * 1.6.6-1.7.5
 * Decode parts array from tuple field and write'em to index_def structure.
 * Throws a nice error about invalid types, but does not check ranges of
 *  resulting values field_no and field_type
 * Parts expected to be a sequence of <part_count> arrays like this:
 *  [NUM, STR, ..][NUM, STR, ..]..,
 */
static int
key_def_decode_parts_166(struct key_part_def *parts, uint32_t part_count,
			 const char **data, const struct field_def *fields,
			 uint32_t field_count)
{
	for (uint32_t i = 0; i < part_count; i++) {
		struct key_part_def *part = &parts[i];
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
		*part = key_part_def_default;
		part->fieldno = (uint32_t) mp_decode_uint(data);
		if (mp_typeof(**data) != MP_STR) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "field type must be a string");
			return -1;
		}
		uint32_t len;
		const char *str = mp_decode_str(data, &len);
		for (uint32_t j = 2; j < item_count; j++)
			mp_next(data);
		part->type = field_type_by_name(str, len);
		if (part->type == field_type_MAX) {
			diag_set(ClientError, ER_WRONG_INDEX_PARTS,
				 "unknown field type");
			return -1;
		}
		part->is_nullable = (part->fieldno < field_count ?
				     fields[part->fieldno].is_nullable :
				     key_part_def_default.is_nullable);
		part->coll_id = COLL_NONE;
	}
	return 0;
}

int
key_def_decode_parts(struct key_part_def *parts, uint32_t part_count,
		     const char **data, const struct field_def *fields,
		     uint32_t field_count)
{
	if (mp_typeof(**data) == MP_ARRAY) {
		return key_def_decode_parts_166(parts, part_count, data,
						fields, field_count);
	}
	for (uint32_t i = 0; i < part_count; i++) {
		struct key_part_def *part = &parts[i];
		if (mp_typeof(**data) != MP_MAP) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + TUPLE_INDEX_BASE,
				 "index part is expected to be a map");
			return -1;
		}
		int opts_count = mp_decode_map(data);
		*part = key_part_def_default;
		bool is_action_missing = true;
		uint32_t  action_literal_len = strlen("nullable_action");
		for (int j = 0; j < opts_count; ++j) {
			if (mp_typeof(**data) != MP_STR) {
				diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
					 i + TUPLE_INDEX_BASE,
					 "key must be a string");
				return -1;
			}
			uint32_t key_len;
			const char *key = mp_decode_str(data, &key_len);
			if (opts_parse_key(part, part_def_reg, key, key_len, data,
					   ER_WRONG_INDEX_OPTIONS,
					   i + TUPLE_INDEX_BASE, NULL,
					   false) != 0)
				return -1;
			if (is_action_missing &&
			    key_len == action_literal_len &&
			    memcmp(key, "nullable_action",
				   action_literal_len) == 0)
				is_action_missing = false;
		}
		if (is_action_missing) {
			part->nullable_action = part->is_nullable ?
				ON_CONFLICT_ACTION_NONE
				: ON_CONFLICT_ACTION_ABORT;
		}
		if (part->type == field_type_MAX) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + TUPLE_INDEX_BASE,
				 "index part: unknown field type");
			return -1;
		}
		if (part->coll_id != COLL_NONE &&
		    part->type != FIELD_TYPE_STRING &&
		    part->type != FIELD_TYPE_SCALAR) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + 1,
				 "collation is reasonable only for "
				 "string and scalar parts");
			return -1;
		}
		if (!((part->is_nullable && part->nullable_action ==
		       ON_CONFLICT_ACTION_NONE)
		      || (!part->is_nullable
			  && part->nullable_action !=
			  ON_CONFLICT_ACTION_NONE))) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + TUPLE_INDEX_BASE,
				 "index part: conflicting nullability and "
				 "nullable action properties");
			return -1;
		}
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

bool
key_def_contains(const struct key_def *first, const struct key_def *second)
{
	const struct key_part *part = second->parts;
	const struct key_part *end = part + second->part_count;
	for (; part != end; part++) {
		if (key_def_find(first, part->fieldno) == NULL)
			return false;
	}
	return true;
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
	new_def->unique_part_count = new_part_count;
	new_def->is_nullable = first->is_nullable || second->is_nullable;
	new_def->has_optional_parts = first->has_optional_parts ||
				      second->has_optional_parts;
	/* Write position in the new key def. */
	uint32_t pos = 0;
	/* Append first key def's parts to the new index_def. */
	part = first->parts;
	end = part + first->part_count;
	for (; part != end; part++) {
		key_def_set_part(new_def, pos++, part->fieldno, part->type,
				 part->nullable_action, part->coll);
	}

	/* Set-append second key def's part to the new key def. */
	part = second->parts;
	end = part + second->part_count;
	for (; part != end; part++) {
		if (key_def_find(first, part->fieldno))
			continue;
		key_def_set_part(new_def, pos++, part->fieldno, part->type,
				 part->nullable_action, part->coll);
	}
	return new_def;
}

int
key_validate_parts(const struct key_def *key_def, const char *key,
		   uint32_t part_count, bool allow_nullable)
{
	for (uint32_t i = 0; i < part_count; i++) {
		enum mp_type mp_type = mp_typeof(*key);
		const struct key_part *part = &key_def->parts[i];
		mp_next(&key);

		if (key_mp_type_validate(part->type, mp_type, ER_KEY_PART_TYPE,
					 i, key_part_is_nullable(part)
					 && allow_nullable))
			return -1;
	}
	return 0;
}
