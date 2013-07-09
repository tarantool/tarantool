/*
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
#include "tuple.h"

#include <salloc.h>
#include "tbuf.h"

#include "key_def.h"
#include "tuple_update.h"
#include <exception.h>
#include <palloc.h>
#include <fiber.h>
#include "scoped_guard.h"
#include <stdio.h>

/** Global table of tuple formats */
struct tuple_format **tuple_formats;
struct tuple_format *tuple_format_ber;

static uint32_t formats_size, formats_capacity;

/** Extract all available type info from keys. */
void
field_type_create(enum field_type *types, uint32_t field_count,
		  struct key_def *key_def, uint32_t key_count)
{
	/* There may be fields between indexed fields (gaps). */
	memset(types, 0, sizeof(*types) * field_count);

	struct key_def *end = key_def + key_count;
	/* extract field type info */
	for (; key_def < end; key_def++) {
		struct key_part *part = key_def->parts;
		struct key_part *pend = part + key_def->part_count;
		for (; part < pend; part++) {
			assert(part->fieldno < field_count);
			types[part->fieldno] = part->type;
		}
	}
}

static struct tuple_format *
tuple_format_alloc_and_register(struct key_def *key_def,
				uint32_t key_count)
{
	uint32_t total;
	struct tuple_format *format;
	struct key_def *end = key_def + key_count;
	uint32_t max_fieldno = 0;
	uint32_t field_count;

	/* find max max field no */
	for (; key_def < end; key_def++)
		max_fieldno= MAX(max_fieldno, key_def->max_fieldno);

	if (formats_size == formats_capacity) {
		uint32_t new_capacity = formats_capacity ?
			formats_capacity * 2 : 16;
		struct tuple_format **formats;
		if (new_capacity >= UINT16_MAX)
			goto error;
		 formats = (struct tuple_format **) realloc(tuple_formats,
				new_capacity * sizeof(tuple_formats[0]));
		if (formats == NULL)
			goto error;

		formats_capacity = new_capacity;
		tuple_formats = formats;
	}
	field_count = key_count > 0 ? max_fieldno + 1 : 0;

	total = sizeof(struct tuple_format) +
		field_count * sizeof(int32_t) +
		field_count * sizeof(enum field_type);

	format = (struct tuple_format *) malloc(total);

	if (format == NULL)
		goto error;

	format->id = formats_size++;
	format->max_fieldno = max_fieldno;
	format->field_count = field_count;
	format->types = (enum field_type *)
		((char *) format + sizeof(*format) +
		field_count * sizeof(int32_t));
	tuple_formats[format->id] = format;
	return format;
error:
	tnt_raise(LoggedError, ER_MEMORY_ISSUE,
		  sizeof(struct tuple_format), "tuple format", "malloc");
	return NULL;
}

struct tuple_format *
tuple_format_new(struct key_def *key_def, uint32_t key_count)
{
	struct tuple_format *format =
		tuple_format_alloc_and_register(key_def, key_count);

	field_type_create(format->types, format->field_count,
			  key_def, key_count);

	int32_t i = 0;
	uint32_t prev_offset = 0;
	/*
	 * In the format, store all offsets available,
	 * they may be useful.
	 */
	for (; i < format->max_fieldno; i++) {
		uint32_t maxlen = field_type_maxlen(format->types[i]);
		if (maxlen == UINT32_MAX)
			break;
		format->offset[i] = (varint32_sizeof(maxlen) + maxlen +
				     prev_offset);
		assert(format->offset[i] > 0);
		prev_offset = format->offset[i];
	}
	int j = 0;
	for (; i < format->max_fieldno; i++) {
		/*
		 * In the tuple, store only offsets necessary to
		 * quickly access indexed fields. Start from
		 * field 1, not field 0, field 0 offset is 0.
		 */
		if (format->types[i + 1] == UNKNOWN)
			format->offset[i] = INT32_MIN;
		else
			format->offset[i] = --j;
	}
	if (format->field_count > 0) {
		/*
		 * The last offset is always there and is unused,
		 * to simplify the loop in tuple_init_field_map()
		 */
		format->offset[format->field_count - 1] = INT32_MIN;
	}
	format->field_map_size = -j * sizeof(uint32_t);
	return format;
}

/*
 * Validate a new tuple format and initialize tuple-local
 * format data.
 */
static inline void
tuple_init_field_map(struct tuple *tuple, struct tuple_format *format)
{
	/* Check to see if the tuple has a sufficient number of fields. */
	if (tuple->field_count < format->field_count)
		tnt_raise(IllegalParams,
			  "tuple must have all indexed fields");

	int32_t *offset = format->offset;
	enum field_type *type = format->types;
	enum field_type *end = format->types + format->field_count;
	const char *pos = tuple->data;
	uint32_t *field_map = (uint32_t *) tuple;

	for (; type < end; offset++, type++) {
		if (pos >= tuple->data + tuple->bsize)
			tnt_raise(IllegalParams,
				  "incorrect tuple format");
		uint32_t len = load_varint32(&pos);
		uint32_t type_maxlen = field_type_maxlen(*type);
		/*
		 * For fixed offsets, validate fields have
		 * correct lengths.
		 */
		if (type_maxlen != UINT32_MAX && len != type_maxlen) {
			tnt_raise(ClientError, ER_KEY_FIELD_TYPE,
				  field_type_strs[*type]);
		}
		pos += len;
		if (*offset < 0 && *offset != INT32_MIN)
			field_map[*offset] = pos - tuple->data;
	}
}

