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
#include "json/json.h"
#include "key_def.h"
#include "tuple_format.h"
#include "tuple_compare.h"
#include "tuple_extract_key.h"
#include "tuple_hash.h"
#include "column_mask.h"
#include "schema_def.h"
#include "coll_id_cache.h"
#include "small/region.h"
#include "coll/coll.h"
#include "fiber.h"

const char *sort_order_strs[] = { "asc", "desc", "undef" };

const struct key_part_def key_part_def_default = {
	0,
	field_type_MAX,
	COLL_NONE,
	false,
	ON_CONFLICT_ACTION_DEFAULT,
	SORT_ORDER_ASC,
	NULL,
	false
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
#define PART_OPT_SORT_ORDER	 "sort_order"
#define PART_OPT_PATH		 "path"
#define PART_OPT_EXCLUDE_NULL	 "exclude_null"

const struct opt_def part_def_reg[] = {
	OPT_DEF_ENUM(PART_OPT_TYPE, field_type, struct key_part_def, type,
		     part_type_by_name_wrapper),
	OPT_DEF(PART_OPT_FIELD, OPT_UINT32, struct key_part_def, fieldno),
	OPT_DEF(PART_OPT_COLLATION, OPT_UINT32, struct key_part_def, coll_id),
	OPT_DEF(PART_OPT_NULLABILITY, OPT_BOOL, struct key_part_def,
		is_nullable),
	OPT_DEF_ENUM(PART_OPT_NULLABLE_ACTION, on_conflict_action,
		     struct key_part_def, nullable_action, NULL),
	OPT_DEF_ENUM(PART_OPT_SORT_ORDER, sort_order, struct key_part_def,
		     sort_order, NULL),
	OPT_DEF(PART_OPT_PATH, OPT_STRPTR, struct key_part_def, path),
	OPT_DEF(PART_OPT_EXCLUDE_NULL, OPT_BOOL, struct key_part_def,
			exclude_null),
	OPT_END,
};

/**
 * Return the size of memory occupied by the given key definition.
 */
static inline size_t
key_def_copy_size(const struct key_def *def)
{
	size_t sz = 0;
	for (uint32_t i = 0; i < def->part_count; i++)
		sz += def->parts[i].path_len;
	return key_def_sizeof(def->part_count, sz);
}

/**
 * A helper function for key_def_copy() and key_def_dup() that
 * copies key definition src of size sz to res without checking
 * that the two key definitions have the same allocation size.
 */
static struct key_def *
key_def_copy_impl(struct key_def *res, const struct key_def *src, size_t sz)
{
	memcpy(res, src, sz);
	/*
	 * Update the paths pointers so that they refer to the
	 * JSON strings bytes in the new allocation.
	 */
	for (uint32_t i = 0; i < src->part_count; i++) {
		if (src->parts[i].path == NULL)
			continue;
		size_t path_offset = src->parts[i].path - (char *)src;
		res->parts[i].path = (char *)res + path_offset;
	}
	if (src->multikey_path != NULL) {
		size_t path_offset = src->multikey_path - (char *)src;
		res->multikey_path = (char *)res + path_offset;
	}
	return res;
}

void
key_def_copy(struct key_def *dest, const struct key_def *src)
{
	size_t sz = key_def_copy_size(src);
	assert(sz == key_def_copy_size(dest));
	key_def_copy_impl(dest, src, sz);
}

struct key_def *
key_def_dup(const struct key_def *src)
{
	size_t sz = key_def_copy_size(src);
	struct key_def *res = malloc(sz);
	if (res == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "res");
		return NULL;
	}
	key_def_copy_impl(res, src, sz);
	return res;
}

void
key_def_delete(struct key_def *def)
{
	free(def);
}

static void
key_def_set_func(struct key_def *def)
{
	key_def_set_compare_func(def);
	key_def_set_hash_func(def);
	key_def_set_extract_func(def);
}

