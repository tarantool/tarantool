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
#include "trivia/util.h"
#include "key_def.h" /* for enum field_type */
#include <pickle.h>

enum { FORMAT_ID_MAX = UINT16_MAX - 1, FORMAT_ID_NIL = UINT16_MAX };

struct tbuf;

extern struct small_alloc talloc;
extern struct slab_arena tuple_arena;

/**
 * @brief In-memory tuple format
 */
struct tuple_format {
	uint16_t id;
	/* Format objects are reference counted. */
	int refs;
	/**
	 * Max field no which participates in any of the space
	 * indexes. Each tuple of this format must have,
	 * therefore, at least max_fieldno fields.
	 *
	 */
	uint32_t max_fieldno;
	/* Length of 'types' and 'offset' arrays. */
	uint32_t field_count;
	/**
	 * Field types of indexed fields. This is an array of size
	 * field_count. If there are gaps, i.e. fields that do not
	 * participate in any index and thus we cannot infer their
	 * type, then respective array members have value UNKNOWN.
	 */
	enum field_type *types;
	/**
	 * Each tuple has an area with field offsets. This area
	 * is located in front of the tuple. It is used to quickly
	 * find field start inside tuple data. This area only
	 * stores offsets of fields preceded with fields of
	 * dynamic length. If preceding fields have a fixed
	 * length, field offset can be calculated once for all
	 * tuples and thus is stored directly in the format object.
	 * The variable below stores the size of field map in the
	 * tuple, *in bytes*.
	 */
	uint32_t field_map_size;
	/**
	 * For each field participating in an index, the format
	 * may either store the fixed offset of the field
	 * (identical in all tuples with this format), or an
	 * offset in the dynamic offset map (field_map), which,
	 * in turn, stores the offset of the field (such offset is
	 * varying between different tuples of the same format).
	 * If an offset is fixed, it's positive, so that
	 * tuple->data[format->offset[fieldno] gives the
	 * start of the field.
	 * If it is varying, it's negative, so that
	 * tuple->data[((uint32_t *) * tuple)[format->offset[fieldno]]]
	 * gives the start of the field.
	 */
	int32_t offset[0];
};

extern struct tuple_format **tuple_formats;
/**
 * Default format for a tuple which does not belong
 * to any space and is stored in memory.
 */
extern struct tuple_format *tuple_format_ber;

static inline uint32_t
tuple_format_id(struct tuple_format *format)
{
	assert(tuple_formats[format->id] == format);
	return format->id;
}

/**
 * @brief Allocate, construct and register a new in-memory tuple
 *	 format.
 * @param space description
 *
 * @return tuple format or raise an exception on error
 */
struct tuple_format *
tuple_format_new(struct rlist *key_list);

/** Delete a format with zero ref count. */
void
tuple_format_delete(struct tuple_format *format);

static inline void
tuple_format_ref(struct tuple_format *format, int count)
{
	assert(format->refs + count >= 0);
	format->refs += count;
	if (format->refs == 0)
		tuple_format_delete(format);

};

/**
 * An atom of Tarantool/Box storage. Consists of a list of fields.
 * The first field is always the primary key.
 */
