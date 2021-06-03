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
#include "bit/bit.h"
#include "fiber.h"
#include "json/json.h"
#include "tuple_format.h"
#include "coll_id_cache.h"
#include "tt_static.h"

#include <PMurHash.h>

/** Global table of tuple formats */
struct tuple_format **tuple_formats;
static intptr_t recycled_format_ids = FORMAT_ID_NIL;

static uint32_t formats_size = 0, formats_capacity = 0;
static uint64_t formats_epoch = 0;

/**
 * Find in format1::fields the field by format2_field's JSON path.
 * Routine uses fiber region for temporal path allocation and
 * panics on failure.
 */
static struct tuple_field *
tuple_format1_field_by_format2_field(struct tuple_format *format1,
				     struct tuple_field *format2_field)
{
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint32_t path_len = json_tree_snprint_path(NULL, 0,
				&format2_field->token, TUPLE_INDEX_BASE);
	char *path = region_alloc(region, path_len + 1);
	if (path == NULL)
		panic("Can not allocate memory for path");
	json_tree_snprint_path(path, path_len + 1, &format2_field->token,
			       TUPLE_INDEX_BASE);
	struct tuple_field *format1_field =
		json_tree_lookup_path_entry(&format1->fields,
					    &format1->fields.root, path,
					    path_len, TUPLE_INDEX_BASE,
					    struct tuple_field, token);
	region_truncate(region, region_svp);
	return format1_field;
}

static int
tuple_format_cmp(const struct tuple_format *format1,
		 const struct tuple_format *format2)
{
	struct tuple_format *a = (struct tuple_format *)format1;
	struct tuple_format *b = (struct tuple_format *)format2;
	if (a->exact_field_count != b->exact_field_count)
		return a->exact_field_count - b->exact_field_count;
	if (a->total_field_count != b->total_field_count)
		return a->total_field_count - b->total_field_count;

	struct tuple_field *field_a;
	json_tree_foreach_entry_preorder(field_a, &a->fields.root,
					 struct tuple_field, token) {
		struct tuple_field *field_b =
			tuple_format1_field_by_format2_field(b, field_a);
		if (field_a->type != field_b->type)
			return (int)field_a->type - (int)field_b->type;
		if (field_a->coll_id != field_b->coll_id)
			return (int)field_a->coll_id - (int)field_b->coll_id;
		if (field_a->nullable_action != field_b->nullable_action)
			return (int)field_a->nullable_action -
				(int)field_b->nullable_action;
		if (field_a->is_key_part != field_b->is_key_part)
			return (int)field_a->is_key_part -
				(int)field_b->is_key_part;
	}

	return 0;
}

static uint32_t
tuple_format_hash(struct tuple_format *format)
{
#define TUPLE_FIELD_MEMBER_HASH(field, member, h, carry, size) \
	PMurHash32_Process(&h, &carry, &field->member, \
			   sizeof(field->member)); \
	size += sizeof(field->member);

	uint32_t h = 13;
	uint32_t carry = 0;
	uint32_t size = 0;
	struct tuple_field *f;
	json_tree_foreach_entry_preorder(f, &format->fields.root,
					 struct tuple_field, token) {
		TUPLE_FIELD_MEMBER_HASH(f, type, h, carry, size)
		TUPLE_FIELD_MEMBER_HASH(f, coll_id, h, carry, size)
		TUPLE_FIELD_MEMBER_HASH(f, nullable_action, h, carry, size)
		TUPLE_FIELD_MEMBER_HASH(f, is_key_part, h, carry, size)
	}
#undef TUPLE_FIELD_MEMBER_HASH
	return PMurHash32_Result(h, carry, size);
}

#define MH_SOURCE 1
#define mh_name _tuple_format
#define mh_key_t struct tuple_format *
#define mh_node_t struct tuple_format *
#define mh_arg_t void *
#define mh_hash(a, arg) ((*(a))->hash)
#define mh_hash_key(a, arg) ((a)->hash)
#define mh_cmp(a, b, arg) (tuple_format_cmp(*(a), *(b)))
#define mh_cmp_key(a, b, arg) (tuple_format_cmp((a), *(b)))
#include "salad/mhash.h"

static struct mh_tuple_format_t *tuple_formats_hash = NULL;

static struct tuple_field *
tuple_field_new(void)
{
	struct tuple_field *field = calloc(1, sizeof(struct tuple_field));
	if (field == NULL) {
		diag_set(OutOfMemory, sizeof(struct tuple_field), "malloc",
			 "tuple field");
		return NULL;
	}
	field->id = UINT32_MAX;
	field->token.type = JSON_TOKEN_END;
	field->type = FIELD_TYPE_ANY;
	field->offset_slot = TUPLE_OFFSET_SLOT_NIL;
	field->coll_id = COLL_NONE;
	field->nullable_action = ON_CONFLICT_ACTION_NONE;
	field->multikey_required_fields = NULL;
	return field;
}

static void
tuple_field_delete(struct tuple_field *field)
{
	free(field->multikey_required_fields);
	free(field);
}

