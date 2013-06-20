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
tuple_field_old(struct tuple *tuple, u32 i)
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
		tbuf_printf(buf, "%hu", *(u16 *)field);
		break;
	case 4:
		tbuf_printf(buf, "%u", *(u32 *)field);
		break;
	case 8:
		tbuf_printf(buf, "%" PRIu64, *(u64 *)field);
		break;
	default:
		tbuf_printf(buf, "'");
		const char *field_end = field + len;
		while (field < field_end) {
			if (0x20 <= *(u8 *)field && *(u8 *)field < 0x7f) {
				tbuf_printf(buf, "%c", *(u8 *) field);
			} else {
				tbuf_printf(buf, "\\x%02X", *(u8 *)field);
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

static void *
palloc_region_alloc(void *ctx, size_t size)
{
	return palloc((struct palloc_pool *) ctx, size);
}

struct tuple *
tuple_update(const struct tuple *old_tuple, const char *expr,
	     const char *expr_end)
{
	size_t allocated_size = palloc_allocated(fiber->gc_pool);
	auto scoped_guard = make_scoped_guard([=] {
		/* truncate memory used by tuple_update */
		ptruncate(fiber->gc_pool, allocated_size);
	});

	uint32_t new_size = 0;
	uint32_t new_field_count = 0;
	struct tuple_update *update =
		tuple_update_prepare(palloc_region_alloc, fiber->gc_pool,
				     expr, expr_end, old_tuple->data,
				     old_tuple->data + old_tuple->bsize,
				     old_tuple->field_count, &new_size,
				     &new_field_count);

	/* Allocate a new tuple. */
	struct tuple *new_tuple = tuple_alloc(new_size);
	new_tuple->field_count = new_field_count;

	try {
		tuple_update_execute(update, new_tuple->data);
		return new_tuple;
	} catch (const Exception&) {
		tuple_free(new_tuple);
		throw;
	}
}