/** Allocate a tuple */
struct tuple *
tuple_alloc(struct tuple_format *format, size_t size)
{
	size_t total = sizeof(struct tuple) + size + format->field_map_size;
	char *ptr = (char *) salloc(total, "tuple");
	struct tuple *tuple = (struct tuple *)(ptr + format->field_map_size);

	tuple->refs = 0;
	tuple->bsize = size;
	tuple->format_id = tuple_format_id(format);

	say_debug("tuple_alloc(%zu) = %p", size, tuple);
	return tuple;
}

/**
 * Free the tuple.
 * @pre tuple->refs  == 0
 */
void
tuple_free(struct tuple *tuple)
{
	say_debug("tuple_free(%p)", tuple);
	assert(tuple->refs == 0);
	char *ptr = (char *) tuple - tuple_format(tuple)->field_map_size;
	sfree(ptr);
}

/**
 * Add count to tuple's reference counter.
 * When the counter goes down to 0, the tuple is destroyed.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct tuple *tuple, int count)
{
	assert(tuple->refs + count >= 0);
	tuple->refs += count;

	if (tuple->refs == 0)
		tuple_free(tuple);
}

/**
 * Get a field from tuple.
 *
 * @pre field < tuple->field_count.
 * @returns field data if field exists or NULL
 */
const char *
tuple_field_old(const struct tuple_format *format,
		const struct tuple *tuple, uint32_t i)
{
	const char *field = tuple->data;

	if (i == 0)
		return field;
	i--;
	if (i < format->max_fieldno) {
		if (format->offset[i] > 0)
			return field + format->offset[i];
		if (format->offset[i] != INT32_MIN) {
			uint32_t *field_map = (uint32_t *) tuple;
			int32_t idx = format->offset[i];
			return field + field_map[idx];
		}
	}
	const char *tuple_end = field + tuple->bsize;

	while (field < tuple_end) {
		uint32_t len = load_varint32(&field);
		field += len;
		if (i == 0)
			return field;
		i--;
	}
	return tuple_end;
}

const char *
tuple_seek(struct tuple_iterator *it, uint32_t i, uint32_t *len)
{
	it->pos = tuple_field_old(tuple_format(it->tuple), it->tuple, i);
	return tuple_next(it, len);
}

const char *
tuple_next(struct tuple_iterator *it, uint32_t *len)
{
	const char *tuple_end = it->tuple->data + it->tuple->bsize;
	if (it->pos < tuple_end) {
		*len = load_varint32(&it->pos);
		const char *field = it->pos;
		it->pos += *len;
		assert(it->pos <= tuple_end);
		return field;
	}
	return NULL;
}

/** print field to tbuf */
static void
print_field(struct tbuf *buf, const char *field, uint32_t len)
{
	switch (len) {
	case 2:
		tbuf_printf(buf, "%hu", *(uint16_t *)field);
		break;
	case 4:
		tbuf_printf(buf, "%u", *(uint32_t *)field);
		break;
	case 8:
		tbuf_printf(buf, "%" PRIu64, *(uint64_t *)field);
		break;
	default:
		tbuf_printf(buf, "'");
		const char *field_end = field + len;
		while (field < field_end) {
			if (0x20 <= *(uint8_t *)field && *(uint8_t *)field < 0x7f) {
				tbuf_printf(buf, "%c", *(uint8_t *) field);
			} else {
				tbuf_printf(buf, "\\x%02X", *(uint8_t *)field);
			}
			field++;
		}
		tbuf_printf(buf, "'");
		break;
	}
}

/**
 * Print a tuple in yaml-compatible mode to tbuf:
 * key: { value, value, value }
 */
void
tuple_print(struct tbuf *buf, const struct tuple *tuple)
{
	if (tuple->field_count == 0) {
		tbuf_printf(buf, "'': {}");
		return;
	}

	struct tuple_iterator it;
	const char *field;
	uint32_t len = 0;
	tuple_rewind(&it, tuple);
	field = tuple_next(&it, &len);
	print_field(buf, field, len);
	tbuf_printf(buf, ": {");

	uint32_t field_no = 1;
	while ((field = tuple_next(&it, &len))) {
		print_field(buf, field, len);
		if (likely(++field_no < tuple->field_count))
			tbuf_printf(buf, ", ");
	}
	assert(field_no == tuple->field_count);

	tbuf_printf(buf, "}");
}

