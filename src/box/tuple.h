#ifndef TARANTOOL_BOX_TUPLE_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "say.h"
#include "diag.h"
#include "error.h"
#include "tt_uuid.h" /* tuple_field_uuid */
#include "tuple_format.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct slab_arena;
struct quota;

/**
 * A format for standalone tuples allocated on runtime arena.
 * \sa tuple_new().
 */
extern struct tuple_format *tuple_format_runtime;

/** Initialize tuple library */
int
tuple_init(field_name_hash_f hash);

/** Cleanup tuple library */
void
tuple_free(void);

/**
 * Initialize tuples arena.
 * @param arena[out] Arena to initialize.
 * @param quota Arena's quota.
 * @param arena_max_size Maximal size of @arena.
 * @param arena_name Name of @arena for logs.
 */
void
tuple_arena_create(struct slab_arena *arena, struct quota *quota,
		   uint64_t arena_max_size, uint32_t slab_size,
		   const char *arena_name);

void
tuple_arena_destroy(struct slab_arena *arena);

/** \cond public */

typedef struct tuple_format box_tuple_format_t;

/**
 * Tuple Format.
 *
 * Each Tuple has associated format (class). Default format is used to
 * create tuples which are not attach to any particular space.
 */
box_tuple_format_t *
box_tuple_format_default(void);

/**
 * Tuple
 */
typedef struct tuple box_tuple_t;

/**
 * Increase the reference counter of tuple.
 *
 * Tuples are reference counted. All functions that return tuples guarantee
 * that the last returned tuple is refcounted internally until the next
 * call to API function that yields or returns another tuple.
 *
 * You should increase the reference counter before taking tuples for long
 * processing in your code. Such tuples will not be garbage collected even
 * if another fiber remove they from space. After processing please
 * decrement the reference counter using box_tuple_unref(), otherwise the
 * tuple will leak.
 *
 * \param tuple a tuple
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa box_tuple_unref()
 */
int
box_tuple_ref(box_tuple_t *tuple);

/**
 * Decrease the reference counter of tuple.
 *
 * \param tuple a tuple
 * \sa box_tuple_ref()
 */
void
box_tuple_unref(box_tuple_t *tuple);

/**
 * Return the number of fields in tuple (the size of MsgPack Array).
 * \param tuple a tuple
 */
uint32_t
box_tuple_field_count(const box_tuple_t *tuple);

/**
 * Return the number of bytes used to store internal tuple data (MsgPack Array).
 * \param tuple a tuple
 */
size_t
box_tuple_bsize(const box_tuple_t *tuple);

/**
 * Dump raw MsgPack data to the memory byffer \a buf of size \a size.
 *
 * Store tuple fields in the memory buffer.
 * \retval -1 on error.
 * \retval number of bytes written on success.
 * Upon successful return, the function returns the number of bytes written.
 * If buffer size is not enough then the return value is the number of bytes
 * which would have been written if enough space had been available.
 */
ssize_t
box_tuple_to_buf(const box_tuple_t *tuple, char *buf, size_t size);

/**
 * Return the associated format.
 * \param tuple tuple
 * \return tuple_format
 */
box_tuple_format_t *
box_tuple_format(const box_tuple_t *tuple);

/**
 * Return the raw tuple field in MsgPack format.
 *
 * The buffer is valid until next call to box_tuple_* functions.
 *
 * \param tuple a tuple
 * \param fieldno zero-based index in MsgPack array.
 * \retval NULL if i >= box_tuple_field_count(tuple)
 * \retval msgpack otherwise
 */
const char *
box_tuple_field(const box_tuple_t *tuple, uint32_t fieldno);

/**
 * Tuple iterator
 */
typedef struct tuple_iterator box_tuple_iterator_t;

/**
 * Allocate and initialize a new tuple iterator. The tuple iterator
 * allow to iterate over fields at root level of MsgPack array.
 *
 * Example:
 * \code
 * box_tuple_iterator *it = box_tuple_iterator(tuple);
 * if (it == NULL) {
 *      // error handling using box_error_last()
 * }
 * const char *field;
 * while (field = box_tuple_next(it)) {
 *      // process raw MsgPack data
 * }
 *
 * // rewind iterator to first position
 * box_tuple_rewind(it);
 * assert(box_tuple_position(it) == 0);
 *
 * // rewind iterator to first position
 * field = box_tuple_seek(it, 3);
 * assert(box_tuple_position(it) == 4);
 *
 * box_iterator_free(it);
 * \endcode
 *
 * \post box_tuple_position(it) == 0
 */