/** Return path to a tuple field. Used for error reporting. */
static const char *
tuple_field_path(const struct tuple_field *field,
		 const struct tuple_format *format)
{
	assert(field->token.parent != NULL);
	if (field->token.parent->parent == NULL) {
		int name_count = format->dict->name_count;
		if (field->token.num < name_count) {
			const char *path;
			int token_num = field->token.num;
			path = tt_sprintf("%d (%s)",
					  token_num + TUPLE_INDEX_BASE,
					  format->dict->names[token_num]);
			return path;
		}
		return int2str(field->token.num + TUPLE_INDEX_BASE);
	}
	char *path = tt_static_buf();
	json_tree_snprint_path(path, TT_STATIC_BUF_LEN, &field->token,
			       TUPLE_INDEX_BASE);
	return path;
}

/**
 * Look up field metadata by identifier.
 *
 * Used only for error reporing so we can afford full field
 * tree traversal here.
 */
static struct tuple_field *
tuple_format_field_by_id(struct tuple_format *format, uint32_t id)
{
	struct tuple_field *field;
	json_tree_foreach_entry_preorder(field, &format->fields.root,
					 struct tuple_field, token) {
		if (field->id == id)
			return field;
	}
	return NULL;
}

/**
 * Check if child_field may be attached to parent_field,
 * update the parent_field container type if required.
 */
static int
tuple_field_ensure_child_compatibility(struct tuple_field *parent,
				       struct tuple_field *child,
				       struct tuple_format* format)
{
	enum field_type expected_type =
		child->token.type == JSON_TOKEN_STR ?
		FIELD_TYPE_MAP : FIELD_TYPE_ARRAY;
	if (field_type1_contains_type2(parent->type, expected_type)) {
		parent->type = expected_type;
	} else {
		diag_set(ClientError, ER_INDEX_PART_TYPE_MISMATCH,
			 tuple_field_path(parent, format),
			 field_type_strs[parent->type],
			 field_type_strs[expected_type]);
		return -1;
	}
	/*
	 * Attempt to append JSON_TOKEN_ANY leaf to parent that
	 * has other child records already i.e. is a intermediate
	 * field of non-multikey JSON index.
	 */
	if (child->token.type == JSON_TOKEN_ANY &&
	    !json_token_is_multikey(&parent->token) &&
	    !json_token_is_leaf(&parent->token)) {
		diag_set(ClientError, ER_MULTIKEY_INDEX_MISMATCH,
			 tuple_field_path(parent, format));
		return -1;
	}
	/*
	 * Attempt to append not JSON_TOKEN_ANY child record to
	 * the parent defined as multikey index root.
	 */
	if (json_token_is_multikey(&parent->token) &&
	    child->token.type != JSON_TOKEN_ANY) {
		diag_set(ClientError, ER_MULTIKEY_INDEX_MISMATCH,
			 tuple_field_path(parent, format));
		return -1;
	}
	return 0;
}

/**
 * Given a field number and a path, add the corresponding field
 * to the tuple format, allocating intermediate fields if
 * necessary.
 *
 * Return a pointer to the leaf field on success, NULL on memory
 * allocation error or type/nullability mistmatch error, diag
 * message is set.
 */
static struct tuple_field *
tuple_format_add_field(struct tuple_format *format, uint32_t fieldno,
		       const char *path, uint32_t path_len, bool is_sequential,
		       int *current_slot, char **path_pool)
{
	struct tuple_field *field = NULL;
	struct tuple_field *parent = tuple_format_field(format, fieldno);
	assert(parent != NULL);
	if (path == NULL)
		goto end;
	field = tuple_field_new();
	if (field == NULL)
		return NULL;

	/*
	 * Retrieve path_len memory chunk from the path_pool and
	 * copy path data there. This is necessary in order to
	 * ensure that each new format::tuple_field refer format
	 * memory.
	 */
	memcpy(*path_pool, path, path_len);
	path = *path_pool;
	*path_pool += path_len;

	int rc = 0;
	uint32_t token_count = 0;
	bool is_multikey = false;
	struct json_tree *tree = &format->fields;
	struct json_lexer lexer;
	json_lexer_create(&lexer, path, path_len, TUPLE_INDEX_BASE);
	while ((rc = json_lexer_next_token(&lexer, &field->token)) == 0 &&
	       field->token.type != JSON_TOKEN_END) {
		if (tuple_field_ensure_child_compatibility(parent, field, format) != 0)
			goto fail;
		struct tuple_field *next =
			json_tree_lookup_entry(tree, &parent->token,
					       &field->token,
					       struct tuple_field, token);
		if (next == NULL) {
			field->id = format->total_field_count++;
			rc = json_tree_add(tree, &parent->token, &field->token);
			if (rc != 0) {
				diag_set(OutOfMemory, sizeof(struct json_token),
					 "json_tree_add", "tree");
				goto fail;
			}
			next = field;
			field = tuple_field_new();
			if (field == NULL)
				goto fail;
		}
		if (json_token_is_multikey(&parent->token)) {
			is_multikey = true;
			if (parent->offset_slot == TUPLE_OFFSET_SLOT_NIL) {
				/**
				 * Allocate offset slot for array
				 * is used in multikey index. This
				 * is required to quickly extract
				 * its size.
				 * @see tuple_multikey_count()
				 */
				assert(parent->type == FIELD_TYPE_ARRAY);
				*current_slot = *current_slot - 1;
				parent->offset_slot = *current_slot;
			}
		}
		parent->is_key_part = true;
		next->is_multikey_part = is_multikey;
		parent = next;
		token_count++;
	}
	/*
	 * The path has already been verified by the
	 * key_def_decode_parts function.
	 */
	assert(rc == 0 && field->token.type == JSON_TOKEN_END);
	assert(parent != NULL);
	/* Update tree depth information. */
	format->fields_depth = MAX(format->fields_depth, token_count + 1);
	tuple_field_delete(field);
end:
	/*
	 * In the tuple, store only offsets necessary to access
	 * fields of non-sequential keys. First field is always
	 * simply accessible, so we don't store an offset for it.
	 */
	if (parent->offset_slot == TUPLE_OFFSET_SLOT_NIL &&
	    is_sequential == false && (fieldno > 0 || path != NULL)) {
		*current_slot = *current_slot - 1;
		parent->offset_slot = *current_slot;
	}
	return parent;
fail:
	if (field != NULL)
		tuple_field_delete(field);
	return NULL;
}