struct tuple *
tuple_update(struct tuple_format *format,
	     void *(*region_alloc)(void *, size_t), void *alloc_ctx,
	     const struct tuple *old_tuple, const char *expr,
	     const char *expr_end)
{
	uint32_t new_size = 0;
	uint32_t new_field_count = 0;
	struct tuple_update *update =
		tuple_update_prepare(region_alloc, alloc_ctx,
				     expr, expr_end, old_tuple->data,
				     old_tuple->data + old_tuple->bsize,
				     old_tuple->field_count, &new_size,
				     &new_field_count);

	/* Allocate a new tuple. */
	struct tuple *new_tuple = tuple_alloc(format, new_size);
	new_tuple->field_count = new_field_count;

	try {
		tuple_update_execute(update, new_tuple->data);
		tuple_init_field_map(new_tuple, format);
	} catch (const Exception&) {
		tuple_free(new_tuple);
		throw;
	}
	return new_tuple;
}

struct tuple *
tuple_new(struct tuple_format *format, uint32_t field_count,
	  const char **data, const char *end)
{
	size_t tuple_len = end - *data;

	if (tuple_len != tuple_range_size(data, end, field_count))
		tnt_raise(IllegalParams, "tuple_new(): incorrect tuple format");

	struct tuple *new_tuple = tuple_alloc(format, tuple_len);
	new_tuple->field_count = field_count;
	memcpy(new_tuple->data, end - tuple_len, tuple_len);
	try {
		tuple_init_field_map(new_tuple, format);
	} catch (...) {
		tuple_free(new_tuple);
		throw;
	}
	return new_tuple;
}

/*
 * Compare two tuple fields.
 * Separate version exists since compare is a very
 * often used operation, so any performance speed up
 * in it can have dramatic impact on the overall
 * server performance.
 */
static inline int
tuple_compare_field(const char *field_a, const char *field_b,
		    enum field_type type)
{
	if (type != STRING) {
		assert(field_a[0] == field_b[0]);
		/*
		 * Little-endian unsigned int is memcmp
		 * compatible.
		 */
		return memcmp(field_a + 1, field_b + 1, field_a[0]);
	}
	uint32_t size_a = load_varint32(&field_a);
	uint32_t size_b = load_varint32(&field_b);
	int r = memcmp(field_a, field_b, MIN(size_a, size_b));
	if (r == 0)
		r = size_a < size_b ? -1 : size_a > size_b;
	return r;
}

int
tuple_compare(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def)
{
	if (key_def->part_count == 1 && key_def->parts[0].fieldno == 0)
		return tuple_compare_field(tuple_a->data, tuple_b->data,
					   key_def->parts[0].type);

	struct key_part *part = key_def->parts;
	struct key_part *end = part + key_def->part_count;
	struct tuple_format *format_a = tuple_format(tuple_a);
	struct tuple_format *format_b = tuple_format(tuple_b);
	const char *field_a;
	const char *field_b;
	int r = 0;

	for (; part < end; part++) {
		field_a = tuple_field_old(format_a, tuple_a, part->fieldno);
		field_b = tuple_field_old(format_b, tuple_b, part->fieldno);
		if ((r = tuple_compare_field(field_a, field_b, part->type)))
			break;
	}
	return r;
}

int
tuple_compare_dup(const struct tuple *tuple_a, const struct tuple *tuple_b,
		  const struct key_def *key_def)
{
	int r = tuple_compare(tuple_a, tuple_b, key_def);
	if (r == 0)
		r = tuple_a < tuple_b ? -1 : tuple_a > tuple_b;

	return r;
}

int
tuple_compare_with_key(const struct tuple *tuple, const char *key,
		       uint32_t part_count, const struct key_def *key_def)
{
	struct key_part *part = key_def->parts;
	struct key_part *end = part + MIN(part_count, key_def->part_count);
	struct tuple_format *format = tuple_format(tuple);
	const char *field;
	uint32_t field_size;
	uint32_t key_size;
	int r = 0; /* Part count can be 0 in wildcard searches. */
	for (; part < end; part++, key += key_size) {
		field = tuple_field_old(format, tuple, part->fieldno);
		field_size = load_varint32(&field);
		key_size = load_varint32(&key);
		switch (part->type) {
		case NUM:
			r = memcmp(field, key, sizeof(uint32_t));
			break;
		case NUM64:
			if (key_size == sizeof(uint32_t)) {
				/*
				 * Allow search in NUM64 indexes
				 * using NUM keys.
				 */
				uint64_t b = *(uint32_t *) key;
				r = memcmp(field, &b, sizeof(uint64_t));
			} else {
				r = memcmp(field, key, sizeof(uint64_t));
			}
			break;
		default:
			r = memcmp(field, key, MIN(field_size, key_size));
			if (r == 0)
				r = field_size < key_size ? -1 : field_size > key_size;
			break;
		}
		if (r != 0)
			break;
	}
	return r;
}

void
tuple_init()
{
	tuple_format_ber = tuple_format_new(NULL, 0);
}

void
tuple_free()
{
	for (struct tuple_format **format = tuple_formats;
	     format < tuple_formats + formats_size;
	     format++)
		free(*format);
	free(tuple_formats);
}