static int
key_def_set_part_path(struct key_def *def, uint32_t part_no, const char *path,
		      uint32_t path_len, char **path_pool)
{
	struct key_part *part = &def->parts[part_no];
	if (path == NULL) {
		part->path = NULL;
		part->path_len = 0;
		return 0;
	}
	assert(path_pool != NULL);
	part->path = *path_pool;
	*path_pool += path_len;
	memcpy(part->path, path, path_len);
	part->path_len = path_len;

	/*
	 * Test whether this key_part has array index
	 * placeholder [*] (i.e. is a part of multikey index
	 * definition).
	 */
	int multikey_path_len =
		json_path_multikey_offset(path, path_len, TUPLE_INDEX_BASE);
	if ((uint32_t) multikey_path_len == path_len)
		return 0;

	/*
	 * In case of multikey index key_parts must have the
	 * same JSON prefix.
	 */
	if (def->multikey_path == NULL) {
		/*
		 * Keep the index of the first multikey key_part
		 * and the length of JSON path substring to the
		 * array index placeholder sign [*].
		 */
		def->multikey_path = part->path;
		def->multikey_fieldno = part->fieldno;
		def->multikey_path_len = (uint32_t) multikey_path_len;
		def->is_multikey = true;
	} else if (def->multikey_fieldno != part->fieldno ||
		   json_path_cmp(path, multikey_path_len, def->multikey_path,
				 def->multikey_path_len,
				 TUPLE_INDEX_BASE) != 0) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 part_no + TUPLE_INDEX_BASE,
			 "incompatable multikey index path");
		return -1;
	}

	/* Skip JSON_TOKEN_ANY token. */
	struct json_lexer lexer;
	struct json_token token;
	json_lexer_create(&lexer, path + multikey_path_len,
			  path_len - multikey_path_len, TUPLE_INDEX_BASE);
	json_lexer_next_token(&lexer, &token);
	assert(token.type == JSON_TOKEN_ANY);

	/* The rest of JSON path couldn't be multikey. */
	int multikey_path_suffix_len =
		path_len - multikey_path_len - lexer.offset;
	if (json_path_multikey_offset(path + multikey_path_len + lexer.offset,
				      multikey_path_suffix_len,
				      TUPLE_INDEX_BASE) !=
	    multikey_path_suffix_len) {
		diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
			 part_no + TUPLE_INDEX_BASE,
			 "no more than one array index placeholder [*] is "
			 "allowed in JSON path");
		return -1;
	}
	return 0;
}

static int
key_def_set_part(struct key_def *def, uint32_t part_no, uint32_t fieldno,
		 enum field_type type, enum on_conflict_action nullable_action,
		 bool exclude_null, struct coll *coll, uint32_t coll_id,
		 enum sort_order sort_order, const char *path,
		 uint32_t path_len, char **path_pool, int32_t offset_slot,
		 uint64_t format_epoch)
{
	assert(part_no < def->part_count);
	assert(type < field_type_MAX);
	def->is_nullable |= (nullable_action == ON_CONFLICT_ACTION_NONE);
	def->has_exclude_null |= exclude_null;
	def->has_json_paths |= path != NULL;
	def->parts[part_no].nullable_action = nullable_action;
	def->parts[part_no].exclude_null = exclude_null;
	def->parts[part_no].fieldno = fieldno;
	def->parts[part_no].type = type;
	def->parts[part_no].coll = coll;
	def->parts[part_no].coll_id = coll_id;
	def->parts[part_no].sort_order = sort_order;
	def->parts[part_no].offset_slot_cache = offset_slot;
	def->parts[part_no].format_epoch = format_epoch;
	column_mask_set_fieldno(&def->column_mask, fieldno);
	return key_def_set_part_path(def, part_no, path, path_len, path_pool);
}