box_tuple_iterator_t *
box_tuple_iterator(box_tuple_t *tuple);

/**
 * Destroy and free tuple iterator
 */
void
box_tuple_iterator_free(box_tuple_iterator_t *it);

/**
 * Return zero-based next position in iterator.
 * That is, this function return the field id of field that will be
 * returned by the next call to box_tuple_next(it). Returned value is zero
 * after initialization or rewind and box_tuple_field_count(tuple)
 * after the end of iteration.
 *
 * \param it tuple iterator
 * \returns position.
 */
uint32_t
box_tuple_position(box_tuple_iterator_t *it);

/**
 * Rewind iterator to the initial position.
 *
 * \param it tuple iterator
 * \post box_tuple_position(it) == 0
 */
void
box_tuple_rewind(box_tuple_iterator_t *it);

/**
 * Seek the tuple iterator.
 *
 * The returned buffer is valid until next call to box_tuple_* API.
 * Requested fieldno returned by next call to box_tuple_next(it).
 *
 * \param it tuple iterator
 * \param fieldno - zero-based position in MsgPack array.
 * \post box_tuple_position(it) == fieldno if returned value is not NULL
 * \post box_tuple_position(it) == box_tuple_field_count(tuple) if returned
 * value is NULL.
 */
const char *
box_tuple_seek(box_tuple_iterator_t *it, uint32_t fieldno);

/**
 * Return the next tuple field from tuple iterator.
 * The returned buffer is valid until next call to box_tuple_* API.
 *
 * \param it tuple iterator.
 * \retval NULL if there are no more fields.
 * \retval MsgPack otherwise
 * \pre box_tuple_position(it) is zerod-based id of returned field
 * \post box_tuple_position(it) == box_tuple_field_count(tuple) if returned
 * value is NULL.
 */
const char *
box_tuple_next(box_tuple_iterator_t *it);

/**
 * Allocate and initialize a new tuple from a raw MsgPack Array data.
 *
 * \param format tuple format.
 * Use box_tuple_format_default() to create space-independent tuple.
 * \param data tuple data in MsgPack Array format ([field1, field2, ...]).
 * \param end the end of \a data
 * \retval NULL on out of memory
 * \retval tuple otherwise
 * \pre data, end is valid MsgPack Array
 * \sa \code box.tuple.new(data) \endcode
 */
box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end);

box_tuple_t *
box_tuple_update(const box_tuple_t *tuple, const char *expr, const
		 char *expr_end);

box_tuple_t *
box_tuple_upsert(const box_tuple_t *tuple, const char *expr, const
		 char *expr_end);

/** \endcond public */

/**
 * An atom of Tarantool storage. Represents MsgPack Array.
 * Tuple has the following structure:
 *                           format->tuple_meta_size      bsize
 *                          +------------------------+-------------+
 * tuple_begin, ..., raw =  |       tuple_meta       | MessagePack |
 * |                        +------------------------+-------------+
 * |                                                 ^
 * +---------------------------------------------data_offset
 *
 * tuple_meta structure:
 *   +----------------------+-----------------------+
 *   |      extra_size      | offset N ... offset 1 |
 *   +----------------------+-----------------------+
 *    @sa tuple_format_new()   uint32  ...  uint32
 *
 * Each 'off_i' is the offset to the i-th indexed field.
 */
struct PACKED tuple
{
	/** reference counter */
	uint16_t refs;
	/** format identifier */
	uint16_t format_id;
	/**
	 * Length of the MessagePack data in raw part of the
	 * tuple.
	 */
	uint32_t bsize;
	/**
	 * Offset to the MessagePack from the begin of the tuple.
	 */
	uint16_t data_offset;
	/**
	 * Engine specific fields and offsets array concatenated
	 * with MessagePack fields array.
	 * char raw[0];
	 */
};

/** Size of the tuple including size of struct tuple. */
static inline size_t
tuple_size(const struct tuple *tuple)
{
	/* data_offset includes sizeof(struct tuple). */
	return tuple->data_offset + tuple->bsize;
}

/**
 * Get pointer to MessagePack data of the tuple.
 * @param tuple tuple.
 * @return MessagePack array.
 */
static inline const char *
tuple_data(const struct tuple *tuple)
{
	return (const char *) tuple + tuple->data_offset;
}

/**
 * Wrapper around tuple_data() which returns NULL if @tuple == NULL.
 */