struct tuple
{
	/** snapshot generation version */
	uint32_t version;
	/** reference counter */
	uint16_t refs;
	/** format identifier */
	uint16_t format_id;
	/** length of the variable part of the tuple */
	uint32_t bsize;
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
tuple_alloc(struct tuple_format *format, size_t size);

/**
 * Create a new tuple from a sequence of BER-len encoded fields.
 * tuple->refs is 0.
 *
 * @post *data is advanced to the length of tuple data
 *
 * Throws an exception if tuple format is incorrect.
 */
struct tuple *
tuple_new(struct tuple_format *format, const char *data, const char *end);

/**
 * Change tuple reference counter. If it has reached zero, free the tuple.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct tuple *tuple, int count);

void
tuple_delete(struct tuple *tuple);

/** Make tuple references exception-friendly in absence of @finally. */
struct TupleGuard {
	struct tuple *tuple;
	TupleGuard(struct tuple *arg) :tuple(arg) {}
	~TupleGuard() { if (tuple->refs == 0) tuple_delete(tuple); }
};

/**
* @brief Return a tuple format instance
* @param tuple tuple
* @return tuple format instance
*/
static inline struct tuple_format *
tuple_format(const struct tuple *tuple)
{
	struct tuple_format *format = tuple_formats[tuple->format_id];
	assert(tuple_format_id(format) == tuple->format_id);
	return format;
}

/**
 * @brief Return the number of fields in tuple
 * @param tuple
 * @return the number of fields in tuple
 */
static inline uint32_t
tuple_arity(const struct tuple *tuple)
{
	const char *data = tuple->data;
	return mp_decode_array(&data);
}

/**
 * Get a field from tuple by index.
 * Returns a pointer to BER-length prefixed field.
 *
 * @pre field < tuple->field_count.
 * @returns field data if field exists or NULL
 */
static inline const char *
tuple_field_old(const struct tuple_format *format,
		const struct tuple *tuple, uint32_t i)
{
	if (likely(i < format->field_count)) {
		/* Indexed field */

		if (i == 0) {
			const char *pos = tuple->data;
			mp_decode_array(&pos);
			return pos;
		}

		if (format->offset[i] != INT32_MIN) {
			uint32_t *field_map = (uint32_t *) tuple;
			int32_t idx = format->offset[i];
			return tuple->data + field_map[idx];
		}
	}

	const char *pos = tuple->data;
	uint32_t size = mp_decode_array(&pos);
	if (unlikely(i >= size))
		return NULL;

	for (uint32_t k = 0; k < i; k++) {
		mp_next(&pos);
	}

	assert(pos <= tuple->data + tuple->bsize);
	return pos;
}

/**
 * @brief Return field data of the field
 * @param tuple tuple
 * @param field_no field number
 * @param field pointer where the start of field data will be stored,
 *        or NULL if field is out of range
 * @param len pointer where the len of the field will be stored
 */
inline const char *
tuple_field(const struct tuple *tuple, uint32_t i)
{
	return tuple_field_old(tuple_format(tuple), tuple, i);
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as uint32_t.
 */
inline uint32_t
tuple_field_u32(struct tuple *tuple, uint32_t i)
{
	const char *field = tuple_field(tuple, i);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, i);
	if (mp_typeof(*field) != MP_UINT)
		tnt_raise(ClientError, ER_FIELD_TYPE, i, field_type_strs[NUM]);

	uint64_t val = mp_decode_uint(&field);
	if (val > UINT32_MAX)
		tnt_raise(ClientError, ER_FIELD_TYPE, i, field_type_strs[NUM]);
	return (uint32_t) val;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as a NUL-terminated string - returns a string of up to 256 bytes.
 */
const char *
tuple_field_cstr(struct tuple *tuple, uint32_t i);

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
	/** field no of the next field. */
	int fieldno;
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
	(void) mp_decode_array(&it->pos); /* Skip array header */
	it->fieldno = 0;
}

/**
 * @brief Position the iterator at a given field no.
 *
 * @retval field  if the iterator has the requested field
 * @retval NULL   otherwise (iteration is out of range)
 */
const char *
tuple_seek(struct tuple_iterator *it, uint32_t field_no);

/**
 * @brief Iterate to the next field
 * @param it tuple iterator
 * @return next field or NULL if the iteration is out of range
 */
const char *
tuple_next(struct tuple_iterator *it);

/**
 * A convenience shortcut for the data dictionary - get next field
 * from iterator as uint32_t or raise an error if there is
 * no next field.
 */
inline uint32_t
tuple_next_u32(struct tuple_iterator *it)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next(it);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, it->fieldno);
	if (mp_typeof(*field) != MP_UINT)
		tnt_raise(ClientError, ER_FIELD_TYPE, fieldno,
			  field_type_strs[NUM]);

	uint32_t val = mp_decode_uint(&field);
	if (val > UINT32_MAX)
		tnt_raise(ClientError, ER_FIELD_TYPE, fieldno,
			  field_type_strs[NUM]);
	return (uint32_t) val;
}

/**
 * A convenience shortcut for the data dictionary - get next field
 * from iterator as a C string or raise an error if there is no
 * next field.
 */
const char *
tuple_next_cstr(struct tuple_iterator *it);

void
tuple_init_field_map(struct tuple_format *format,
		     struct tuple *tuple, uint32_t *field_map);

struct tuple *
tuple_update(struct tuple_format *new_format,
	     void *(*region_alloc)(void *, size_t), void *alloc_ctx,
	     const struct tuple *old_tuple,
	     const char *expr, const char *expr_end);

/** Tuple length when adding to iov. */
static inline size_t tuple_len(struct tuple *tuple)
{
	return tuple->bsize + sizeof(tuple->bsize);
}

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

/* Store tuple fields in the tbuf, BER-length-encoded. */
void
tuple_to_tbuf(struct tuple *tuple, struct tbuf *buf);

/** Initialize tuple library */
void
tuple_init(float slab_alloc_arena, uint32_t slab_alloc_minimal,
	   float alloc_factor);

/** Cleanup tuple library */
void
tuple_free();

void
tuple_begin_snapshot();

void
tuple_end_snapshot();
#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