struct key_def *
key_def_new(const struct key_part_def *parts, uint32_t part_count,
	    bool for_func_index)
{
	size_t sz = 0;
	for (uint32_t i = 0; i < part_count; i++)
		sz += parts[i].path != NULL ? strlen(parts[i].path) : 0;
	sz = key_def_sizeof(part_count, sz);
	struct key_def *def = calloc(1, sz);
	if (def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct key_def");
		return NULL;
	}

	def->part_count = part_count;
	def->unique_part_count = part_count;
	def->for_func_index = for_func_index;
	/* A pointer to the JSON paths data in the new key_def. */
	char *path_pool = (char *)def + key_def_sizeof(part_count, 0);
	for (uint32_t i = 0; i < part_count; i++) {
		const struct key_part_def *part = &parts[i];
		struct coll *coll = NULL;
		if (part->coll_id != COLL_NONE) {
			struct coll_id *coll_id = coll_by_id(part->coll_id);
			if (coll_id == NULL) {
				diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
					 i + 1, "collation was not found by ID");
				goto error;
			}
			coll = coll_id->coll;
		}
		uint32_t path_len = part->path != NULL ? strlen(part->path) : 0;
		if (key_def_set_part(def, i, part->fieldno, part->type,
				     part->nullable_action, part->exclude_null,
				     coll, part->coll_id,
				     part->sort_order, part->path, path_len,
				     &path_pool, TUPLE_OFFSET_SLOT_NIL,
				     0) != 0)
			goto error;
	}
	if (for_func_index) {
		if (def->has_json_paths) {
			diag_set(ClientError, ER_UNSUPPORTED,
				 "Functional index", "json paths");
			goto error;
		}
		if(!key_def_is_sequential(def) || parts->fieldno != 0) {
			diag_set(ClientError, ER_FUNC_INDEX_PARTS,
				 "key part numbers must be sequential and "
				 "first part number must be 1");
			goto error;
		}
	}
	assert(path_pool == (char *)def + sz);
	key_def_set_func(def);
	return def;
error:
	key_def_delete(def);
	return NULL;
}

int
key_def_dump_parts(const struct key_def *def, struct key_part_def *parts,
		   struct region *region)
{
	for (uint32_t i = 0; i < def->part_count; i++) {
		const struct key_part *part = &def->parts[i];
		struct key_part_def *part_def = &parts[i];
		part_def->fieldno = part->fieldno;
		part_def->type = part->type;
		part_def->is_nullable = key_part_is_nullable(part);
		part_def->exclude_null = part->exclude_null;
		part_def->nullable_action = part->nullable_action;
		part_def->coll_id = part->coll_id;
		if (part->path != NULL) {
			char *path = region_alloc(region, part->path_len + 1);
			if (path == NULL) {
				diag_set(OutOfMemory, part->path_len + 1,
					 "region", "part_def->path");
				return -1;
			}
			memcpy(path, part->path, part->path_len);
			path[part->path_len] = '\0';
			part_def->path = path;
		} else {
			part_def->path = NULL;
		}
	}
	return 0;
}

 /* {{{ Module API helpers */