static int
tuple_format_use_key_part(struct tuple_format *format, uint32_t field_count,
			  const struct key_part *part, bool is_sequential,
			  int *current_slot, char **path_pool)
{
	assert(part->fieldno < tuple_format_field_count(format));
	struct tuple_field *field =
		tuple_format_add_field(format, part->fieldno, part->path,
				       part->path_len, is_sequential,
				       current_slot, path_pool);
	if (field == NULL)
		return -1;
	/*
	 * If a field is not present in the space format, inherit
	 * nullable action of the first key part referencing it.
	 */
	if (part->fieldno >= field_count && !field->is_key_part)
		field->nullable_action = part->nullable_action;
	/*
	 * Field and part nullable actions may differ only
	 * if one of them is DEFAULT, in which case we use
	 * the non-default action *except* the case when
	 * the other one is NONE, in which case we assume
	 * DEFAULT. The latter is needed so that in case
	 * index definition and space format have different
	 * is_nullable flag, we will use the strictest option,
	 * i.e. DEFAULT.
	 */
	if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
		if (part->nullable_action != ON_CONFLICT_ACTION_NONE ||
		    part->path != NULL)
			field->nullable_action = part->nullable_action;
	} else if (part->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
		if (field->nullable_action == ON_CONFLICT_ACTION_NONE)
			field->nullable_action = part->nullable_action;
	} else if (field->nullable_action != part->nullable_action) {
		diag_set(ClientError, ER_ACTION_MISMATCH,
			 tuple_field_path(field, format),
			 on_conflict_action_strs[field->nullable_action],
			 on_conflict_action_strs[part->nullable_action]);
		return -1;
	}

	/**
	 * Check that there are no conflicts between index part
	 * types and space fields. If a part type is compatible
	 * with field's one, then the part type is more strict
	 * and the part type must be used in tuple_format.
	 */
	if (field_type1_contains_type2(field->type,
					part->type)) {
		field->type = part->type;
	} else if (!field_type1_contains_type2(part->type,
					       field->type)) {
		int errcode;
		if (!field->is_key_part)
			errcode = ER_FORMAT_MISMATCH_INDEX_PART;
		else
			errcode = ER_INDEX_PART_TYPE_MISMATCH;
		diag_set(ClientError, errcode, tuple_field_path(field, format),
			 field_type_strs[field->type],
			 field_type_strs[part->type]);
		return -1;
	}
	field->is_key_part = true;
	return 0;
}

/**
 * Extract all available type info from keys and field
 * definitions.
 */
