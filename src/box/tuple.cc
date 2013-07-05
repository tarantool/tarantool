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

static struct tuple_format *
tuple_format_alloc_and_register(uint32_t offset_count)
{
	if (formats_size == formats_capacity) {
		uint32_t new_capacity = formats_capacity ?
			formats_capacity * 2 : 16;
		if (new_capacity >= UINT16_MAX)
			tnt_raise(LoggedError, ER_MEMORY_ISSUE,
				  new_capacity, "tuple_formats", "resize");
		struct tuple_format **formats = (struct tuple_format **)
			realloc(tuple_formats,
				new_capacity * sizeof(tuple_formats[0]));
		if (formats == NULL)
			tnt_raise(LoggedError, ER_MEMORY_ISSUE,
				  new_capacity, "tuple_formats", "realloc");

		formats_capacity = new_capacity;
		tuple_formats = formats;
	}

	uint32_t total = sizeof(struct tuple_format) +
		offset_count * sizeof(int32_t);

	struct tuple_format *format = (struct tuple_format *) malloc(total);

	if (format == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE,
			  total, "tuple format", "malloc");
	}

	format->id = formats_size++;
	format->offset_count = offset_count;
	tuple_formats[format->id] = format;
	return format;
}

struct tuple_format *
tuple_format_new(const enum field_type *fields, uint32_t max_fieldno)
{
	(void) max_fieldno;
	(void) fields;
	struct tuple_format *format =
		tuple_format_alloc_and_register(0);

	return format;
}

/** Allocate a tuple */
struct tuple *
tuple_alloc(struct tuple_format *format, size_t size)
{
	size_t total = sizeof(struct tuple) + size;
	struct tuple *tuple = (struct tuple *) salloc(total, "tuple");

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
	sfree(tuple);
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
tuple_field_old(const struct tuple *tuple, uint32_t i)
{
	const char *field = tuple->data;
	const char *tuple_end = tuple->data + tuple->bsize;

	while (field < tuple_end) {
		if (i == 0)
			return field;
		uint32_t len = load_varint32(&field);
		field += len;
		i--;
	}
	return tuple_end;
}

const char *
tuple_field(const struct tuple *tuple, uint32_t field_no, uint32_t *len)
{
	const char *field = tuple_field_old((struct tuple *) tuple, field_no);
	if (field < tuple->data + tuple->bsize) {
		*len = load_varint32(&field);
		return field;
	}
	return NULL;
}

const char *
tuple_seek(struct tuple_iterator *it, uint32_t field_no, uint32_t *len)
{
	it->pos = tuple_field_old((struct tuple *) it->tuple, field_no);
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
	uint32_t len;
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
		printf("len: %d\n", field_a[0]);
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
	const char *field_a;
	const char *field_b;
	int r;

	for (; part < end; part++) {
		field_a = tuple_field_old(tuple_a, part->fieldno);
		field_b = tuple_field_old(tuple_b, part->fieldno);
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
	const char *field;
	uint32_t field_size;
	uint32_t key_size;
	int r = 0; /* Part count can be 0 in wildcard searches. */
	for (; part < end; part++, key += key_size) {
		field = tuple_field_old(tuple, part->fieldno);
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