static int
key_def_set_internal_part(struct key_part_def *internal_part,
			  box_key_part_def_t *part, struct region *region)
{
	*internal_part = key_part_def_default;

	/* Set internal_part->fieldno. */
	internal_part->fieldno = part->fieldno;

	/* Set internal_part->type. */
	if (part->field_type == NULL) {
		diag_set(IllegalParams, "Field type is mandatory");
		return -1;
	}
	size_t type_len = strlen(part->field_type);
	internal_part->type = field_type_by_name(part->field_type, type_len);
	if (internal_part->type == field_type_MAX) {
		diag_set(IllegalParams, "Unknown field type: \"%s\"",
			 part->field_type);
		return -1;
	}

	/* Set internal_part->{is_nullable,nullable_action}. */
	bool is_nullable = (part->flags & BOX_KEY_PART_DEF_IS_NULLABLE) ==
		BOX_KEY_PART_DEF_IS_NULLABLE;
	if (is_nullable) {
		internal_part->is_nullable = is_nullable;
		internal_part->nullable_action = ON_CONFLICT_ACTION_NONE;
	}
	internal_part->exclude_null =
		(part->flags & BOX_KEY_PART_DEF_EXCLUDE_NULL)
		== BOX_KEY_PART_DEF_EXCLUDE_NULL;

	/* Set internal_part->coll_id. */
	if (part->collation != NULL) {
		size_t collation_len = strlen(part->collation);
		struct coll_id *coll_id = coll_by_name(part->collation,
						       collation_len);
		if (coll_id == NULL) {
			diag_set(IllegalParams, "Unknown collation: \"%s\"",
				 part->collation);
			return -1;
		}
		internal_part->coll_id = coll_id->id;
	}

	/* Set internal_part->path (JSON path). */
	if (part->path != NULL) {
		size_t path_len = strlen(part->path);
		if (json_path_validate(part->path, path_len,
				       TUPLE_INDEX_BASE) != 0) {
			diag_set(IllegalParams, "Invalid JSON path: \"%s\"",
				 part->path);
			return -1;
		}
		char *tmp = region_alloc(region, path_len + 1);
		if (tmp == NULL) {
			diag_set(OutOfMemory, path_len + 1, "region", "path");
			return -1;
		}
		memcpy(tmp, part->path, path_len + 1);
		internal_part->path = tmp;
	}

	return 0;
}

/* }}} Module API helpers */

/* {{{ Module API functions */

box_key_def_t *
box_key_def_new(uint32_t *fields, uint32_t *types, uint32_t part_count)
{
	size_t sz = key_def_sizeof(part_count, 0);
	struct key_def *key_def = calloc(1, sz);
	if (key_def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "struct key_def");
		return NULL;
	}

	key_def->part_count = part_count;
	key_def->unique_part_count = part_count;

	for (uint32_t item = 0; item < part_count; ++item) {
		if (key_def_set_part(key_def, item, fields[item],
				     (enum field_type)types[item],
				     ON_CONFLICT_ACTION_DEFAULT, false, NULL,
				     COLL_NONE, SORT_ORDER_ASC, NULL, 0, NULL,
				     TUPLE_OFFSET_SLOT_NIL, 0) != 0) {
			key_def_delete(key_def);
			return NULL;
		}
	}
	key_def_set_func(key_def);
	return key_def;
}

void
box_key_part_def_create(box_key_part_def_t *part)
{
	memset(part, 0, sizeof(*part));
}

box_key_def_t *
box_key_def_new_v2(box_key_part_def_t *parts, uint32_t part_count)
{
	if (part_count == 0) {
		diag_set(IllegalParams, "At least one key part is required");
		return NULL;
	}

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t internal_parts_size;
	struct key_part_def *internal_parts =
		region_alloc_array(region, typeof(internal_parts[0]),
				   part_count, &internal_parts_size);
	if (internal_parts == NULL) {
		diag_set(OutOfMemory, internal_parts_size, "region_alloc_array",
			 "parts");
		return NULL;
	}

	/*
	 * It is possible to implement a function similar to
	 * key_def_new() and eliminate <box_key_part_def_t> ->
	 * <struct key_part_def> copying. However this would lead
	 * to code duplication and would complicate maintanence,
	 * so it worth to do so only if key_def creation will
	 * appear on a hot path in some meaningful use case.
	 */
	uint32_t min_field_count = 0;
	for (uint32_t i = 0; i < part_count; ++i) {
		if (key_def_set_internal_part(&internal_parts[i], &parts[i],
					      region) != 0) {
			region_truncate(region, region_svp);
			return NULL;
		}
		bool is_nullable =
			(parts[i].flags & BOX_KEY_PART_DEF_IS_NULLABLE) ==
			BOX_KEY_PART_DEF_IS_NULLABLE;
		if (!is_nullable && parts[i].fieldno > min_field_count)
			min_field_count = parts[i].fieldno;
	}

	struct key_def *key_def = key_def_new(internal_parts, part_count,
					      false);
	region_truncate(region, region_svp);
	if (key_def == NULL)
		return NULL;

	/*
	 * Update key_def->has_optional_parts and function
	 * pointers.
	 *
	 * FIXME: It seems, this call should be part of
	 * key_def_new(), because otherwise a callee function may
	 * obtain an incorrect key_def. However I don't know any
	 * case that would prove this guess.
	 */
	key_def_update_optionality(key_def, min_field_count);

	return key_def;
}