static inline const char *
tuple_data_or_null(const struct tuple *tuple)
{
	return tuple != NULL ? tuple_data(tuple) : NULL;
}

/**
 * Get pointer to MessagePack data of the tuple.
 * @param tuple tuple.
 * @param[out] size Size in bytes of the MessagePack array.
 * @return MessagePack array.
 */
static inline const char *
tuple_data_range(const struct tuple *tuple, uint32_t *p_size)
{
	*p_size = tuple->bsize;
	return (const char *) tuple + tuple->data_offset;
}

/**
 * Format a tuple into string.
 * Example: [1, 2, "string"]
 * @param buf buffer to format tuple to
 * @param size buffer size. This function writes at most @a size bytes
 * (including the terminating null byte ('\0')) to @a buffer
 * @param tuple tuple to format
 * @retval the number of characters printed, excluding the null byte used
 * to end output to string. If the output was truncated due to this limit,
 * then the return value is the number of characters (excluding the
 * terminating null byte) which would have been written to the final string
 * if enough space had been available.
 * @see snprintf
 * @see mp_snprint
 */
int
tuple_snprint(char *buf, int size, const struct tuple *tuple);

/**
 * Format a tuple into string using a static buffer.
 * Useful for debugger. Example: [1, 2, "string"]
 * @param tuple to format
 * @return formatted null-terminated string
 */
const char *
tuple_str(const struct tuple *tuple);

/**
 * Get the format of the tuple.
 * @param tuple Tuple.
 * @retval Tuple format instance.
 */
static inline struct tuple_format *
tuple_format(const struct tuple *tuple)
{
	struct tuple_format *format = tuple_format_by_id(tuple->format_id);
	assert(tuple_format_id(format) == tuple->format_id);
	return format;
}

/**
 * Return extra data saved in tuple metadata.
 * @param tuple tuple
 * @return a pointer to extra data saved in tuple metadata.
 */
static inline const char *
tuple_extra(const struct tuple *tuple)
{
	struct tuple_format *format = tuple_format(tuple);
	return tuple_data(tuple) - tuple_format_meta_size(format);
}

/**
 * Instantiate a new engine-independent tuple from raw MsgPack Array data
 * using runtime arena. Use this function to create a standalone tuple
 * from Lua or C procedures.
 *
 * \param format tuple format.
 * \param data tuple data in MsgPack Array format ([field1, field2, ...]).
 * \param end the end of \a data
 * \retval tuple on success
 * \retval NULL on out of memory
 * \sa \code box.tuple.new(data) \endcode
 */
struct tuple *
tuple_new(struct tuple_format *format, const char *data, const char *end);

/**
 * Free the tuple of any engine.
 * @pre tuple->refs  == 0
 */
static inline void
tuple_delete(struct tuple *tuple)
{
	say_debug("%s(%p)", __func__, tuple);
	assert(tuple->refs == 0);
	struct tuple_format *format = tuple_format(tuple);
	format->vtab.destroy(format, tuple);
}

/**
 * Check tuple data correspondence to space format.
 * Actually checks everything that checks tuple_init_field_map.
 * @param format Format to which the tuple must match.
 * @param tuple  MessagePack array.
 *
 * @retval  0 The tuple is valid.
 * @retval -1 The tuple is invalid.
 */
int
tuple_validate_raw(struct tuple_format *format, const char *data);

/**
 * Check tuple data correspondence to the space format.
 * @param format Format to which the tuple must match.
 * @param tuple  Tuple to validate.
 *
 * @retval  0 The tuple is valid.
 * @retval -1 The tuple is invalid.
 */
static inline int
tuple_validate(struct tuple_format *format, struct tuple *tuple)
{
	return tuple_validate_raw(format, tuple_data(tuple));
}

/*
 * Return a field map for the tuple.
 * @param tuple tuple
 * @returns a field map for the tuple.
 * @sa tuple_init_field_map()
 */
static inline const uint32_t *
tuple_field_map(const struct tuple *tuple)
{
	return (const uint32_t *) ((const char *) tuple + tuple->data_offset);
}

/**
 * @brief Return the number of fields in tuple
 * @param tuple
 * @return the number of fields in tuple
 */
static inline uint32_t
tuple_field_count(const struct tuple *tuple)
{
	const char *data = tuple_data(tuple);
	return mp_decode_array(&data);
}

/**
 * Get a field at the specific index in this tuple.
 * @param tuple tuple
 * @param fieldno the index of field to return
 * @param len pointer where the len of the field will be stored
 * @retval pointer to MessagePack data
 * @retval NULL when fieldno is out of range
 */
