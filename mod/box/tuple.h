#ifndef TARANTOOL_BOX_TUPLE_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_H_INCLUDED
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
#include <util.h>

struct tbuf;

/**
 * An atom of Tarantool/Box storage. Consists of a list of fields.
 * The first field is always the primary key.
 */
struct box_tuple
{
	/** reference counter */
	u16 refs;
	/* see enum tuple_flags */
	u16 flags;
	/** length of the variable part of the tuple */
	u32 bsize;
	/** number of fields in the variable part. */
	u32 field_count;
	/**
	 * Fields can have variable length, and thus are packed
	 * into a contiguous byte array. Each field is prefixed
	 * with BER-packed field length.
	 */
	u8 data[0];
} __attribute__((packed));

/** Allocate a tuple
 *
 * @param size  tuple->bsize
 * @post tuple->refs = 1
 */
struct box_tuple *
tuple_alloc(size_t size);

/**
 * Change tuple reference counter. If it has reached zero, free the tuple.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct box_tuple *tuple, int count);

/**
 * Get a field from tuple by index.
 *
 * @returns field data if the field exists, or NULL
 */
void *
tuple_field(struct box_tuple *tuple, size_t i);

/**
 * Print a tuple in yaml-compatible mode to tbuf:
 * key: { value, value, value }
 */
void
tuple_print(struct tbuf *buf, uint8_t field_count, void *f);

/** Tuple length when adding to iov. */
static inline size_t tuple_len(struct box_tuple *tuple)
{
	return tuple->bsize + sizeof(tuple->bsize) +
		sizeof(tuple->field_count);
}
#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