box_key_def_t *
box_key_def_dup(const box_key_def_t *key_def)
{
	return key_def_dup(key_def);
}

void
box_key_def_delete(box_key_def_t *key_def)
{
	key_def_delete(key_def);
}

box_key_part_def_t *
box_key_def_dump_parts(const box_key_def_t *key_def, uint32_t *part_count_ptr)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	size_t size;
	box_key_part_def_t *parts = region_alloc_array(
		region, typeof(parts[0]), key_def->part_count, &size);
	if (parts == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "parts");
		return NULL;
	}

	for (uint32_t i = 0; i < key_def->part_count; i++) {
		const struct key_part *part = &key_def->parts[i];
		box_key_part_def_t *part_def = &parts[i];
		box_key_part_def_create(part_def);

		/* Set part->{fieldno,flags,field_type}. */
		part_def->fieldno = part->fieldno;
		part_def->flags = 0;
		if (key_part_is_nullable(part))
			part_def->flags |= BOX_KEY_PART_DEF_IS_NULLABLE;
		if (part->exclude_null)
			part_def->flags |= BOX_KEY_PART_DEF_EXCLUDE_NULL;
		assert(part->type >= 0 && part->type < field_type_MAX);
		part_def->field_type = field_type_strs[part->type];

		/* Set part->collation. */
		if (part->coll_id != COLL_NONE) {
			struct coll_id *coll_id = coll_by_id(part->coll_id);
			/*
			 * A collation may be removed after
			 * key_def creation.
			 */
			if (coll_id == NULL) {
				diag_set(CollationError,
					 "key_def holds dead collation id %d",
					 part->coll_id);
				region_truncate(region, region_svp);
				return NULL;
			}
			/*
			 * A collation may be removed while the
			 * resulting key parts array is in use.
			 */
			char *collation = region_alloc(region,
						       coll_id->name_len + 1);
			if (collation == NULL) {
				diag_set(OutOfMemory, coll_id->name_len + 1,
					 "region_alloc", "part_def->collation");
				region_truncate(region, region_svp);
				return NULL;
			}
			memcpy(collation, coll_id->name, coll_id->name_len);
			collation[coll_id->name_len] = '\0';
			part_def->collation = collation;
		}

		/* Set part->path. */
		if (part->path != NULL) {
			char *path = region_alloc(region, part->path_len + 1);
			if (path == NULL) {
				diag_set(OutOfMemory, part->path_len + 1,
					 "region", "part_def->path");
				region_truncate(region, region_svp);
				return NULL;
			}
			memcpy(path, part->path, part->path_len);
			path[part->path_len] = '\0';
			part_def->path = path;
		}
	}

	if (part_count_ptr != NULL)
		*part_count_ptr = key_def->part_count;

	return parts;
}

int
box_key_def_validate_tuple(box_key_def_t *key_def, box_tuple_t *tuple)
{
	return tuple_validate_key_parts(key_def, tuple);
}

int
box_tuple_compare(box_tuple_t *tuple_a, box_tuple_t *tuple_b,
		  box_key_def_t *key_def)
{
	return tuple_compare(tuple_a, HINT_NONE, tuple_b, HINT_NONE, key_def);
}