static int
tuple_format_create(struct tuple_format *format, struct key_def * const *keys,
		    uint16_t key_count, const struct field_def *fields,
		    uint32_t field_count)
{
	format->min_field_count =
		tuple_format_min_field_count(keys, key_count, fields,
					     field_count);
	if (tuple_format_field_count(format) == 0) {
		format->field_map_size = 0;
		return 0;
	}
	/* Initialize defined fields */
	for (uint32_t i = 0; i < field_count; ++i) {
		struct tuple_field *field = tuple_format_field(format, i);
		field->type = fields[i].type;
		field->nullable_action = fields[i].nullable_action;
		struct coll *coll = NULL;
		uint32_t cid = fields[i].coll_id;
		if (cid != COLL_NONE) {
			struct coll_id *coll_id = coll_by_id(cid);
			if (coll_id == NULL) {
				diag_set(ClientError,ER_WRONG_COLLATION_OPTIONS,
					 i + 1, "collation was not found by ID");
				return -1;
			}
			coll = coll_id->coll;
		}
		field->coll = coll;
		field->coll_id = cid;
	}

	int current_slot = 0;

	/*
	 * Set pointer to reserved area in the format chunk
	 * allocated with tuple_format_alloc call.
	 */
	char *path_pool = (char *)format + sizeof(struct tuple_format);
	/* extract field type info */
	for (uint16_t key_no = 0; key_no < key_count; ++key_no) {
		const struct key_def *key_def = keys[key_no];
		if (key_def->for_func_index)
			continue;
		bool is_sequential = key_def_is_sequential(key_def);
		const struct key_part *part = key_def->parts;
		const struct key_part *parts_end = part + key_def->part_count;

		for (; part < parts_end; part++) {
			if (tuple_format_use_key_part(format, field_count, part,
						      is_sequential,
						      &current_slot,
						      &path_pool) != 0)
				return -1;
		}
	}

	assert(tuple_format_field(format, 0)->offset_slot == TUPLE_OFFSET_SLOT_NIL
	       || json_token_is_multikey(&tuple_format_field(format, 0)->token));
	size_t field_map_size = -current_slot * sizeof(uint32_t);
	if (field_map_size > INT16_MAX) {
		/** tuple->data_offset is 15 bits */
		diag_set(ClientError, ER_INDEX_FIELD_COUNT_LIMIT,
			 -current_slot);
		return -1;
	}
	format->field_map_size = field_map_size;

	size_t required_fields_sz = bitmap_size(format->total_field_count);
	format->required_fields = calloc(1, required_fields_sz);
	if (format->required_fields == NULL) {
		diag_set(OutOfMemory, required_fields_sz,
			 "malloc", "required field bitmap");
		return -1;
	}
	struct tuple_field *field;
	uint32_t *required_fields = format->required_fields;
	json_tree_foreach_entry_preorder(field, &format->fields.root,
					 struct tuple_field, token) {
		/*
		 * In the case of the multikey index,
		 * required_fields is overridden with local for
		 * the JSON_TOKEN_ANY field bitmask. Restore
		 * the main format->required_fields bitmask
		 * when leaving the multikey subtree,
		 */
		if (!field->is_multikey_part &&
		    required_fields != format->required_fields)
			required_fields = format->required_fields;
		/*
		 * Override required_fields with a new local
		 * bitmap for multikey index subtree.
		 */
		if (field->token.type == JSON_TOKEN_ANY) {
			assert(required_fields == format->required_fields);
			assert(field->multikey_required_fields == NULL);
			void *multikey_required_fields =
				calloc(1, required_fields_sz);
			if (multikey_required_fields == NULL) {
				diag_set(OutOfMemory, required_fields_sz,
					"malloc", "required field bitmap");
				return -1;
			}
			field->multikey_required_fields =
				multikey_required_fields;
			required_fields = multikey_required_fields;
		}
		/*
		 * Mark all leaf non-nullable fields as required
		 * by setting the corresponding bit in the bitmap
		 * of required fields.
		 */
		if (json_token_is_leaf(&field->token) &&
		    !tuple_field_is_nullable(field))
			bit_set(required_fields, field->id);
	}
	format->hash = tuple_format_hash(format);
	return 0;
}

static int
tuple_format_register(struct tuple_format *format)
{
	if (recycled_format_ids != FORMAT_ID_NIL) {

		format->id = (uint16_t) recycled_format_ids;
		recycled_format_ids = (intptr_t) tuple_formats[recycled_format_ids];
	} else {
		if (formats_size == formats_capacity) {
			uint32_t new_capacity = formats_capacity ?
						formats_capacity * 2 : 16;
			struct tuple_format **formats;
			formats = (struct tuple_format **)
				realloc(tuple_formats, new_capacity *
						       sizeof(tuple_formats[0]));
			if (formats == NULL) {
				diag_set(OutOfMemory,
					 sizeof(struct tuple_format), "malloc",
					 "tuple_formats");
				return -1;
			}

			formats_capacity = new_capacity;
			tuple_formats = formats;
		}
		uint32_t formats_size_max = FORMAT_ID_MAX + 1;
		struct errinj *inj = errinj(ERRINJ_TUPLE_FORMAT_COUNT,
					    ERRINJ_INT);
		if (inj != NULL && inj->iparam > 0)
			formats_size_max = inj->iparam;
		if (formats_size >= formats_size_max) {
			diag_set(ClientError, ER_TUPLE_FORMAT_LIMIT,
				 (unsigned) formats_capacity);
			return -1;
		}
		format->id = formats_size++;
	}
	tuple_formats[format->id] = format;
	return 0;
}

static void
tuple_format_deregister(struct tuple_format *format)
{
	if (format->id == FORMAT_ID_NIL)
		return;
	tuple_formats[format->id] = (struct tuple_format *) recycled_format_ids;
	recycled_format_ids = format->id;
	format->id = FORMAT_ID_NIL;
}

/*
 * Dismantle the tuple field tree attached to the format and free
 * memory occupied by tuple fields.
 */
static void
tuple_format_destroy_fields(struct tuple_format *format)
{
	struct tuple_field *field, *tmp;
	json_tree_foreach_entry_safe(field, &format->fields.root,
				     struct tuple_field, token, tmp) {
		json_tree_del(&format->fields, &field->token);
		tuple_field_delete(field);
	}
	json_tree_destroy(&format->fields);
}

