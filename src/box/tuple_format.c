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
#include "tuple_format.h"

field_name_hash_f field_name_hash;

#define mh_name _strnu32
struct mh_strnu32_key_t {
	const char *str;
	size_t len;
	uint32_t hash;
};
#define mh_key_t struct mh_strnu32_key_t *
struct mh_strnu32_node_t {
	const char *str;
	size_t len;
	uint32_t hash;
	uint32_t val;
};
#define mh_node_t struct mh_strnu32_node_t

#define mh_arg_t void *
#define mh_hash(a, arg) ((a)->hash)
#define mh_hash_key(a, arg) mh_hash(a, arg)
#define mh_cmp(a, b, arg) ((a)->len != (b)->len || \
			   memcmp((a)->str, (b)->str, (a)->len))
#define mh_cmp_key(a, b, arg) mh_cmp(a, b, arg)
#define MH_SOURCE 1
#include "salad/mhash.h" /* Create mh_strnu32_t hash. */

/** Global table of tuple formats */
struct tuple_format **tuple_formats;
static intptr_t recycled_format_ids = FORMAT_ID_NIL;

static uint32_t formats_size = 0, formats_capacity = 0;

static const struct tuple_field tuple_field_default = {
	FIELD_TYPE_ANY, TUPLE_OFFSET_SLOT_NIL, false, NULL, false,
};

/**
 * Add @a name to a name hash of @a format.
 * @param format Format to add name.
 * @param name Name to add.
 * @param name_len Length of @a name.
 * @param fieldno Field number.
 * @param check_dup True, if need to check for duplicates.
 *
 * @retval -1 Duplicate field error.
 * @retval  0 Success.
 */
static inline int
tuple_format_add_name(struct tuple_format *format, const char *name,
		      uint32_t name_len, uint32_t fieldno, bool check_dup)
{
	uint32_t name_hash = field_name_hash(name, name_len);
	struct mh_strnu32_node_t name_node = {
		name, name_len, name_hash, fieldno
	};
	if (check_dup) {
		struct mh_strnu32_key_t key = {
			name, name_len, name_hash
		};
		mh_int_t rc = mh_strnu32_find(format->names, &key, NULL);
		if (rc != mh_end(format->names)) {
			diag_set(ClientError, ER_SPACE_FIELD_IS_DUPLICATE,
				 name);
			return -1;
		}
	}
	mh_int_t rc = mh_strnu32_put(format->names, &name_node, NULL, NULL);
	/* Memory was reserved in alloc(). */
	assert(rc != mh_end(format->names));
	(void) rc;
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
	if (format->field_count == 0) {
		format->field_map_size = 0;
		return 0;
	}
	char *name_pos = (char *)format + sizeof(*format) +
			 sizeof(struct tuple_field) * format->field_count;
	/* Initialize defined fields */
	for (uint32_t i = 0; i < field_count; ++i) {
		format->fields[i].is_key_part = false;
		format->fields[i].type = fields[i].type;
		format->fields[i].offset_slot = TUPLE_OFFSET_SLOT_NIL;
		format->fields[i].is_nullable = fields[i].is_nullable;
		format->fields[i].name = name_pos;
		size_t len = strlen(fields[i].name);
		memcpy(name_pos, fields[i].name, len);
		name_pos[len] = 0;
		if (tuple_format_add_name(format, name_pos, len, i, true) != 0)
			return -1;
		name_pos += len + 1;
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
				field->is_nullable = part->is_nullable;
			} else if (field->is_nullable != part->is_nullable) {
				diag_set(ClientError, ER_NULLABLE_MISMATCH,
					 part->fieldno + TUPLE_INDEX_BASE,
					 field->is_nullable ? "nullable" :
					 "not nullable", part->is_nullable ?
					 "nullable" : "not nullable");
				return -1;
			}

			field->is_key_part = true;
			if (field->type == FIELD_TYPE_ANY) {
				field->type = part->type;
			} else if (field->type != part->type) {
				/**
				 * Check that there are no
				 * conflicts between index part
				 * types and space fields.
				 */
				diag_set(ClientError, ER_FIELD_TYPE_MISMATCH,
					 part->fieldno + TUPLE_INDEX_BASE,
					 field_type_strs[part->type],
					 field_type_strs[field->type]);
				return -1;
			}
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
		   const struct field_def *space_fields,
		   uint32_t space_field_count)
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
	for (uint32_t i = 0; i < space_field_count; ++i)
		total += strlen(space_fields[i].name) + 1;

	struct tuple_format *format = (struct tuple_format *) malloc(total);
	if (format == NULL) {
		diag_set(OutOfMemory, sizeof(struct tuple_format), "malloc",
			 "tuple format");
		return NULL;
	}
	if (space_field_count != 0) {
		format->names = mh_strnu32_new();
		if (format->names == NULL) {
			diag_set(OutOfMemory, sizeof(*format->names),
				 "mh_strnu32_new", "format->names");
			goto error_name_hash_new;
		}
		if (mh_strnu32_reserve(format->names, space_field_count,
				       NULL) != 0) {
			diag_set(OutOfMemory, space_field_count *
					      sizeof(struct mh_strnu32_node_t),
				 "mh_strnu32_reserve", "format->names");
			goto error_name_hash_reserve;
		}
	} else {
		format->names = NULL;
	}
	format->refs = 0;
	format->id = FORMAT_ID_NIL;
	format->field_count = field_count;
	format->index_field_count = index_field_count;
	format->exact_field_count = 0;
	return format;