int
box_tuple_compare_with_key(box_tuple_t *tuple_a, const char *key_b,
			   box_key_def_t *key_def)
{
	uint32_t part_count = mp_decode_array(&key_b);
	return tuple_compare_with_key(tuple_a, HINT_NONE, key_b,
				      part_count, HINT_NONE, key_def);

}

box_key_def_t *
box_key_def_merge(const box_key_def_t *first, const box_key_def_t *second)
{
	return key_def_merge(first, second);
}

char *
box_key_def_extract_key(box_key_def_t *key_def, box_tuple_t *tuple,
			int multikey_idx, uint32_t *key_size_ptr)
{
	return tuple_extract_key(tuple, key_def, multikey_idx, key_size_ptr);
}

int
box_key_def_validate_key(const box_key_def_t *key_def, const char *key,
			 uint32_t *key_size_ptr)
{
	const char *pos = key;
	uint32_t part_count = mp_decode_array(&pos);
	if (part_count > key_def->part_count) {
		diag_set(ClientError, ER_KEY_PART_COUNT, key_def->part_count,
			 part_count);
		return -1;
	}
	int rc = key_validate_parts(key_def, pos, part_count, true, &pos);
	if (rc == 0 && key_size_ptr != NULL)
		*key_size_ptr = pos - key;
	return rc;
}

int
box_key_def_validate_full_key(const box_key_def_t *key_def, const char *key,
			      uint32_t *key_size_ptr)
{
	const char *pos = key;
	uint32_t part_count = mp_decode_array(&pos);
	if (part_count != key_def->part_count) {
		diag_set(ClientError, ER_EXACT_MATCH, key_def->part_count,
			 part_count);
		return -1;
	}
	int rc = key_validate_parts(key_def, pos, part_count, true, &pos);
	if (rc == 0 && key_size_ptr != NULL)
		*key_size_ptr = pos - key;
	return rc;
}

/* }}} Module API functions */

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
		if (part1->sort_order != part2->sort_order)
			return part1->sort_order < part2->sort_order ? -1 : 1;
		if (key_part_is_nullable(part1) != key_part_is_nullable(part2))
			return key_part_is_nullable(part1) <
			       key_part_is_nullable(part2) ? -1 : 1;
		if (part1->exclude_null != part2->exclude_null)
			return part1->exclude_null < part2->exclude_null ? -1 : 1;
		int rc = json_path_cmp(part1->path, part1->path_len,
				       part2->path, part2->path_len,
				       TUPLE_INDEX_BASE);
		if (rc != 0)
			return rc;
	}
	return part_count1 < part_count2 ? -1 : part_count1 > part_count2;
}

