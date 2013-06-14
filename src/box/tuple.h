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
#include <pickle.h>

struct tbuf;

/**
 * An atom of Tarantool/Box storage. Consists of a list of fields.
 * The first field is always the primary key.
 */
struct tuple
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
	char data[0];
} __attribute__((packed));

/** Allocate a tuple
 *
 * @param size  tuple->bsize
 * @post tuple->refs = 1
 */
struct tuple *
tuple_alloc(size_t size);

/**
 * Change tuple reference counter. If it has reached zero, free the tuple.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct tuple *tuple, int count);

/**
 * Get a field from tuple by index.
 *
 * @returns field data if the field exists, or NULL
 */
__attribute__((deprecated)) const char *
tuple_field_old(struct tuple *tuple, u32 i);

/**
 * @brief Return field data of the field
 * @param tuple tuple
 * @param field_no field number
 * @param begin pointer where the start of field data will be stored
 * @param end pointer where the end of field data + 1 will be stored
 * @throws IllegalParams if \a field_no is out of range
 */
void
tuple_field(const struct tuple *tuple, uint32_t field_no,
	    const char **begin, const char **end);

/**
 * @brief Tuple Interator
 */
struct tuple_iterator {
	/** @cond false **/
	/* Result */
	const char **begin;
	const char **end;
	/* State */
	const struct tuple *tuple;
	const char *cur;
	/** @endcond **/
};

/**
 * @brief Seek tuple iterator to position \a field_no
 *
 * A workflow example:
 * @code
 * struct tuple_iterator it;
 * const char *fb, *fe;
 * tuple_seek(&it, tuple, 0, &fb, &fe);
 * while (tuple_next(&it)) {
 *      // field_data = fb
 *	// field_size = fe-fb
 *	lua_pushlstring(L, fb, fe - fb);
 * }
 * @endcode
 *
 * @param it tuple iterator
 * @param tuple tuple
 * @param field_no a field number to seek
 * @param begin pointer where the start of field data will be stored
 * @param end pointer where the end of field data + 1 will be stored
 */
void
tuple_seek(struct tuple_iterator *it, const struct tuple *tuple,
	   uint32_t field_no, const char **begin, const char **end);

/**
 * @brief Iterate to the next position
 * @param it tuple iterator
 * @retval \a it if the iterator has tuple
 * @retval NULL if where is no more tuples (values of \a begin \a end
 * are not specifed in this case)
 */
struct tuple_iterator *
tuple_next(struct tuple_iterator *it);

/**
 * @brief Print a tuple in yaml-compatible mode to tbuf:
 * key: { value, value, value }
 *
 * @param buf tbuf
 * @param tuple tuple
 */
void
tuple_print(struct tbuf *buf, const struct tuple *tuple);

/** Tuple length when adding to iov. */
static inline size_t tuple_len(struct tuple *tuple)
{
	return tuple->bsize + sizeof(tuple->bsize) +
		sizeof(tuple->field_count);
}

void tuple_free(struct tuple *tuple);

/**
 * Calculate size for a specified fields range
 *
 * @returns size of fields data including size of varint data
 */
static inline size_t
tuple_range_size(const char **begin, const char *end, size_t count)
{
	const char *start = *begin;
	while (*begin < end && count-- > 0) {
		size_t len = load_varint32(begin);
		*begin += len;
	}
	return *begin - start;
}

static inline uint32_t
valid_tuple(const char *data, const char *end, uint32_t field_count)
{
	return tuple_range_size(&data, end, field_count);
}

#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

