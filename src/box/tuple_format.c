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

#include "third_party/PMurHash.h"

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
	return field;
}

static void
tuple_field_delete(struct tuple_field *field)
{
	free(field);
}

/** Return path to a tuple field. Used for error reporting. */
static const char *
tuple_field_path(const struct tuple_field *field)
{
	assert(field->token.parent != NULL);
	if (field->token.parent->parent == NULL) {
		/* Top-level field, no need to format the path. */
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
		       const char *path, uint32_t path_len, char **path_pool)
{
	struct tuple_field *field = NULL;
	struct tuple_field *parent = tuple_format_field(format, fieldno);
	assert(parent != NULL);
	if (path == NULL)
		return parent;
	field = tuple_field_new();
	if (field == NULL)
		goto fail;

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
	struct json_tree *tree = &format->fields;
	struct json_lexer lexer;
	json_lexer_create(&lexer, path, path_len, TUPLE_INDEX_BASE);
	while ((rc = json_lexer_next_token(&lexer, &field->token)) == 0 &&
	       field->token.type != JSON_TOKEN_END) {
		enum field_type expected_type =
			field->token.type == JSON_TOKEN_STR ?
			FIELD_TYPE_MAP : FIELD_TYPE_ARRAY;
		if (field_type1_contains_type2(parent->type, expected_type)) {
			parent->type = expected_type;
		} else {
			diag_set(ClientError, ER_INDEX_PART_TYPE_MISMATCH,
				 tuple_field_path(parent),
				 field_type_strs[parent->type],
				 field_type_strs[expected_type]);
			goto fail;
		}
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
		parent->is_key_part = true;
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
end:
	tuple_field_delete(field);
	return parent;
fail:
	parent = NULL;
	goto end;
}

static int
tuple_format_use_key_part(struct tuple_format *format, uint32_t field_count,
			  const struct key_part *part, bool is_sequential,
			  int *current_slot, char **path_pool)
{
	assert(part->fieldno < tuple_format_field_count(format));
	struct tuple_field *field =
		tuple_format_add_field(format, part->fieldno, part->path,
				       part->path_len, path_pool);
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
		if (part->nullable_action != ON_CONFLICT_ACTION_NONE)
			field->nullable_action = part->nullable_action;
	} else if (part->nullable_action == ON_CONFLICT_ACTION_DEFAULT) {
		if (field->nullable_action == ON_CONFLICT_ACTION_NONE)
			field->nullable_action = part->nullable_action;
	} else if (field->nullable_action != part->nullable_action) {
		diag_set(ClientError, ER_ACTION_MISMATCH,
			 tuple_field_path(field),
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
		diag_set(ClientError, errcode, tuple_field_path(field),
			 field_type_strs[field->type],
			 field_type_strs[part->type]);
		return -1;
	}
	field->is_key_part = true;
	/*
	 * In the tuple, store only offsets necessary to access
	 * fields of non-sequential keys. First field is always
	 * simply accessible, so we don't store an offset for it.
	 */
	if (field->offset_slot == TUPLE_OFFSET_SLOT_NIL &&
	    is_sequential == false &&
	    (part->fieldno > 0 || part->path != NULL)) {
		*current_slot = *current_slot - 1;
		field->offset_slot = *current_slot;
	}
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

	assert(tuple_format_field(format, 0)->offset_slot ==
	       TUPLE_OFFSET_SLOT_NIL);
	size_t field_map_size = -current_slot * sizeof(uint32_t);
	if (field_map_size > UINT16_MAX) {
		/** tuple->data_offset is 16 bits */
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
	format->min_tuple_size = mp_sizeof_array(format->index_field_count);
	struct tuple_field *field;
	json_tree_foreach_entry_preorder(field, &format->fields.root,
					 struct tuple_field, token) {
		/*
		 * Mark all leaf non-nullable fields as required
		 * by setting the corresponding bit in the bitmap
		 * of required fields.
		 */
		if (json_token_is_leaf(&field->token) &&
		    !tuple_field_is_nullable(field))
			bit_set(format->required_fields, field->id);

		/*
		 * Update format::min_tuple_size by field.
		 * Skip fields not involved in index.
		 */
		if (!field->is_key_part)
			continue;
		if (field->token.type == JSON_TOKEN_NUM) {
			/*
			 * Account a gap between omitted array
			 * items.
			 */
			struct json_token **neighbors =
				field->token.parent->children;
			for (int i = field->token.sibling_idx - 1; i >= 0; i--) {
				if (neighbors[i] != NULL &&
				    json_tree_entry(neighbors[i],
						    struct tuple_field,
						    token)->is_key_part)
					break;
				format->min_tuple_size += mp_sizeof_nil();
			}
		} else {
			/* Account a key string for map member. */
			assert(field->token.type == JSON_TOKEN_STR);
			format->min_tuple_size +=
				mp_sizeof_str(field->token.len);
		}
		int max_child_idx = field->token.max_child_idx;
		if (json_token_is_leaf(&field->token)) {
			format->min_tuple_size += mp_sizeof_nil();
		} else if (field->type == FIELD_TYPE_ARRAY) {
			format->min_tuple_size +=
				mp_sizeof_array(max_child_idx + 1);
		} else if (field->type == FIELD_TYPE_MAP) {
			format->min_tuple_size +=
				mp_sizeof_map(max_child_idx + 1);
		}
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
	format->min_tuple_size = 0;
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
	if (!format->is_ephemeral)
		return false;
	/*
	 * These fields do not participate in hashing.
	 * Make sure they're unset.
	 */
	assert(format->dict->name_count == 0);
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
	if(!format->is_ephemeral)
		return 0;
	assert(format->dict->name_count == 0);
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
	format->vtab = *vtab;
	format->engine = engine;
	format->is_temporary = is_temporary;
	format->is_ephemeral = is_ephemeral;
	format->exact_field_count = exact_field_count;
	format->epoch = ++formats_epoch;
	if (tuple_format_create(format, keys, key_count, space_fields,
				space_field_count) < 0)
		goto err;
	if (tuple_format_reuse(&format))
		return format;
	if (tuple_format_register(format) < 0)
		goto err;
	if (tuple_format_add_to_hash(format) < 0) {
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

/** @sa declaration for details. */
int
tuple_init_field_map(struct tuple_format *format, uint32_t *field_map,
		     const char *tuple, bool validate)
{
	if (tuple_format_field_count(format) == 0)
		return 0; /* Nothing to initialize */

	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	const char *pos = tuple;
	int rc = 0;

	/* Check to see if the tuple has a sufficient number of fields. */
	uint32_t field_count = mp_decode_array(&pos);
	if (validate && format->exact_field_count > 0 &&
	    format->exact_field_count != field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->exact_field_count);
		goto error;
	}
	/*
	 * Allocate a field bitmap that will be used for checking
	 * that all mandatory fields are present.
	 */
	void *required_fields = NULL;
	size_t required_fields_sz = 0;
	if (validate) {
		required_fields_sz = bitmap_size(format->total_field_count);
		required_fields = region_alloc(region, required_fields_sz);
		if (required_fields == NULL) {
			diag_set(OutOfMemory, required_fields_sz,
				 "region", "required field bitmap");
			goto error;
		}
		memcpy(required_fields, format->required_fields,
		       required_fields_sz);
	}
	/*
	 * Initialize the tuple field map and validate field types.
	 */
	if (field_count == 0) {
		/* Empty tuple, nothing to do. */
		goto finish;
	}
	uint32_t defined_field_count = MIN(field_count, validate ?
					   tuple_format_field_count(format) :
					   format->index_field_count);
	/*
	 * Nullify field map to be able to detect by 0,
	 * which key fields are absent in tuple_field().
	 */
	memset((char *)field_map - format->field_map_size, 0,
		format->field_map_size);
	/*
	 * Prepare mp stack of the size equal to the maximum depth
	 * of the indexed field in the format::fields tree
	 * (fields_depth) to carry out a simultaneous parsing of
	 * the tuple and tree traversal to process type
	 * validations and field map initialization.
	 */
	uint32_t frames_sz = format->fields_depth * sizeof(struct mp_frame);
	struct mp_frame *frames = region_alloc(region, frames_sz);
	if (frames == NULL) {
		diag_set(OutOfMemory, frames_sz, "region", "frames");
		goto error;
	}
	struct mp_stack stack;
	mp_stack_create(&stack, format->fields_depth, frames);
	mp_stack_push(&stack, MP_ARRAY, defined_field_count);
	struct tuple_field *field;
	struct json_token *parent = &format->fields.root;
	while (true) {
		int idx;
		while ((idx = mp_stack_advance(&stack)) == -1) {
			/*
			 * If the elements of the current frame
			 * are over, pop this frame out of stack
			 * and climb one position in the
			 * format::fields tree to match the
			 * changed JSON path to the data in the
			 * tuple.
			 */
			mp_stack_pop(&stack);
			if (mp_stack_is_empty(&stack))
				goto finish;
			parent = parent->parent;
		}
		/*
		 * Use the top frame of the stack and the
		 * current data offset to prepare the JSON token
		 * for the subsequent format::fields lookup.
		 */
		struct json_token token;
		switch (mp_stack_type(&stack)) {
		case MP_ARRAY:
			token.type = JSON_TOKEN_NUM;
			token.num = idx;
			break;
		case MP_MAP:
			if (mp_typeof(*pos) != MP_STR) {
				/*
				 * JSON path support only string
				 * keys for map. Skip this entry.
				 */
				mp_next(&pos);
				mp_next(&pos);
				continue;
			}
			token.type = JSON_TOKEN_STR;
			token.str = mp_decode_str(&pos, (uint32_t *)&token.len);
			break;
		default:
			unreachable();
		}
		/*
		 * Perform lookup for a field in format::fields,
		 * that represents the field metadata by JSON path
		 * corresponding to the current position in the
		 * tuple.
		 */
		enum mp_type type = mp_typeof(*pos);
		assert(parent != NULL);
		field = json_tree_lookup_entry(&format->fields, parent, &token,
					       struct tuple_field, token);
		if (field != NULL) {
			bool is_nullable = tuple_field_is_nullable(field);
			if (validate &&
			    !field_mp_type_is_compatible(field->type, type,
							 is_nullable) != 0) {
				diag_set(ClientError, ER_FIELD_TYPE,
					 tuple_field_path(field),
					 field_type_strs[field->type]);
				goto error;
			}
			if (field->offset_slot != TUPLE_OFFSET_SLOT_NIL)
				field_map[field->offset_slot] = pos - tuple;
			if (required_fields != NULL)
				bit_clear(required_fields, field->id);
		}
		/*
		 * If the current position of the data in tuple
		 * matches the container type (MP_MAP or MP_ARRAY)
		 * and the format::fields tree has such a record,
		 * prepare a new stack frame because it needs to
		 * be analyzed in the next iterations.
		 */
		if ((type == MP_ARRAY || type == MP_MAP) &&
		    !mp_stack_is_full(&stack) && field != NULL) {
			uint32_t size = type == MP_ARRAY ?
					mp_decode_array(&pos) :
					mp_decode_map(&pos);
			mp_stack_push(&stack, type, size);
			parent = &field->token;
		} else {
			mp_next(&pos);
		}
	}
finish:
	/*
	 * Check the required field bitmap for missing fields.
	 */
	if (required_fields != NULL) {
		struct bit_iterator it;
		bit_iterator_init(&it, required_fields,
				  required_fields_sz, true);
		size_t id = bit_iterator_next(&it);
		if (id < SIZE_MAX) {
			/* A field is missing, report an error. */
			field = tuple_format_field_by_id(format, id);
			assert(field != NULL);
			diag_set(ClientError, ER_FIELD_MISSING,
				 tuple_field_path(field));
			goto error;
		}
	}
out:
	region_truncate(region, region_svp);
	return rc;
error:
	rc = -1;
	goto out;
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