void
key_def_update_optionality(struct key_def *def, uint32_t min_field_count)
{
	def->has_optional_parts = false;
	for (uint32_t i = 0; i < def->part_count; ++i) {
		struct key_part *part = &def->parts[i];
		def->has_optional_parts |=
			(min_field_count < part->fieldno + 1 ||
			 part->path != NULL) && key_part_is_nullable(part);
		/*
		 * One optional part is enough to switch to new
		 * comparators.
		 */
		if (def->has_optional_parts)
			break;
	}
	key_def_set_func(def);
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
		SNPRINT(total, snprintf, buf, size, "[%d, '%s'",
			(int)part->fieldno, field_type_strs[part->type]);
		if (part->path != NULL) {
			SNPRINT(total, snprintf, buf, size, ", path='%s'",
				part->path);
		}
		SNPRINT(total, snprintf, buf, size, "]");
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
		if (part->exclude_null)
			count++;
		if (part->path != NULL)
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
		if (part->exclude_null) {
			size += mp_sizeof_str(strlen(PART_OPT_EXCLUDE_NULL));
			size += mp_sizeof_bool(part->exclude_null);
		}
		if (part->path != NULL) {
			size += mp_sizeof_str(strlen(PART_OPT_PATH));
			size += mp_sizeof_str(strlen(part->path));
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
		if (part->exclude_null)
			count++;
		if (part->path != NULL)
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
		if (part->exclude_null) {
			data = mp_encode_str(data, PART_OPT_EXCLUDE_NULL,
					     strlen(PART_OPT_EXCLUDE_NULL));
			data = mp_encode_bool(data, part->exclude_null);
		}
		if (part->path != NULL) {
			data = mp_encode_str(data, PART_OPT_PATH,
					     strlen(PART_OPT_PATH));
			data = mp_encode_str(data, part->path,
					     strlen(part->path));
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
		part->exclude_null = false;
		part->coll_id = COLL_NONE;
		part->path = NULL;
	}
	return 0;
}

int
key_def_decode_parts(struct key_part_def *parts, uint32_t part_count,
		     const char **data, const struct field_def *fields,
		     uint32_t field_count, struct region *region)
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
					   i + TUPLE_INDEX_BASE, region,
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
				: ON_CONFLICT_ACTION_DEFAULT;
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
		if (part->sort_order == sort_order_MAX) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + TUPLE_INDEX_BASE,
				 "index part: unknown sort order");
			return -1;
		}
		if (part->path != NULL &&
		    json_path_validate(part->path, strlen(part->path),
				       TUPLE_INDEX_BASE) != 0) {
			diag_set(ClientError, ER_WRONG_INDEX_OPTIONS,
				 i + TUPLE_INDEX_BASE, "invalid path");
			return -1;
		}
	}
	return 0;
}

const struct key_part *
key_def_find_by_fieldno(const struct key_def *key_def, uint32_t fieldno)
{
	struct key_part part;
	memset(&part, 0, sizeof(struct key_part));
	part.fieldno = fieldno;
	return key_def_find(key_def, &part);
}

const struct key_part *
key_def_find(const struct key_def *key_def, const struct key_part *to_find)
{
	const struct key_part *part = key_def->parts;
	const struct key_part *end = part + key_def->part_count;
	for (; part != end; part++) {
		if (part->fieldno == to_find->fieldno &&
		    json_path_cmp(part->path, part->path_len,
				  to_find->path, to_find->path_len,
				  TUPLE_INDEX_BASE) == 0)
			return part;
	}
	return NULL;
}

bool
key_def_contains(const struct key_def *first, const struct key_def *second)
{
	/*
	 * Func index definitions cannot be contained in
	 * each other.
	 */
	if (first->for_func_index || second->for_func_index)
		return false;
	const struct key_part *part = second->parts;
	const struct key_part *end = part + second->part_count;
	for (; part != end; part++) {
		if (key_def_find(first, part) == NULL)
			return false;
	}
	return true;
}

/**
 * Return true if to_merge can be merged into key_def.
 */
static bool
key_def_can_merge(const struct key_def *key_def,
		  const struct key_part *to_merge)
{
	if (key_def->for_func_index) {
		/*
		 * Nothing can be omitted in functional index
		 * key definition, everything should be merged.
		 */
		return true;
	}

	const struct key_part *part = key_def_find(key_def, to_merge);
	if (part == NULL)
		return true;
	/*
	 * If both key_def and to_merge have the same field, then
	 * we can merge to_merge into key_def only if its collation
	 * may impose a strict order on otherwise equal keys.
	 */
	return coll_can_merge(part->coll, to_merge->coll);
}

