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

#include "exception.h"

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
tuple_field_old(struct tuple *tuple, u32 i)
{
	const char *field = tuple->data;

	if (i >= tuple->field_count)
		return NULL;

	while (i-- > 0)
		field = next_field(field);

	return field;
}

void
tuple_field(const struct tuple *tuple, uint32_t field_no,
	    const char **begin, const char **end)
{
	const char *data = tuple->data;

	if (field_no >= tuple->field_count)
		tnt_raise(IllegalParams, "field_no is out of range");

	while (field_no-- > 0)
		data = next_field(data);

	uint32_t len = load_varint32(&data);
	*begin = data;
	*end = data + len;
}

void
tuple_seek(struct tuple_iterator *it, const struct tuple *tuple,
	   uint32_t field_no, const char **begin, const char **end)
{
	const char *data = tuple->data;

	if (field_no >= tuple->field_count)
		tnt_raise(IllegalParams, "field_no is out of range");

	while (field_no-- > 0)
		data = next_field(data);

	it->tuple = tuple;
	it->cur = data;
	it->begin = begin;
	it->end = end;
	*it->begin = NULL;
	*it->end = NULL;
}

struct tuple_iterator *
tuple_next(struct tuple_iterator *it)
{
	if (it->cur == it->tuple->data + it->tuple->bsize) {
		/* No more fields in the tuple*/
		it->cur = NULL;
		return NULL;
	} else if (it->cur == NULL) {
		/* Sanity check: second call to next() is invalid */
		tnt_raise(IllegalParams, "field_no is out of range");
	}

	uint32_t len = load_varint32(&it->cur);
	*it->begin = it->cur;
	*it->end = it->cur + len;
	it->cur += len;

	if (it->cur <= it->tuple->data + it->tuple->bsize) {
		return it;
	} else /* it->cur > it->tuple->data + it->tuple->bsize */ {
		tnt_raise(IllegalParams, "invalid tuple");
	}
}


/** print field to tbuf */
static void
print_field(struct tbuf *buf, const char *f, const char *end)
{
	uint32_t size = (end - f);
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
tuple_print(struct tbuf *buf, const struct tuple *tuple)
{
	if (tuple->field_count == 0) {
		tbuf_printf(buf, "'': {}");
		return;
	}

	struct tuple_iterator it;
	const char *fb, *fe;
	tuple_seek(&it, tuple, 0, &fb, &fe);
	tuple_next(&it);
	print_field(buf, fb, fe);
	tbuf_printf(buf, ": {");

	uint32_t field_no = 1;
	for (; tuple_next(&it); field_no++) {
		print_field(buf, fb, fe);
		if (likely(field_no + 1 < tuple->field_count)) {
			tbuf_printf(buf, ", ");
		}
	}
	assert (field_no == tuple->field_count);

	tbuf_printf(buf, "}");
}