static struct tuple_format *
tuple_format_alloc(struct key_def * const *keys, uint16_t key_count,
		   uint32_t space_field_count, struct tuple_dictionary *dict)
{
	/* Size of area to store JSON paths data. */
	uint32_t path_pool_size = 0;
	uint32_t index_field_count = 0;
	/* find max max field no */
	for (uint16_t key_no = 0; key_no < key_count; ++key_no) {
		const struct key_def *key_def = keys[key_no];
		if (key_def->for_func_index)
			continue;
		const struct key_part *part = key_def->parts;
		const struct key_part *pend = part + key_def->part_count;
		for (; part < pend; part++) {
			index_field_count = MAX(index_field_count,
						part->fieldno + 1);
			path_pool_size += part->path_len;
		}
	}
	uint32_t field_count = MAX(space_field_count, index_field_count);

	uint32_t allocation_size = sizeof(struct tuple_format) + path_pool_size;
	struct tuple_format *format = malloc(allocation_size);
	if (format == NULL) {
		diag_set(OutOfMemory, allocation_size, "malloc",
			 "tuple format");
		return NULL;
	}
	if (json_tree_create(&format->fields) != 0) {
		diag_set(OutOfMemory, 0, "json_lexer_create",
			 "tuple field tree");
		free(format);
		return NULL;
	}
	for (uint32_t fieldno = 0; fieldno < field_count; fieldno++) {
		struct tuple_field *field = tuple_field_new();
		if (field == NULL)
			goto error;
		field->id = fieldno;
		field->token.num = fieldno;
		field->token.type = JSON_TOKEN_NUM;
		if (json_tree_add(&format->fields, &format->fields.root,
				  &field->token) != 0) {
			diag_set(OutOfMemory, 0, "json_tree_add",
				 "tuple field tree entry");
			tuple_field_delete(field);
			goto error;
		}
	}
	if (dict == NULL) {
		assert(space_field_count == 0);
		format->dict = tuple_dictionary_new(NULL, 0);
		if (format->dict == NULL)
			goto error;
	} else {
		format->dict = dict;
		tuple_dictionary_ref(dict);
	}
	format->total_field_count = field_count;
	format->required_fields = NULL;
	format->fields_depth = 1;
	format->refs = 0;
	format->id = FORMAT_ID_NIL;
	format->index_field_count = index_field_count;
	format->exact_field_count = 0;
	format->min_field_count = 0;
	format->epoch = 0;
	return format;
error:
	tuple_format_destroy_fields(format);
	free(format);
	return NULL;
}

/** Free tuple format resources, doesn't unregister. */
static inline void
tuple_format_destroy(struct tuple_format *format)
{
	free(format->required_fields);
	tuple_format_destroy_fields(format);
	tuple_dictionary_unref(format->dict);
}

/**
 * Try to reuse given format. This is only possible for formats
 * of ephemeral spaces, since we need to be sure that shared
 * dictionary will never be altered. If it can, then alter can
 * affect another space, which shares a format with one which is
 * altered.
 *
 * The only way to change the format of the space is to recreate
 * space with the new format inside of BOX. Since there is no
 * mechanism for recreating the ephemeral space, we need not worry
 * about changing the format of the ephemeral space.
 *
 * @param p_format Double pointer to format. It is updated with
 * 		   hashed value, if corresponding format was found
 * 		   in hash table
 * @retval Returns true if format was found in hash table, false
 *	   otherwise.
 *
 */
static bool
tuple_format_reuse(struct tuple_format **p_format)
{
	struct tuple_format *format = *p_format;
	assert(format->is_ephemeral);
	assert(format->is_temporary);
	mh_int_t key = mh_tuple_format_find(tuple_formats_hash, format,
					    NULL);
	if (key != mh_end(tuple_formats_hash)) {
		struct tuple_format **entry = mh_tuple_format_node(
			tuple_formats_hash, key);
		tuple_format_destroy(format);
		free(format);
		*p_format = *entry;
		return true;
	}
	return false;
}

/**
 * See justification, why ephemeral space's formats are
 * only feasible for hasing.
 * @retval 0 on success, even if format wasn't added to hash
 * 	   -1 in case of error.
 */
static int
tuple_format_add_to_hash(struct tuple_format *format)
{
	assert(format->is_ephemeral);
	assert(format->is_temporary);
	mh_int_t key = mh_tuple_format_put(tuple_formats_hash,
					   (const struct tuple_format **)&format,
					   NULL, NULL);
	if (key == mh_end(tuple_formats_hash)) {
		diag_set(OutOfMemory, 0, "tuple_format_add_to_hash",
			 "tuple formats hash entry");
		return -1;
	}
	return 0;
}

static void
tuple_format_remove_from_hash(struct tuple_format *format)
{
	mh_int_t key = mh_tuple_format_find(tuple_formats_hash, format, NULL);
	if (key != mh_end(tuple_formats_hash))
		mh_tuple_format_del(tuple_formats_hash, key, NULL);
}

void
tuple_format_delete(struct tuple_format *format)
{
	tuple_format_remove_from_hash(format);
	tuple_format_deregister(format);
	tuple_format_destroy(format);
	free(format);
}

struct tuple_format *
tuple_format_new(struct tuple_format_vtab *vtab, void *engine,
		 struct key_def * const *keys, uint16_t key_count,
		 const struct field_def *space_fields,
		 uint32_t space_field_count, uint32_t exact_field_count,
		 struct tuple_dictionary *dict, bool is_temporary,
		 bool is_ephemeral)
{
	struct tuple_format *format =
		tuple_format_alloc(keys, key_count, space_field_count, dict);
	if (format == NULL)
		return NULL;
	if (vtab != NULL)
		format->vtab = *vtab;
	else
		memset(&format->vtab, 0, sizeof(format->vtab));
	format->engine = engine;
	format->is_temporary = is_temporary;
	format->is_ephemeral = is_ephemeral;
	format->exact_field_count = exact_field_count;
	format->epoch = ++formats_epoch;
	if (tuple_format_create(format, keys, key_count, space_fields,
				space_field_count) < 0)
		goto err;
	if (is_ephemeral && tuple_format_reuse(&format))
		return format;
	if (tuple_format_register(format) < 0)
		goto err;
	if (is_ephemeral && tuple_format_add_to_hash(format) < 0) {
		tuple_format_deregister(format);
		goto err;
	}
	return format;
err:
	tuple_format_destroy(format);
	free(format);
	return NULL;
}