struct key_def *
key_def_merge(const struct key_def *first, const struct key_def *second)
{
	assert(!second->for_func_index);
	uint32_t new_part_count = first->part_count + second->part_count;
	/*
	 * Find and remove part duplicates, i.e. parts counted
	 * twice since they are present in both key defs.
	 */
	size_t sz = 0;
	const struct key_part *part = first->parts;
	const struct key_part *end = part + first->part_count;
	for (; part != end; part++)
		sz += part->path_len;
	part = second->parts;
	end = part + second->part_count;
	for (; part != end; part++) {
		if (!key_def_can_merge(first, part))
			--new_part_count;
		else
			sz += part->path_len;
	}

	sz = key_def_sizeof(new_part_count, sz);
	struct key_def *new_def;
	new_def = (struct key_def *)calloc(1, sz);
	if (new_def == NULL) {
		diag_set(OutOfMemory, sz, "malloc", "new_def");
		return NULL;
	}
	new_def->part_count = new_part_count;
	new_def->unique_part_count = new_part_count;
	new_def->is_nullable = first->is_nullable || second->is_nullable;
	new_def->has_exclude_null = first->has_exclude_null ||
				    second->has_exclude_null;
	new_def->has_optional_parts = first->has_optional_parts ||
				      second->has_optional_parts;
	new_def->is_multikey = first->is_multikey || second->is_multikey;
	new_def->for_func_index = first->for_func_index;
	new_def->func_index_func = first->func_index_func;

	/* JSON paths data in the new key_def. */
	char *path_pool = (char *)new_def + key_def_sizeof(new_part_count, 0);
	/* Write position in the new key def. */
	uint32_t pos = 0;
	/* Append first key def's parts to the new index_def. */
	part = first->parts;
	end = part + first->part_count;
	for (; part != end; part++) {
		if (key_def_set_part(new_def, pos++, part->fieldno, part->type,
				     part->nullable_action, part->exclude_null, part->coll,
				     part->coll_id, part->sort_order,
				     part->path, part->path_len, &path_pool,
				     part->offset_slot_cache,
				     part->format_epoch) != 0) {
			key_def_delete(new_def);
			return NULL;
		}
	}

	/* Set-append second key def's part to the new key def. */
	part = second->parts;
	end = part + second->part_count;
	for (; part != end; part++) {
		if (!key_def_can_merge(first, part))
			continue;
		if (key_def_set_part(new_def, pos++, part->fieldno, part->type,
				     part->nullable_action, part->exclude_null, part->coll,
				     part->coll_id, part->sort_order,
				     part->path, part->path_len, &path_pool,
				     part->offset_slot_cache,
				     part->format_epoch) != 0) {
			key_def_delete(new_def);
			return NULL;
		}
	}
	assert(path_pool == (char *)new_def + sz);
	key_def_set_func(new_def);
	return new_def;
}

struct key_def *
key_def_find_pk_in_cmp_def(const struct key_def *cmp_def,
			   const struct key_def *pk_def,
			   struct region *region)
{
	struct key_def *extracted_def = NULL;
	size_t region_svp = region_used(region);

	/* First, dump primary key parts as is. */
	size_t size;
	struct key_part_def *parts =
		region_alloc_array(region, typeof(parts[0]), pk_def->part_count,
				   &size);
	if (parts == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "parts");
		goto out;
	}
	if (key_def_dump_parts(pk_def, parts, region) != 0)
		goto out;
	/*
	 * Second, update field numbers to match the primary key
	 * parts in a secondary key.
	 */
	for (uint32_t i = 0; i < pk_def->part_count; i++) {
		const struct key_part *part = key_def_find(cmp_def,
							   &pk_def->parts[i]);
		assert(part != NULL);
		parts[i].fieldno = part - cmp_def->parts;
		parts[i].path = NULL;
	}

	/* Finally, allocate the new key definition. */
	extracted_def = key_def_new(parts, pk_def->part_count, false);
out:
	region_truncate(region, region_svp);
	return extracted_def;
}

int
key_validate_parts(const struct key_def *key_def, const char *key,
		   uint32_t part_count, bool allow_nullable,
		   const char **key_end)
{
	for (uint32_t i = 0; i < part_count; i++) {
		const struct key_part *part = &key_def->parts[i];
		if (key_part_validate(part->type, key, i,
				      key_part_is_nullable(part) &&
				      allow_nullable))
			return -1;
		mp_next(&key);
	}
	*key_end = key;
	return 0;
}
