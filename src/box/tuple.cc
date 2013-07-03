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

/** Allocate a tuple */
struct tuple *
tuple_alloc(size_t size)
{
	size_t total = sizeof(struct tuple) + size;
	struct tuple *tuple = (struct tuple *) salloc(total, "tuple");

	tuple->flags = tuple->refs = 0;
	tuple->bsize = size;

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
tuple_field_old(struct tuple *tuple, uint32_t i)
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
tuple_update(void *(*region_alloc)(void *, size_t), void *alloc_ctx,
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
	struct tuple *new_tuple = tuple_alloc(new_size);
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
tuple_new(uint32_t field_count, const char **data, const char *end)
{
	size_t tuple_len = end - *data;

	if (tuple_len != tuple_range_size(data, end, field_count))
		tnt_raise(IllegalParams, "tuple_new(): incorrect tuple format");

	struct tuple *new_tuple = tuple_alloc(tuple_len);
	new_tuple->field_count = field_count;
	memcpy(new_tuple->data, end - tuple_len, tuple_len);
	return new_tuple;
}

static inline int
tuple_compare_field(const char *field_a, uint32_t size_a,
		    const char *field_b, uint32_t size_b,
		    enum field_type type)
{
	/*
	 * field_a is always a tuple field.
	 * field_b can be either a tuple field or a key part.
	 * All tuple fields were validated before by space_validate_tuple().
	 * All key parts were validated before by key_validate().
	 */
	switch (type) {
	case NUM:
	{
		assert(size_a == sizeof(uint32_t));
		assert(size_b == sizeof(uint32_t));
		uint32_t a = *(uint32_t *) field_a;
		uint32_t b = *(uint32_t *) field_b;
		return a < b ? -1 : (a > b);
	}
	case NUM64:
	{
		assert(size_a == sizeof(uint64_t));
		uint64_t a = *(uint64_t *) field_a;
		uint64_t b;
		/* Allow search in NUM64 indexes using NUM keys. */
		if (size_b == sizeof(uint32_t)) {
			b = *(uint32_t *) field_b;
		} else {
			assert(size_b == sizeof(uint64_t));
			b = *(uint64_t *) field_b;
		}
		return a < b ? -1 : (a > b);
	}
	case STRING:
	{
		int cmp = memcmp(field_a, field_b, MIN(size_a, size_b));
		if (cmp != 0)
			return cmp;

		if (size_a > size_b) {
			return 1;
		} else if (size_a < size_b){
			return -1;
		} else {
			return 0;
		}
	}
	default:
		assert(false);
	}
}

int
tuple_compare(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def)
{
	for (uint32_t part = 0; part < key_def->part_count; part++) {
		uint32_t field_no = key_def->parts[part].fieldno;
		uint32_t size_a, size_b;
		const char *field_a = tuple_field(tuple_a, field_no, &size_a);
		const char *field_b = tuple_field(tuple_b, field_no, &size_b);

		int r = tuple_compare_field(field_a, size_a, field_b, size_b,
					    key_def->parts[part].type);
		if (r != 0) {
			return r;
		}
	}

	return 0;
}

int
tuple_compare_dup(const struct tuple *tuple_a, const struct tuple *tuple_b,
		  const struct key_def *key_def)
{
	int r = tuple_compare(tuple_a, tuple_b, key_def);
	if (r != 0) {
		return r;
	}

	return tuple_a < tuple_b ? -1 : (tuple_a > tuple_b);
}

int
tuple_compare_with_key(const struct tuple *tuple_a, const char *key,
		       uint32_t part_count, const struct key_def *key_def)
{
	part_count = MIN(part_count, key_def->part_count);
	for (uint32_t part = 0; part < part_count; part++) {
		uint32_t field_no = key_def->parts[part].fieldno;

		uint32_t size_a;
		const char *field_a = tuple_field(tuple_a, field_no, &size_a);

		uint32_t key_size = load_varint32(&key);
		int r = tuple_compare_field(field_a, size_a, key, key_size,
					    key_def->parts[part].type);
		if (r != 0) {
			return r;
		}

		key += key_size;
	}

	return 0;
}