static inline const char *
tuple_field(const struct tuple *tuple, uint32_t fieldno)
{
	return tuple_field_raw(tuple_format(tuple), tuple_data(tuple),
			       tuple_field_map(tuple), fieldno);
}

/**
 * Get tuple field by its JSON path.
 * @param tuple Tuple to get field from.
 * @param path Field JSON path.
 * @param path_len Length of @a path.
 * @param path_hash Hash of @a path.
 * @param[out] field Found field, or NULL, if not found.
 *
 * @retval  0 Success.
 * @retval -1 Error in JSON path.
 */
static inline int
tuple_field_by_path(const struct tuple *tuple, const char *path,
                    uint32_t path_len, uint32_t path_hash,
                    const char **field)
{
	return tuple_field_raw_by_path(tuple_format(tuple), tuple_data(tuple),
	                               tuple_field_map(tuple), path, path_len,
	                               path_hash, field);
}

/**
 * Get tuple field by its name.
 * @param tuple Tuple to get field from.
 * @param name Field name.
 * @param name_len Length of @a name.
 * @param name_hash Hash of @a name.
 *
 * @retval not NULL MessagePack field.
 * @retval     NULL No field with @a name.
 */
static inline const char *
tuple_field_by_name(const struct tuple *tuple, const char *name,
		    uint32_t name_len, uint32_t name_hash)
{
	return tuple_field_raw_by_name(tuple_format(tuple), tuple_data(tuple),
				       tuple_field_map(tuple), name, name_len,
				       name_hash);
}

/**
 * @brief Tuple Interator
 */
struct tuple_iterator {
	/** @cond false **/
	/* State */
	struct tuple *tuple;
	/** Always points to the beginning of the next field. */
	const char *pos;
	/** End of the tuple. */
	const char *end;
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
tuple_rewind(struct tuple_iterator *it, struct tuple *tuple)
{
	it->tuple = tuple;
	uint32_t bsize;
	const char *data = tuple_data_range(tuple, &bsize);
	it->pos = data;
	(void) mp_decode_array(&it->pos); /* Skip array header */
	it->fieldno = 0;
	it->end = data + bsize;
}

/**
 * @brief Position the iterator at a given field no.
 *
 * @retval field  if the iterator has the requested field
 * @retval NULL   otherwise (iteration is out of range)
 */
const char *
tuple_seek(struct tuple_iterator *it, uint32_t fieldno);

/**
 * @brief Iterate to the next field
 * @param it tuple iterator
 * @return next field or NULL if the iteration is out of range
 */
const char *
tuple_next(struct tuple_iterator *it);

/**
 * Assert that buffer is valid MessagePack array
 * @param tuple buffer
 * @param the end of the buffer
 */
static inline void
mp_tuple_assert(const char *tuple, const char *tuple_end)
{
	assert(mp_typeof(*tuple) == MP_ARRAY);
#ifndef NDEBUG
	mp_next(&tuple);
#endif
	assert(tuple == tuple_end);
	(void) tuple;
	(void) tuple_end;
}

static inline const char *
tuple_field_with_type(const struct tuple *tuple, uint32_t fieldno,
		      enum mp_type type)
{
	const char *field = tuple_field(tuple, fieldno);
	if (field == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD,
			 fieldno + TUPLE_INDEX_BASE);
		return NULL;
	}
	if (mp_typeof(*field) != type) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 fieldno + TUPLE_INDEX_BASE, mp_type_strs[type]);
		return NULL;
	}
	return field;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as bool.
 */
