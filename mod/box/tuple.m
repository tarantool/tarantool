/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "tuple.h"

#include <pickle.h>
#include <salloc.h>

#include "exception.h"

/*
 * local functions declaraion
 */

/** get next field */
static void *
next_field(void *f);

/** print field value to tbuf */
static void
print_field(struct tbuf *buf, void *f);


/*
 * tuple interface definition
 */

/** Allocate tuple */
struct box_tuple *
tuple_alloc(size_t size)
{
	size_t total = sizeof(struct box_tuple) + size;
	struct box_tuple *tuple = salloc(total);

	if (tuple == NULL)
		tnt_raise(LoggedError, :ER_MEMORY_ISSUE, total, "slab allocator", "tuple");

	tuple->flags = tuple->refs = 0;
	tuple->bsize = size;

	say_debug("tuple_alloc(%zu) = %p", size, tuple);
	return tuple;
}

/**
 * Clean-up tuple
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_free(struct box_tuple *tuple)
{
	say_debug("tuple_free(%p)", tuple);
	assert(tuple->refs == 0);
	sfree(tuple);
}

/**
 * Add count to tuple's reference counter. If tuple's refs counter down to
 * zero the tuple will be destroyed.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct box_tuple *tuple, int count)
{
	assert(tuple->refs + count >= 0);
	tuple->refs += count;

	if (tuple->refs == 0)
		tuple_free(tuple);
}

/**
 * Get field from tuple
 *
 * @returns field data if field is exist or NULL
 */
void *
tuple_field(struct box_tuple *tuple, size_t i)
{
	void *field = tuple->data;

	if (i >= tuple->cardinality)
		return NULL;

	while (i-- > 0)
		field = next_field(field);

	return field;
}

/**
 * Tuple length.
 *
 * @returns tuple length in bytes, exception will be raised if error happen.
 */
u32
tuple_length(struct tbuf *buf, u32 cardinality)
{
	void *data = buf->data;
	u32 len = buf->len;

	for (int i = 0; i < cardinality; i++)
		read_field(buf);

	u32 r = len - buf->len;
	buf->data = data;
	buf->len = len;
	return r;
}

/**
 * Print a tuple in yaml-compatible mode tp tbuf:
 * key: { value, value, value }
 */
void
tuple_print(struct tbuf *buf, uint8_t cardinality, void *f)
{
	print_field(buf, f);
	tbuf_printf(buf, ": {");
	f = next_field(f);

	for (size_t i = 1; i < cardinality; i++, f = next_field(f)) {
		print_field(buf, f);
		if (likely(i + 1 < cardinality))
			tbuf_printf(buf, ", ");
	}
	tbuf_printf(buf, "}\r\n");
}


/*
 * local function definition
 */

/** get next field from tuple */
static void *
next_field(void *f)
{
	u32 size = load_varint32(&f);
	return (u8 *)f + size;
}

/** print field to tbuf */
static void
print_field(struct tbuf *buf, void *f)
{
	uint32_t size = load_varint32(&f);
	switch (size) {
	case 2:
		tbuf_printf(buf, "%hu", *(u16 *)f);
		break;
	case 4:
		tbuf_printf(buf, "%u", *(u32 *)f);
		break;
	default:
		tbuf_printf(buf, "'");
		while (size-- > 0) {
			if (0x20 <= *(u8 *)f && *(u8 *)f < 0x7f)
				tbuf_printf(buf, "%c", *(u8 *)f++);
			else
				tbuf_printf(buf, "\\x%02X", *(u8 *)f++);
		}
		tbuf_printf(buf, "'");
		break;
	}
}

