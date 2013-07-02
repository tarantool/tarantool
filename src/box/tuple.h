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
#include "tarantool/util.h"
#include <pickle.h>

struct tbuf;
struct key_def;

/**
 * An atom of Tarantool/Box storage. Consists of a list of fields.
 * The first field is always the primary key.
 */
struct tuple
{
	/** reference counter */
	uint16_t refs;
	/* see enum tuple_flags */
	uint16_t flags;
	/** length of the variable part of the tuple */
	uint32_t bsize;
	/** number of fields in the variable part. */
	uint32_t field_count;
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
 * Create a new tuple from a sequence of BER-len encoded fields.
 * tuple->refs is 0.
 *
 * @post *data is advanced to the length of tuple data
 *
 * Throws an exception if tuple format is incorrect.
 */
struct tuple *
tuple_new(uint32_t field_count, const char **data, const char *end);

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
const char *
tuple_field_old(struct tuple *tuple, uint32_t i);

/**
 * @brief Return field data of the field
 * @param tuple tuple
 * @param field_no field number
 * @param field pointer where the start of field data will be stored
 * @param len pointer where the len of the field will be stored
 * @throws IllegalParams if \a field_no is out of range
 */
const char *
tuple_field(const struct tuple *tuple, uint32_t field_no, uint32_t *len);

/**
 * @brief Tuple Interator
 */
struct tuple_iterator {
	/** @cond false **/
	/* State */
	const struct tuple *tuple;
	/** Always points to the beginning of the next field. */
	const char *pos;
	/** @endcond **/
};

/**
 * @brief Initialize an iterator over tuple fields
 *
 * A workflow example:
 * @code
 * struct tuple_iterator it;
 * tuple_rewind(&it, tuple);
 * const char *field;
 * uint32_t len;
 * while ((field = tuple_next(&it, &len)))
 *	lua_pushlstring(L, field, len);
 *
 * @endcode
 *
 * @param[out] it tuple iterator
 * @param[in]  tuple tuple
 */
static inline void
tuple_rewind(struct tuple_iterator *it, const struct tuple *tuple)
{
	it->tuple = tuple;
	it->pos = tuple->data;
}

/**
 * @brief Position the iterator at a given field no.
 *
 * @retval field  if the iterator has the requested field
 * @retval NULL   otherwise (iteration is out of range)
 */
const char *
tuple_seek(struct tuple_iterator *it, uint32_t field_no, uint32_t *len);

/**
 * @brief Iterate to the next field
 * @param it tuple iterator
 * @return next field or NULL if the iteration is out of range
 */
const char *
tuple_next(struct tuple_iterator *it, uint32_t *len);

/**
 * @brief Print a tuple in yaml-compatible mode to tbuf:
 * key: { value, value, value }
 *
 * @param buf tbuf
 * @param tuple tuple
 */
void
tuple_print(struct tbuf *buf, const struct tuple *tuple);

struct tuple *
tuple_update(void *(*region_alloc)(void *, size_t), void *alloc_ctx,
	     const struct tuple *old_tuple,
	     const char *expr, const char *expr_end);

/** Tuple length when adding to iov. */
static inline size_t tuple_len(struct tuple *tuple)
{
	return tuple->bsize + sizeof(tuple->bsize) +
		sizeof(tuple->field_count);
}

static inline size_t
tuple_range_size(const char **begin, const char *end, uint32_t count)
{
	const char *start = *begin;
	while (*begin < end && count-- > 0) {
		size_t len = load_varint32(begin);
		*begin += len;
	}
	return *begin - start;
}

void tuple_free(struct tuple *tuple);

/**
 * @brief Compare two tuples using field by field using key definition
 * @param tuple_a tuple
 * @param tuple_b tuple
 * @param key_def key definition
 * @retval 0  if key_fields(tuple_a) == key_fields(tuple_b)
 * @retval <0 if key_fields(tuple_a) < key_fields(tuple_b)
 * @retval >0 if key_fields(tuple_a) > key_fields(tuple_b)
 */
int
tuple_compare(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def);

/**
 * @brief Compare two tuples field by field for duplicate using key definition
 * @param tuple_a tuple
 * @param tuple_b tuple
 * @param key_def key definition
 * @retval 0  if key_fields(tuple_a) == key_fields(tuple_b) and
 * tuple_a == tuple_b - tuple_a is the same object as tuple_b
 * @retval <0 if key_fields(tuple_a) <= key_fields(tuple_b)
 * @retval >0 if key_fields(tuple_a > key_fields(tuple_b)
 */
int
tuple_compare_dup(const struct tuple *tuple_a, const struct tuple *tuple_b,
		  const struct key_def *key_def);

/**
 * @brief Compare a tuple with a key field by field using key definition
 * @param tuple_a tuple
 * @param key BER-encoded key
 * @param part_count number of parts in \a key
 * @param key_def key definition
 * @retval 0  if key_fields(tuple_a) == parts(key)
 * @retval <0 if key_fields(tuple_a) < parts(key)
 * @retval >0 if key_fields(tuple_a) > parts(key)
 */
int
tuple_compare_with_key(const struct tuple *tuple_a, const char *key,
		       uint32_t part_count, const struct key_def *key_def);

/** These functions are implemented in tuple_convert.cc. */

/* Store tuple in the output buffer in iproto format. */
void
tuple_to_obuf(struct tuple *tuple, struct obuf *buf);

/* Store tuple fields in the Lua buffer, BER-length-encoded. */
void
tuple_to_luabuf(struct tuple *tuple, struct luaL_Buffer *b);

#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

