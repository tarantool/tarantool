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
#include "json/path.h"
#include "tuple_format.h"

/** Global table of tuple formats */
struct tuple_format **tuple_formats;
static intptr_t recycled_format_ids = FORMAT_ID_NIL;

static uint32_t formats_size = 0, formats_capacity = 0;

static const struct tuple_field tuple_field_default = {
	FIELD_TYPE_ANY, TUPLE_OFFSET_SLOT_NIL, false,
	ON_CONFLICT_ACTION_DEFAULT, NULL
};

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
	if (format->field_count == 0) {
		format->field_map_size = 0;
		return 0;
	}
	/* Initialize defined fields */
	for (uint32_t i = 0; i < field_count; ++i) {
		format->fields[i].is_key_part = false;
		format->fields[i].type = fields[i].type;
		format->fields[i].offset_slot = TUPLE_OFFSET_SLOT_NIL;
		format->fields[i].nullable_action = fields[i].nullable_action;
		struct coll *coll = NULL;
		uint32_t coll_id = fields[i].coll_id;
		if (coll_id != COLL_NONE) {
			coll = coll_by_id(coll_id);
			if (coll == NULL) {
				diag_set(ClientError,ER_WRONG_COLLATION_OPTIONS,
					 i + 1, "collation was not found by ID");
				return -1;
			}
		}
		format->fields[i].coll = coll;
	}
	/* Initialize remaining fields */
	for (uint32_t i = field_count; i < format->field_count; i++)
		format->fields[i] = tuple_field_default;

	int current_slot = 0;

	/* extract field type info */
	for (uint16_t key_no = 0; key_no < key_count; ++key_no) {
		const struct key_def *key_def = keys[key_no];
		bool is_sequential = key_def_is_sequential(key_def);
		const struct key_part *part = key_def->parts;
		const struct key_part *parts_end = part + key_def->part_count;

		for (; part < parts_end; part++) {
			assert(part->fieldno < format->field_count);
			struct tuple_field *field =
				&format->fields[part->fieldno];
			if (part->fieldno >= field_count) {
				field->nullable_action = part->nullable_action;
			} else {
				if (tuple_field_is_nullable(field) !=
				    key_part_is_nullable(part)) {
					diag_set(ClientError,
						 ER_NULLABLE_MISMATCH,
						 part->fieldno +
						 TUPLE_INDEX_BASE,
						 tuple_field_is_nullable(field) ?
						 "nullable" : "not nullable",
						 key_part_is_nullable(part) ?
						 "nullable" : "not nullable");
					return -1;
				}

				if (field->nullable_action == ON_CONFLICT_ACTION_DEFAULT &&
				    !(part->nullable_action == ON_CONFLICT_ACTION_NONE ||
				      part->nullable_action == ON_CONFLICT_ACTION_DEFAULT))
					field->nullable_action = part->nullable_action;
				else {
					if (field->nullable_action != part->nullable_action &&
					    part->nullable_action != ON_CONFLICT_ACTION_DEFAULT) {
						int action_f = field->nullable_action;
						int action_p = part->nullable_action;
						diag_set(ClientError,
							 ER_ACTION_MISMATCH,
							 part->fieldno +
							 TUPLE_INDEX_BASE,
							 on_conflict_action_strs[action_f],
							 on_conflict_action_strs[action_p]);
						return -1;
					}
				}
			}

			/*
			 * Check that there are no conflicts
			 * between index part types and space
			 * fields. If a part type is compatible
			 * with field's one, then the part type is
			 * more strict and the part type must be
			 * used in tuple_format.
			 */
			if (field_type1_contains_type2(field->type,
						       part->type)) {
				field->type = part->type;
			} else if (! field_type1_contains_type2(part->type,
								field->type)) {
				const char *name;
				int fieldno = part->fieldno + TUPLE_INDEX_BASE;
				if (part->fieldno >= field_count) {
					name = tt_sprintf("%d", fieldno);
				} else {
					const struct field_def *def =
						&fields[part->fieldno];
					name = tt_sprintf("'%s'", def->name);
				}
				int errcode;
				if (! field->is_key_part)
					errcode = ER_FORMAT_MISMATCH_INDEX_PART;
				else
					errcode = ER_INDEX_PART_TYPE_MISMATCH;
				diag_set(ClientError, errcode, name,
					 field_type_strs[field->type],
					 field_type_strs[part->type]);
				return -1;
			}
			field->is_key_part = true;
			/*
			 * In the tuple, store only offsets necessary
			 * to access fields of non-sequential keys.
			 * First field is always simply accessible,
			 * so we don't store an offset for it.
			 */
			if (field->offset_slot == TUPLE_OFFSET_SLOT_NIL &&
			    is_sequential == false && part->fieldno > 0) {

				field->offset_slot = --current_slot;
			}
		}
	}

	assert(format->fields[0].offset_slot == TUPLE_OFFSET_SLOT_NIL);
	size_t field_map_size = -current_slot * sizeof(uint32_t);
	if (field_map_size + format->extra_size > UINT16_MAX) {
		/** tuple->data_offset is 16 bits */
		diag_set(ClientError, ER_INDEX_FIELD_COUNT_LIMIT,
			 -current_slot);
		return -1;
	}
	format->field_map_size = field_map_size;
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
		if (formats_size == FORMAT_ID_MAX + 1) {
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

static struct tuple_format *
tuple_format_alloc(struct key_def * const *keys, uint16_t key_count,
		   uint32_t space_field_count, struct tuple_dictionary *dict)
{
	uint32_t index_field_count = 0;
	/* find max max field no */
	for (uint16_t key_no = 0; key_no < key_count; ++key_no) {
		const struct key_def *key_def = keys[key_no];
		const struct key_part *part = key_def->parts;
		const struct key_part *pend = part + key_def->part_count;
		for (; part < pend; part++) {
			index_field_count = MAX(index_field_count,
						part->fieldno + 1);
		}
	}
	uint32_t field_count = MAX(space_field_count, index_field_count);
	uint32_t total = sizeof(struct tuple_format) +
			 field_count * sizeof(struct tuple_field);

	struct tuple_format *format = (struct tuple_format *) malloc(total);
	if (format == NULL) {
		diag_set(OutOfMemory, sizeof(struct tuple_format), "malloc",
			 "tuple format");
		return NULL;
	}
	if (dict == NULL) {
		assert(space_field_count == 0);
		format->dict = tuple_dictionary_new(NULL, 0);
		if (format->dict == NULL) {
			free(format);
			return NULL;
		}
	} else {
		format->dict = dict;
		tuple_dictionary_ref(dict);
	}
	format->refs = 0;
	format->id = FORMAT_ID_NIL;
	format->field_count = field_count;
	format->index_field_count = index_field_count;
	format->exact_field_count = 0;
	format->min_field_count = 0;
	return format;
}

/** Free tuple format resources, doesn't unregister. */
static inline void
tuple_format_destroy(struct tuple_format *format)
{
	tuple_dictionary_unref(format->dict);
}

void
tuple_format_delete(struct tuple_format *format)
{
	tuple_format_deregister(format);
	tuple_format_destroy(format);
	free(format);
}

struct tuple_format *
tuple_format_new(struct tuple_format_vtab *vtab, struct key_def * const *keys,
		 uint16_t key_count, uint16_t extra_size,
		 const struct field_def *space_fields,
		 uint32_t space_field_count, struct tuple_dictionary *dict)
{
	struct tuple_format *format =
		tuple_format_alloc(keys, key_count, space_field_count, dict);
	if (format == NULL)
		return NULL;
	format->vtab = *vtab;
	format->extra_size = extra_size;
	if (tuple_format_register(format) < 0) {
		tuple_format_destroy(format);
		free(format);
		return NULL;
	}
	if (tuple_format_create(format, keys, key_count, space_fields,
				space_field_count) < 0) {
		tuple_format_delete(format);
		return NULL;
	}
	return format;
}

bool
tuple_format1_can_store_format2_tuples(const struct tuple_format *format1,
				       const struct tuple_format *format2)
{
	if (format1->exact_field_count != format2->exact_field_count)
		return false;
	for (uint32_t i = 0; i < format1->field_count; ++i) {
		const struct tuple_field *field1 = &format1->fields[i];
		/*
		 * The field has a data type in format1, but has
		 * no data type in format2.
		 */
		if (i >= format2->field_count) {
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
		const struct tuple_field *field2 = &format2->fields[i];
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

struct tuple_format *
tuple_format_dup(struct tuple_format *src)
{
	uint32_t total = sizeof(struct tuple_format) +
			 src->field_count * sizeof(struct tuple_field);
	struct tuple_format *format = (struct tuple_format *) malloc(total);
	if (format == NULL) {
		diag_set(OutOfMemory, total, "malloc", "tuple format");
		return NULL;
	}
	memcpy(format, src, total);
	tuple_dictionary_ref(format->dict);
	format->id = FORMAT_ID_NIL;
	format->refs = 0;
	if (tuple_format_register(format) != 0) {
		tuple_format_destroy(format);
		free(format);
		return NULL;
	}
	return format;
}

/** @sa declaration for details. */
int
tuple_init_field_map(const struct tuple_format *format, uint32_t *field_map,
		     const char *tuple)
{
	if (format->field_count == 0)
		return 0; /* Nothing to initialize */

	const char *pos = tuple;

	/* Check to see if the tuple has a sufficient number of fields. */
	uint32_t field_count = mp_decode_array(&pos);
	if (format->exact_field_count > 0 &&
	    format->exact_field_count != field_count) {
		diag_set(ClientError, ER_EXACT_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->exact_field_count);
		return -1;
	}
	if (unlikely(field_count < format->min_field_count)) {
		diag_set(ClientError, ER_MIN_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->min_field_count);
		return -1;
	}

	/* first field is simply accessible, so we do not store offset to it */
	enum mp_type mp_type = mp_typeof(*pos);
	const struct tuple_field *field = &format->fields[0];
	if (key_mp_type_validate(field->type, mp_type, ER_FIELD_TYPE,
				 TUPLE_INDEX_BASE, tuple_field_is_nullable(field)))
		return -1;
	mp_next(&pos);
	/* other fields...*/
	++field;
	uint32_t i = 1;
	uint32_t defined_field_count = MIN(field_count, format->field_count);
	if (field_count < format->index_field_count) {
		/*
		 * Nullify field map to be able to detect by 0,
		 * which key fields are absent in tuple_field().
		 */
		memset((char *)field_map - format->field_map_size, 0,
		       format->field_map_size);
	}
	for (; i < defined_field_count; ++i, ++field) {
		mp_type = mp_typeof(*pos);
		if (key_mp_type_validate(field->type, mp_type, ER_FIELD_TYPE,
					 i + TUPLE_INDEX_BASE,
					 tuple_field_is_nullable(field)))
			return -1;
		if (field->offset_slot != TUPLE_OFFSET_SLOT_NIL) {
			field_map[field->offset_slot] =
				(uint32_t) (pos - tuple);
		}
		mp_next(&pos);
	}
	return 0;
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

/**
 * Propagate @a field to MessagePack(field)[index].
 * @param[in][out] field Field to propagate.
 * @param index 1-based index to propagate to.
 *
 * @retval  0 Success, the index was found.
 * @retval -1 Not found.
 */
static inline int
tuple_field_go_to_index(const char **field, uint64_t index)
{
	enum mp_type type = mp_typeof(**field);
	if (type == MP_ARRAY) {
		if (index == 0)
			return -1;
		/* Make index 0-based. */
		index -= TUPLE_INDEX_BASE;
		uint32_t count = mp_decode_array(field);
		if (index >= count)
			return -1;
		for (; index > 0; --index)
			mp_next(field);
		return 0;
	} else if (type == MP_MAP) {
		uint64_t count = mp_decode_map(field);
		for (; count > 0; --count) {
			type = mp_typeof(**field);
			if (type == MP_UINT) {
				uint64_t value = mp_decode_uint(field);
				if (value == index)
					return 0;
			} else if (type == MP_INT) {
				int64_t value = mp_decode_int(field);
				if (value >= 0 && (uint64_t)value == index)
					return 0;
			} else {
				/* Skip key. */
				mp_next(field);
			}
			/* Skip value. */
			mp_next(field);
		}
	}
	return -1;
}

/**
 * Propagate @a field to MessagePack(field)[key].
 * @param[in][out] field Field to propagate.
 * @param key Key to propagate to.
 * @param len Length of @a key.
 *
 * @retval  0 Success, the index was found.
 * @retval -1 Not found.
 */
static inline int
tuple_field_go_to_key(const char **field, const char *key, int len)
{
	enum mp_type type = mp_typeof(**field);
	if (type != MP_MAP)
		return -1;
	uint64_t count = mp_decode_map(field);
	for (; count > 0; --count) {
		type = mp_typeof(**field);
		if (type == MP_STR) {
			uint32_t value_len;
			const char *value = mp_decode_str(field, &value_len);
			if (value_len == (uint)len &&
			    memcmp(value, key, len) == 0)
				return 0;
		} else {
			/* Skip key. */
			mp_next(field);
		}
		/* Skip value. */
		mp_next(field);
	}
	return -1;
}

int
tuple_field_raw_by_path(struct tuple_format *format, const char *tuple,
                        const uint32_t *field_map, const char *path,
                        uint32_t path_len, uint32_t path_hash,
                        const char **field)
{
	assert(path_len > 0);
	uint32_t fieldno;
	/*
	 * It is possible, that a field has a name as
	 * well-formatted JSON. For example 'a.b.c.d' or '[1]' can
	 * be field name. To save compatibility at first try to
	 * use the path as a field name.
	 */
	if (tuple_fieldno_by_name(format->dict, path, path_len, path_hash,
				  &fieldno) == 0) {
		*field = tuple_field_raw(format, tuple, field_map, fieldno);
		return 0;
	}
	struct json_path_parser parser;
	struct json_path_node node;
	json_path_parser_create(&parser, path, path_len);
	int rc = json_path_next(&parser, &node);
	if (rc != 0)
		goto error;
	switch(node.type) {
	case JSON_PATH_NUM: {
		int index = node.num;
		if (index == 0) {
			*field = NULL;
			return 0;
		}
		index -= TUPLE_INDEX_BASE;
		*field = tuple_field_raw(format, tuple, field_map, index);
		if (*field == NULL)
			return 0;
		break;
	}
	case JSON_PATH_STR: {
		/* First part of a path is a field name. */
		uint32_t name_hash;
		if (path_len == (uint32_t) node.len) {
			name_hash = path_hash;
		} else {
			/*
			 * If a string is "field....", then its
			 * precalculated juajit hash can not be
			 * used. A tuple dictionary hashes only
			 * name, not path.
			 */
			name_hash = field_name_hash(node.str, node.len);
		}
		*field = tuple_field_raw_by_name(format, tuple, field_map,
						 node.str, node.len, name_hash);
		if (*field == NULL)
			return 0;
		break;
	}
	default:
		assert(node.type == JSON_PATH_END);
		*field = NULL;
		return 0;
	}
	while ((rc = json_path_next(&parser, &node)) == 0) {
		switch(node.type) {
		case JSON_PATH_NUM:
			rc = tuple_field_go_to_index(field, node.num);
			break;
		case JSON_PATH_STR:
			rc = tuple_field_go_to_key(field, node.str, node.len);
			break;
		default:
			assert(node.type == JSON_PATH_END);
			return 0;
		}
		if (rc != 0) {
			*field = NULL;
			return 0;
		}
	}
error:
	assert(rc > 0);
	diag_set(ClientError, ER_ILLEGAL_PARAMS,
		 tt_sprintf("error in path on position %d", rc));
	return -1;
}
