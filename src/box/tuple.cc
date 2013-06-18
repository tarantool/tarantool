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

/** Get the next field from a tuple */
static const char *
next_field(const char *f)
{
	u32 size = load_varint32(&f);
	return f + size;
}

/**
 * Get a field from tuple.
 *
 * @returns field data if field exists or NULL
 */
const char *
tuple_field(struct tuple *tuple, u32 i)
{
	const char *field = tuple->data;

	if (i >= tuple->field_count)
		return NULL;

	while (i-- > 0)
		field = next_field(field);

	return field;
}

/** print field to tbuf */
static void
print_field(struct tbuf *buf, const char *f)
{
	uint32_t size = load_varint32(&f);
	switch (size) {
	case 2:
		tbuf_printf(buf, "%hu", *(u16 *)f);
		break;
	case 4:
		tbuf_printf(buf, "%u", *(u32 *)f);
		break;
	case 8:
		tbuf_printf(buf, "%" PRIu64, *(u64 *)f);
		break;
	default:
		tbuf_printf(buf, "'");
		while (size-- > 0) {
			if (0x20 <= *(u8 *)f && *(u8 *)f < 0x7f) {
				tbuf_printf(buf, "%c", *(u8 *) f);
			} else {
				tbuf_printf(buf, "\\x%02X", *(u8 *)f);
			}
			f++;
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
tuple_print(struct tbuf *buf, u32 field_count, const char *f)
{
	if (field_count == 0) {
		tbuf_printf(buf, "'': {}");
		return;
	}

	print_field(buf, f);
	tbuf_printf(buf, ": {");
	f = next_field(f);

	for (u32 i = 1; i < field_count; i++, f = next_field(f)) {
		print_field(buf, f);
		if (likely(i + 1 < field_count))
			tbuf_printf(buf, ", ");
	}
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