bool
tuple_format1_can_store_format2_tuples(struct tuple_format *format1,
				       struct tuple_format *format2)
{
	if (format1->exact_field_count != format2->exact_field_count)
		return false;
	struct tuple_field *field1;
	json_tree_foreach_entry_preorder(field1, &format1->fields.root,
					 struct tuple_field, token) {
		struct tuple_field *field2 =
			tuple_format1_field_by_format2_field(format2, field1);
		/*
		 * The field has a data type in format1, but has
		 * no data type in format2.
		 */
		if (field2 == NULL) {
			/*
			 * The field can get a name added
			 * for it, and this doesn't require a data
			 * check.
			 * If the field is defined as not
			 * nullable, however, we need a data
			 * check, since old data may contain
			 * NULLs or miss the subject field.
			 */
			if (field1->type == FIELD_TYPE_ANY &&
			    tuple_field_is_nullable(field1))
				continue;
			else
				return false;
		}
		if (! field_type1_contains_type2(field1->type, field2->type))
			return false;
		/*
		 * Do not allow transition from nullable to non-nullable:
		 * it would require a check of all data in the space.
		 */
		if (tuple_field_is_nullable(field2) &&
		    !tuple_field_is_nullable(field1))
			return false;
	}
	return true;
}

static int
tuple_format_required_fields_validate(struct tuple_format *format,
				      void *required_fields,
				      uint32_t required_fields_sz);

static int
tuple_field_map_create_plain(struct tuple_format *format, const char *tuple,
			     bool validate, struct field_map_builder *builder)
{
	struct region *region = &fiber()->gc;
	const char *pos = tuple;
	uint32_t defined_field_count = mp_decode_array(&pos);
	if (validate && format->exact_field_count > 0 &&
	    format->exact_field_count != defined_field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) defined_field_count,
			 (unsigned) format->exact_field_count);
		return -1;
	}
	defined_field_count = MIN(defined_field_count,
				  tuple_format_field_count(format));

	void *required_fields = NULL;
	uint32_t required_fields_sz = bitmap_size(format->total_field_count);

	if (unlikely(defined_field_count == 0)) {
		required_fields = format->required_fields;
		goto end;
	}

	if (validate) {
		required_fields = region_alloc(region, required_fields_sz);
		memcpy(required_fields, format->required_fields,
		       required_fields_sz);
	}

	struct tuple_field *field;
	struct json_token **token = format->fields.root.children;
	for (uint32_t i = 0; i < defined_field_count; i++, token++, mp_next(&pos)) {
		field = json_tree_entry(*token, struct tuple_field, token);
		if (validate) {
			bool nullable = tuple_field_is_nullable(field);
			if(!field_mp_type_is_compatible(field->type, pos,
							nullable)) {
				diag_set(ClientError, ER_FIELD_TYPE,
					 tuple_field_path(field, format),
					 field_type_strs[field->type],
					 mp_type_strs[mp_typeof(*pos)]);
				return -1;
			}
			bit_clear(required_fields, field->id);
		}
		if (field->offset_slot != TUPLE_OFFSET_SLOT_NIL &&
		    field_map_builder_set_slot(builder, field->offset_slot,
					       pos - tuple, MULTIKEY_NONE,
					       0, NULL) != 0) {
			return -1;
		}
	}

end:
	if (!validate)
		return 0;

	return tuple_format_required_fields_validate(format, required_fields,
						     required_fields_sz);
}

/** @sa declaration for details. */
int
tuple_field_map_create(struct tuple_format *format, const char *tuple,
		       bool validate, struct field_map_builder *builder)
{
	struct region *region = &fiber()->gc;
	if (field_map_builder_create(builder, format->field_map_size,
				     region) != 0)
		return -1;
	if (tuple_format_field_count(format) == 0)
		return 0; /* Nothing to initialize */

	/*
	 * In case tuple format doesn't contain fields accessed by JSON paths,
	 * tuple field traversal may be simplified.
	 */
	if (format->fields_depth == 1) {
		return tuple_field_map_create_plain(format, tuple, validate,
						    builder);
	}

	uint32_t field_count;
	struct tuple_format_iterator it;
	uint8_t flags = validate ? TUPLE_FORMAT_ITERATOR_VALIDATE : 0;
	if (tuple_format_iterator_create(&it, format, tuple, flags,
					 &field_count, region) != 0)
		return -1;
	struct tuple_format_iterator_entry entry;
	while (tuple_format_iterator_next(&it, &entry) == 0 &&
	       entry.data != NULL) {
		if (entry.field == NULL)
			continue;
		if (entry.field->offset_slot != TUPLE_OFFSET_SLOT_NIL &&
		    field_map_builder_set_slot(builder, entry.field->offset_slot,
					entry.data - tuple, entry.multikey_idx,
					entry.multikey_count, region) != 0)
			return -1;
	}
	return entry.data == NULL ? 0 : -1;
}