error_name_hash_reserve:
	mh_strnu32_delete(format->names);
error_name_hash_new:
	free(format);
	return NULL;
}

/** Free tuple format resources, doesn't unregister. */
static inline void
tuple_format_destroy(struct tuple_format *format)
{
	if (format->names != NULL) {
		while (mh_size(format->names)) {
			mh_int_t i = mh_first(format->names);
			mh_strnu32_del(format->names, i, NULL);
		}
		mh_strnu32_delete(format->names);
	}
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
		 const struct field_def *space_fields, uint32_t space_field_count)
{
	struct tuple_format *format =
		tuple_format_alloc(keys, key_count, space_fields,
				   space_field_count);
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
tuple_format_eq(const struct tuple_format *a, const struct tuple_format *b)
{
	if (a->field_map_size != b->field_map_size ||
	    a->field_count != b->field_count)
		return false;
	for (uint32_t i = 0; i < a->field_count; ++i) {
		if (a->fields[i].type != b->fields[i].type ||
		    a->fields[i].offset_slot != b->fields[i].offset_slot)
			return false;
		if (a->fields[i].is_key_part != b->fields[i].is_key_part)
			return false;
		if (a->fields[i].is_nullable != b->fields[i].is_nullable)
			return false;
	}
	return true;
}

struct tuple_format *
tuple_format_dup(const struct tuple_format *src)
{
	uint32_t total = sizeof(struct tuple_format) +
			 src->field_count * sizeof(struct tuple_field);
	uint32_t name_offset = total;
	uint32_t name_count = 0;
	for (; name_count < src->field_count &&
	       src->fields[name_count].name != NULL; ++name_count)
		total += strlen(src->fields[name_count].name) + 1;

	struct tuple_format *format = (struct tuple_format *) malloc(total);
	if (format == NULL) {
		diag_set(OutOfMemory, total, "malloc", "tuple format");
		return NULL;
	}
	memcpy(format, src, total);
	if (name_count != 0) {
		format->names = mh_strnu32_new();
		if (format->names == NULL) {
			diag_set(OutOfMemory, sizeof(*format->names),
				 "mh_strnu32_new", "format->names");
			goto error_name_hash_new;
		}
		if (mh_strnu32_reserve(format->names, name_count,
				       NULL) != 0) {
			diag_set(OutOfMemory, sizeof(struct mh_strnu32_node_t) *
					      name_count,
				 "mh_strnu32_reserve", "format->names");
			goto error_name_hash_reserve;
		}
		char *name_pos = (char *)format + name_offset;
		for (uint32_t i = 0; i < name_count; ++i) {
			assert(src->fields[i].name != NULL);
			uint32_t len = strlen(src->fields[i].name);
			format->fields[i].name = name_pos;
			tuple_format_add_name(format, name_pos, len, i, false);
			name_pos += len + 1;
		}
	} else {
		assert(format->names == NULL);
	}
	format->id = FORMAT_ID_NIL;
	format->refs = 0;
	if (tuple_format_register(format) != 0) {
		tuple_format_destroy(format);
		free(format);
		return NULL;
	}
	return format;

error_name_hash_reserve:
	mh_strnu32_delete(format->names);
error_name_hash_new:
	free(format);
	return NULL;
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
	if (unlikely(field_count < format->field_count)) {
		diag_set(ClientError, ER_INDEX_FIELD_COUNT,
			 (unsigned) field_count,
			 (unsigned) format->field_count);
		return -1;
	}

	/* first field is simply accessible, so we do not store offset to it */
	enum mp_type mp_type = mp_typeof(*pos);
	if (key_mp_type_validate(format->fields[0].type, mp_type, ER_FIELD_TYPE,
				 TUPLE_INDEX_BASE))
		return -1;
	mp_next(&pos);
	/* other fields...*/
	for (uint32_t i = 1; i < format->field_count; i++) {
		mp_type = mp_typeof(*pos);
		if (key_mp_type_validate(format->fields[i].type, mp_type,
					 ER_FIELD_TYPE, i + TUPLE_INDEX_BASE))
			return -1;
		if (format->fields[i].offset_slot != TUPLE_OFFSET_SLOT_NIL)
			field_map[format->fields[i].offset_slot] =
				(uint32_t) (pos - tuple);
		mp_next(&pos);
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

const char *
tuple_field_raw_by_name(struct tuple_format *format, const char *tuple,
			const uint32_t *field_map, const char *name,
			uint32_t name_len, uint32_t name_hash)
{
	if (format->names == NULL)
		return NULL;
	struct mh_strnu32_key_t key = {name, name_len, name_hash};
	mh_int_t rc = mh_strnu32_find(format->names, &key, NULL);
	if (rc == mh_end(format->names))
		return NULL;
	uint32_t fieldno = mh_strnu32_node(format->names, rc)->val;
	return tuple_field_raw(format, tuple, field_map, fieldno);
}