static inline int
tuple_field_bool(const struct tuple *tuple, uint32_t fieldno, bool *out)
{
	const char *field = tuple_field_with_type(tuple, fieldno, MP_BOOL);
	if (field == NULL)
		return -1;
	*out = mp_decode_bool(&field);
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as int64_t.
 */
static inline int
tuple_field_i64(const struct tuple *tuple, uint32_t fieldno, int64_t *out)
{
	const char *field = tuple_field(tuple, fieldno);
	if (field == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD, fieldno);
		return -1;
	}
	uint64_t val;
	switch (mp_typeof(*field)) {
	case MP_INT:
		*out = mp_decode_int(&field);
		break;
	case MP_UINT:
		val = mp_decode_uint(&field);
		if (val <= INT64_MAX) {
			*out = val;
			break;
		}
		FALLTHROUGH;
	default:
		diag_set(ClientError, ER_FIELD_TYPE, fieldno + TUPLE_INDEX_BASE,
			 field_type_strs[FIELD_TYPE_INTEGER]);
		return -1;
	}
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as uint64_t.
 */
static inline int
tuple_field_u64(const struct tuple *tuple, uint32_t fieldno, uint64_t *out)
{
	const char *field = tuple_field_with_type(tuple, fieldno, MP_UINT);
	if (field == NULL)
		return -1;
	*out = mp_decode_uint(&field);
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as uint32_t.
 */
static inline int
tuple_field_u32(const struct tuple *tuple, uint32_t fieldno, uint32_t *out)
{
	const char *field = tuple_field_with_type(tuple, fieldno, MP_UINT);
	if (field == NULL)
		return -1;
	*out = mp_decode_uint(&field);
	if (*out > UINT32_MAX) {
		diag_set(ClientError, ER_FIELD_TYPE, fieldno + TUPLE_INDEX_BASE,
			 field_type_strs[FIELD_TYPE_UNSIGNED]);
		return -1;
	}
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as a string.
 */
static inline const char *
tuple_field_str(const struct tuple *tuple, uint32_t fieldno, uint32_t *len)
{
	const char *field = tuple_field_with_type(tuple, fieldno, MP_STR);
	if (field == NULL)
		return NULL;
	return mp_decode_str(&field, len);
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as a NUL-terminated string - returns a string of up to 256 bytes.
 */
static inline const char *
tuple_field_cstr(const struct tuple *tuple, uint32_t fieldno)
{
	uint32_t len;
	const char *str = tuple_field_str(tuple, fieldno, &len);
	if (str == NULL)
		return NULL;
	return tt_cstr(str, len);
}

/**
 * Parse a tuple field which is expected to contain a string
 * representation of UUID, and return a 16-byte representation.
 */
static inline int
tuple_field_uuid(const struct tuple *tuple, int fieldno,
		 struct tt_uuid *out)
{
	const char *value = tuple_field_cstr(tuple, fieldno);
	if (tt_uuid_from_string(value, out) != 0) {
		diag_set(ClientError, ER_INVALID_UUID, value);
		return -1;
	}
	return 0;
}

enum { TUPLE_REF_MAX = UINT16_MAX };

/**
 * Increment tuple reference counter.
 * @param tuple Tuple to reference.
 * @retval  0 Success.
 * @retval -1 Too many refs error.
 */
static inline int
tuple_ref(struct tuple *tuple)
{
	if (tuple->refs + 1 > TUPLE_REF_MAX) {
		diag_set(ClientError, ER_TUPLE_REF_OVERFLOW);
		return -1;
	}
	tuple->refs++;
	return 0;
}

/**
 * Decrement tuple reference counter. If it has reached zero, free the tuple.
 *
 * @pre tuple->refs + count >= 0
 */
static inline void
tuple_unref(struct tuple *tuple)
{
	assert(tuple->refs - 1 >= 0);

	tuple->refs--;

	if (tuple->refs == 0)
		tuple_delete(tuple);
}

extern struct tuple *box_tuple_last;

/**
 * Convert internal `struct tuple` to public `box_tuple_t`.
 * \retval tuple on success
 * \retval NULL on error, check diag
 * \post \a tuple ref counted until the next call.
 * \post tuple_ref() doesn't fail at least once
 * \sa tuple_ref
 */
static inline box_tuple_t *
tuple_bless(struct tuple *tuple)
{
	assert(tuple != NULL);
	/* Ensure tuple can be referenced at least once after return */
	if (tuple->refs + 2 > TUPLE_REF_MAX) {
		diag_set(ClientError, ER_TUPLE_REF_OVERFLOW);
		return NULL;
	}
	tuple->refs++;
	/* Remove previous tuple */
	if (likely(box_tuple_last != NULL))
		tuple_unref(box_tuple_last); /* do not throw */
	/* Remember current tuple */
	box_tuple_last = tuple;
	return tuple;
}

/**
 * \copydoc box_tuple_to_buf()
 */
ssize_t
tuple_to_buf(const struct tuple *tuple, char *buf, size_t size);

#if defined(__cplusplus)
} /* extern "C" */

#include "tuple_update.h"
#include "errinj.h"

/**
 * \copydoc tuple_ref()
 * \throws if overflow detected.
 */
static inline void
tuple_ref_xc(struct tuple *tuple)
{
	if (tuple_ref(tuple))
		diag_raise();
}

/**
 * \copydoc tuple_bless
 * \throw ER_TUPLE_REF_OVERFLOW
 */
static inline box_tuple_t *
tuple_bless_xc(struct tuple *tuple)
{
	box_tuple_t *blessed = tuple_bless(tuple);
	if (blessed == NULL)
		diag_raise();
	return blessed;
}

/** Make tuple references exception-friendly in absence of @finally. */
struct TupleRefNil {
	struct tuple *tuple;
	TupleRefNil (struct tuple *arg) :tuple(arg)
	{ if (tuple) tuple_ref_xc(tuple); }
	~TupleRefNil() { if (tuple) tuple_unref(tuple); }

	TupleRefNil(const TupleRefNil&) = delete;
	void operator=(const TupleRefNil&) = delete;
};

/* @copydoc tuple_field_with_type() */
static inline const char *
tuple_field_with_type_xc(const struct tuple *tuple, uint32_t fieldno,
		         enum mp_type type)
{
	const char *out = tuple_field_with_type(tuple, fieldno, type);
	if (out == NULL)
		diag_raise();
	return out;
}

/* @copydoc tuple_field_bool() */
static inline bool
tuple_field_bool_xc(const struct tuple *tuple, uint32_t fieldno)
{
	bool out;
	if (tuple_field_bool(tuple, fieldno, &out) != 0)
		diag_raise();
	return out;
}

/* @copydoc tuple_field_i64() */
static inline int64_t
tuple_field_i64_xc(const struct tuple *tuple, uint32_t fieldno)
{
	int64_t out;
	if (tuple_field_i64(tuple, fieldno, &out) != 0)
		diag_raise();
	return out;
}

/* @copydoc tuple_field_u64() */
static inline uint64_t
tuple_field_u64_xc(const struct tuple *tuple, uint32_t fieldno)
{
	uint64_t out;
	if (tuple_field_u64(tuple, fieldno, &out) != 0)
		diag_raise();
	return out;
}

/* @copydoc tuple_field_u32() */
static inline uint32_t
tuple_field_u32_xc(const struct tuple *tuple, uint32_t fieldno)
{
	uint32_t out;
	if (tuple_field_u32(tuple, fieldno, &out) != 0)
		diag_raise();
	return out;
}

/** @copydoc tuple_field_str() */
static inline const char *
tuple_field_str_xc(const struct tuple *tuple, uint32_t fieldno,
			uint32_t *len)
{
	const char *ret = tuple_field_str(tuple, fieldno, len);
	if (ret == NULL)
		diag_raise();
	return ret;
}

/** @copydoc tuple_field_cstr() */
static inline const char *
tuple_field_cstr_xc(const struct tuple *tuple, uint32_t fieldno)
{
	const char *out = tuple_field_cstr(tuple, fieldno);
	if (out == NULL)
		diag_raise();
	return out;
}

/** @copydoc tuple_field_uuid() */
static inline void
tuple_field_uuid_xc(const struct tuple *tuple, int fieldno,
		    struct tt_uuid *out)
{
	if (tuple_field_uuid(tuple, fieldno, out) != 0)
		diag_raise();
}

/** Return a tuple field and check its type. */
static inline const char *
tuple_next_xc(struct tuple_iterator *it, enum mp_type type)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next(it);
	if (field == NULL)
		tnt_raise(ClientError, ER_NO_SUCH_FIELD, it->fieldno);
	if (mp_typeof(*field) != MP_UINT) {
		tnt_raise(ClientError, ER_FIELD_TYPE,
			  fieldno + TUPLE_INDEX_BASE,
			  mp_type_strs[type]);
	}

	return field;
}

/**
 * A convenience shortcut for the data dictionary - get next field
 * from iterator as uint32_t or raise an error if there is
 * no next field.
 */
static inline uint32_t
tuple_next_u32_xc(struct tuple_iterator *it)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next_xc(it, MP_UINT);
	uint32_t val = mp_decode_uint(&field);
	if (val > UINT32_MAX) {
		tnt_raise(ClientError, ER_FIELD_TYPE,
			  fieldno + TUPLE_INDEX_BASE,
			  field_type_strs[FIELD_TYPE_UNSIGNED]);
	}
	return val;
}

/**
 * A convenience shortcut for the data dictionary - get next field
 * from iterator as a C string or raise an error if there is no
 * next field.
 */
static inline const char *
tuple_next_cstr_xc(struct tuple_iterator *it)
{
	const char *field = tuple_next_xc(it, MP_STR);
	uint32_t len = 0;
	const char *str = mp_decode_str(&field, &len);
	return tt_cstr(str, len);
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