uint32_t
tuple_format_min_field_count(struct key_def * const *keys, uint16_t key_count,
			     const struct field_def *space_fields,
			     uint32_t space_field_count)
{
	uint32_t min_field_count = 0;
	for (uint32_t i = 0; i < space_field_count; ++i) {
		if (! space_fields[i].is_nullable)
			min_field_count = i + 1;
	}
	for (uint32_t i = 0; i < key_count; ++i) {
		const struct key_def *kd = keys[i];
		for (uint32_t j = 0; j < kd->part_count; ++j) {
			const struct key_part *kp = &kd->parts[j];
			if (!key_part_is_nullable(kp) &&
			    kp->fieldno + 1 > min_field_count)
				min_field_count = kp->fieldno + 1;
		}
	}
	return min_field_count;
}

int
tuple_format_init()
{
	tuple_formats_hash = mh_tuple_format_new();
	if (tuple_formats_hash == NULL) {
		diag_set(OutOfMemory, sizeof(struct mh_tuple_format_t), "malloc",
			 "tuple format hash");
		return -1;
	}
	return 0;
}

/** Destroy tuple format subsystem and free resourses */
void
tuple_format_free()
{
	/* Clear recycled ids. */
	while (recycled_format_ids != FORMAT_ID_NIL) {
		uint16_t id = (uint16_t) recycled_format_ids;
		recycled_format_ids = (intptr_t) tuple_formats[id];
		tuple_formats[id] = NULL;
	}
	for (struct tuple_format **format = tuple_formats;
	     format < tuple_formats + formats_size; format++) {
		/* Do not unregister. Only free resources. */
		if (*format != NULL) {
			tuple_format_destroy(*format);
			free(*format);
		}
	}
	free(tuple_formats);
	mh_tuple_format_delete(tuple_formats_hash);
}

void
box_tuple_format_ref(box_tuple_format_t *format)
{
	tuple_format_ref(format);
}

void
box_tuple_format_unref(box_tuple_format_t *format)
{
	tuple_format_unref(format);
}

int
tuple_format_iterator_create(struct tuple_format_iterator *it,
			     struct tuple_format *format, const char *tuple,
			     uint8_t flags, uint32_t *defined_field_count,
			     struct region *region)
{
	assert(mp_typeof(*tuple) == MP_ARRAY);
	*defined_field_count = mp_decode_array(&tuple);
	bool validate = flags & TUPLE_FORMAT_ITERATOR_VALIDATE;
	if (validate && format->exact_field_count > 0 &&
	    format->exact_field_count != *defined_field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) *defined_field_count,
			 (unsigned) format->exact_field_count);
		return -1;
	}
	it->parent = &format->fields.root;
	it->format = format;
	it->pos = tuple;
	it->flags = flags;
	it->multikey_frame = NULL;
	it->required_fields = NULL;
	it->multikey_required_fields = NULL;
	it->required_fields_sz = 0;

	uint32_t frames_sz = format->fields_depth * sizeof(struct mp_frame);
	if (validate)
		it->required_fields_sz = bitmap_size(format->total_field_count);
	uint32_t total_sz = frames_sz + 2 * it->required_fields_sz;
	struct mp_frame *frames = region_aligned_alloc(region, total_sz,
						       alignof(frames[0]));
	if (frames == NULL) {
		diag_set(OutOfMemory, total_sz, "region",
			 "tuple_format_iterator");
		return -1;
	}
	mp_stack_create(&it->stack, format->fields_depth, frames);
	bool key_parts_only =
		(flags & TUPLE_FORMAT_ITERATOR_KEY_PARTS_ONLY) != 0;
	*defined_field_count = MIN(*defined_field_count, key_parts_only ?
				   format->index_field_count :
				   tuple_format_field_count(format));
	mp_stack_push(&it->stack, MP_ARRAY, *defined_field_count);

	if (validate) {
		it->required_fields = (char *)frames + frames_sz;
		memcpy(it->required_fields, format->required_fields,
		       it->required_fields_sz);
		it->multikey_required_fields =
			(char *)frames + frames_sz + it->required_fields_sz;
	}
	return 0;
}

/**
 * Scan required_fields bitmap and raise error when it is
 * non-empty.
 * @sa format:required_fields and field:multikey_required_fields
 * definition.
 */
static int
tuple_format_required_fields_validate(struct tuple_format *format,
				      void *required_fields,
				      uint32_t required_fields_sz)
{
	struct bit_iterator it;
	bit_iterator_init(&it, required_fields, required_fields_sz, true);
	size_t id = bit_iterator_next(&it);
	if (id < SIZE_MAX) {
		/* A field is missing, report an error. */
		struct tuple_field *field =
			tuple_format_field_by_id(format, id);
		assert(field != NULL);
		diag_set(ClientError, ER_FIELD_MISSING,
			 tuple_field_path(field, format));
		return -1;
	}
	return 0;
}

int
tuple_format_iterator_next(struct tuple_format_iterator *it,
			   struct tuple_format_iterator_entry *entry)
{
	entry->data = it->pos;
	struct mp_frame *frame = mp_stack_top(&it->stack);
	while (!mp_frame_advance(frame)) {
		/*
		 * If the elements of the current frame
		 * are over, pop this frame out of stack
		 * and climb one position in the format::fields
		 * tree to match the changed JSON path to the
		 * data in the tuple.
		 */
		mp_stack_pop(&it->stack);
		if (mp_stack_is_empty(&it->stack))
			goto eof;
		frame = mp_stack_top(&it->stack);
		if (json_token_is_multikey(it->parent)) {
			/*
			 * All multikey index entries have been
			 * processed. Reset pointer to the
			 * corresponding multikey frame.
			 */
			it->multikey_frame = NULL;
		}
		it->parent = it->parent->parent;
		if (json_token_is_multikey(it->parent)) {
			/*
			 * Processing of next multikey index
			 * format:subtree has finished, test if
			 * all required fields are present.
			 */
			if (it->flags & TUPLE_FORMAT_ITERATOR_VALIDATE &&
			    tuple_format_required_fields_validate(it->format,
						it->multikey_required_fields,
						it->required_fields_sz) != 0)
				return -1;
		}
	}
	entry->parent =
		it->parent != &it->format->fields.root ?
		json_tree_entry(it->parent, struct tuple_field, token) : NULL;
	/*
	 * Use the top frame of the stack and the
	 * current data offset to prepare the JSON token
	 * and perform subsequent format::fields lookup.
	 */
	struct json_token token;
	switch (frame->type) {
	case MP_ARRAY:
		token.type = JSON_TOKEN_NUM;
		token.num = frame->idx;
		break;
	case MP_MAP:
		if (mp_typeof(*it->pos) != MP_STR) {
			entry->field = NULL;
			mp_next(&it->pos);
			entry->data = it->pos;
			mp_next(&it->pos);
			entry->data_end = it->pos;
			return 0;
		}
		token.type = JSON_TOKEN_STR;
		token.str = mp_decode_str(&it->pos, (uint32_t *)&token.len);
		break;
	default:
		unreachable();
	}
	assert(it->parent != NULL);
	struct tuple_field *field =
		json_tree_lookup_entry(&it->format->fields, it->parent, &token,
				       struct tuple_field, token);
	if (it->flags & TUPLE_FORMAT_ITERATOR_KEY_PARTS_ONLY &&
	    field != NULL && !field->is_key_part)
		field = NULL;
	entry->field = field;
	entry->data = it->pos;
	if (it->multikey_frame != NULL) {
		entry->multikey_count = it->multikey_frame->count;
		entry->multikey_idx = it->multikey_frame->idx;
	} else {
		entry->multikey_count = 0;
		entry->multikey_idx = MULTIKEY_NONE;
	}
	/*
	 * If the current position of the data in tuple
	 * matches the container type (MP_MAP or MP_ARRAY)
	 * and the format::fields tree has such a record,
	 * prepare a new stack frame because it needs to
	 * be analyzed in the next iterations.
	 */
	enum mp_type type = mp_typeof(*it->pos);
	if ((type == MP_ARRAY || type == MP_MAP) &&
	    !mp_stack_is_full(&it->stack) && field != NULL) {
		uint32_t size = type == MP_ARRAY ?
				mp_decode_array(&it->pos) :
				mp_decode_map(&it->pos);
		entry->count = size;
		mp_stack_push(&it->stack, type, size);
		if (json_token_is_multikey(&field->token)) {
			/**
			 * Keep a pointer to the frame that
			 * describes an array with index
			 * placeholder [*]. The "current" item
			 * of this frame matches the logical
			 * index of item in multikey index
			 * and is equal to multikey index
			 * comparison hint.
			 */
			it->multikey_frame = mp_stack_top(&it->stack);
		}
		it->parent = &field->token;
	} else {
		entry->count = 0;
		mp_next(&it->pos);
	}
	entry->data_end = it->pos;
	if (field == NULL || (it->flags & TUPLE_FORMAT_ITERATOR_VALIDATE) == 0)
		return 0;

	if (field->token.type == JSON_TOKEN_ANY) {
		/**
		 * Start processing of next multikey
		 * index element. Reset required_fields
		 * bitmap.
		 */
		assert(it->multikey_frame != NULL);
		assert(field->multikey_required_fields != NULL);
		memcpy(it->multikey_required_fields,
		       field->multikey_required_fields, it->required_fields_sz);
	}
	/*
	 * Check if field mp_type is compatible with type
	 * defined in format.
	 */
	bool is_nullable = tuple_field_is_nullable(field);
	if (!field_mp_type_is_compatible(field->type, entry->data, is_nullable) != 0) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 tuple_field_path(field, it->format),
			 field_type_strs[field->type],
			 mp_type_strs[mp_typeof(*entry->data)]);
		return -1;
	}
	bit_clear(it->multikey_frame != NULL ?
		  it->multikey_required_fields : it->required_fields, field->id);
	return 0;
eof:
	if (it->flags & TUPLE_FORMAT_ITERATOR_VALIDATE &&
	    tuple_format_required_fields_validate(it->format,
			it->required_fields, it->required_fields_sz) != 0)
		return -1;
	entry->data = NULL;
	return 0;
}
