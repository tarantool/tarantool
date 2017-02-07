#ifndef INCLUDES_TARANTOOL_BOX_VY_STMT_H
#define INCLUDES_TARANTOOL_BOX_VY_STMT_H
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <trivia/util.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <msgpuck.h>
#include <bit/bit.h>

#include "tuple.h"
#include "tuple_compare.h"
#include "iproto_constants.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;
struct region;
struct tuple_format;
struct iovec;

/**
 * There are two groups of statements:
 *
 *  - SELECT is "key" statement.
 *  - DELETE, UPSERT and REPLACE are "tuple" statements.
 *
 * REPLACE/UPSERT/DELETE statements structure:
 *                               data_offset
 *                                    ^
 * +----------------------------------+
 * |               4 bytes      4 bytes     MessagePack data.
 * |               +------+----+------+---------------------------+- - - - - - .
 *tuple, ..., raw: | offN | .. | off1 | header ..|key1|..|keyN|.. | operations |
 *                 +--+---+----+--+---+---------------------------+- - - - - - .
 *                 |     ...    |                 ^       ^
 *                 |            +-----------------+       |
 *                 +--------------------------------------+
 * Offsets are stored only for indexed fields, though MessagePack'ed tuple data
 * can contain also not indexed fields. For example, if fields 3 and 5 are
 * indexed then before MessagePack data are stored offsets only for field 3 and
 * field 5.
 *
 * SELECT statements structure.
 * +--------------+-----------------+
 * | array header | part1 ... partN |  -  MessagePack data
 * +--------------+-----------------+
 *
 * Field 'operations' is used for storing operations of UPSERT statement.
 */
struct vy_stmt {
	struct tuple base;
	int64_t lsn;
	uint8_t  type; /* IPROTO_SELECT/REPLACE/UPSERT/DELETE */
	/**
	 * Number of UPSERT statements for the same key preceding
	 * this statement. Used to trigger upsert squashing in the
	 * background (see vy_range_set_upsert()).
	 */
	uint8_t n_upserts;
	/** Offsets count before MessagePack data. */
	/**
	 * Offsets array concatenated with MessagePack fields
	 * array.
	 * char raw[0];
	 */
};

/** Get LSN of the vinyl statement. */
static inline int64_t
vy_stmt_lsn(const struct tuple *stmt)
{
	return ((const struct vy_stmt *) stmt)->lsn;
}

/** Set LSN of the vinyl statement. */
static inline void
vy_stmt_set_lsn(struct tuple *stmt, int64_t lsn)
{
	((struct vy_stmt *) stmt)->lsn = lsn;
}

/** Get type of the vinyl statement. */
static inline enum iproto_type
vy_stmt_type(const struct tuple *stmt)
{
	return (enum iproto_type)((const struct vy_stmt *) stmt)->type;
}

/** Set type of the vinyl statement. */
static inline void
vy_stmt_set_type(struct tuple *stmt, enum iproto_type type)
{
	((struct vy_stmt *) stmt)->type = type;
}

/** Get upserts count of the vinyl statement. */
static inline uint8_t
vy_stmt_n_upserts(const struct tuple *stmt)
{
	return ((const struct vy_stmt *) stmt)->n_upserts;
}

/** Set upserts count of the vinyl statement. */
static inline void
vy_stmt_set_n_upserts(struct tuple *stmt, uint8_t n)
{
	((struct vy_stmt *) stmt)->n_upserts = n;
}

/** Get the column mask of the specified tuple. */
static inline uint64_t
vy_stmt_column_mask(const struct tuple *tuple)
{
#ifndef NDEBUG
	enum iproto_type type = vy_stmt_type(tuple);
	assert(type == IPROTO_REPLACE || type == IPROTO_DELETE);
#endif
	const char *meta = tuple_meta(tuple);
	return load_u64(meta);
}

/**
 * Set the column mask in the tuple.
 * @param tuple       Tuple to set column mask.
 * @param column_mask Bitmask of the updated columns.
 */
static inline void
vy_stmt_set_column_mask(struct tuple *tuple, uint64_t column_mask)
{
	assert(tuple_meta_size(tuple) == sizeof(uint64_t));
#ifndef NDEBUG
	enum iproto_type type = vy_stmt_type(tuple);
	assert(type == IPROTO_REPLACE || type == IPROTO_DELETE);
#endif
	char *meta = (char *) tuple_meta(tuple);
	store_u64(meta, column_mask);
}

/**
 * Free the tuple of a vinyl space.
 * @pre tuple->refs  == 0
 */
void
vy_tuple_delete(struct tuple_format *format, struct tuple *tuple);

/**
 * Duplicate statememnt.
 *
 * @param stmt statement
 * @return new statement of the same type with the same data.
 */
struct tuple *
vy_stmt_dup(const struct tuple *stmt, struct tuple_format *format);

/**
 * Specialized comparators are faster than general-purpose comparators.
 * For example, vy_stmt_compare - slowest comparator because it in worst case
 * checks all combinations of key and tuple types, but
 * vy_key_compare - fastest comparator, because it shouldn't check statement
 * types.
 */

/**
 * Compare SELECT/DELETE statements using the key definition
 * @param a left operand (SELECT/DELETE)
 * @param b right operand (SELECT/DELETE)
 * @param key_def key definition
 *
 * @retval 0   if a == b
 * @retval > 0 if a > b
 * @retval < 0 if a < b
 *
 * @sa key_compare()
 */
static inline int
vy_key_compare(const struct tuple *a, const struct tuple *b,
	       const struct key_def *key_def)
{
	assert(vy_stmt_type(a) == IPROTO_SELECT);
	assert(vy_stmt_type(b) == IPROTO_SELECT);
	return key_compare(tuple_data(a), tuple_data(b), key_def);
}

/**
 * Compare REPLACE/UPSERTS statements.
 * @param a left operand (REPLACE/UPSERT)
 * @param b right operand (REPLACE/UPSERT)
 * @param key_def key definition
 *
 * @retval 0   if a == b
 * @retval > 0 if a > b
 * @retval < 0 if a < b
 *
 * @sa tuple_compare()
 */
static inline int
vy_tuple_compare(const struct tuple *a, const struct tuple *b,
		 const struct key_def *key_def)
{
	assert(vy_stmt_type(a) == IPROTO_REPLACE ||
	       vy_stmt_type(a) == IPROTO_UPSERT ||
	       vy_stmt_type(a) == IPROTO_DELETE);
	assert(vy_stmt_type(b) == IPROTO_REPLACE ||
	       vy_stmt_type(b) == IPROTO_UPSERT ||
	       vy_stmt_type(b) == IPROTO_DELETE);
	return tuple_compare(a, b, key_def);
}

/**
 * Compare REPLACE/UPSERT with SELECT/DELETE using the key
 * definition
 * @param tuple Left operand (REPLACE/UPSERT)
 * @param key   MessagePack array of key fields, right operand.
 *
 * @retval > 0  tuple > key.
 * @retval == 0 tuple == key in all fields
 * @retval == 0 tuple is prefix of key
 * @retval == 0 key is a prefix of tuple
 * @retval < 0  tuple < key.
 *
 * @sa tuple_compare_with_key()
 */
static inline int
vy_tuple_compare_with_raw_key(const struct tuple *tuple, const char *key,
			      const struct key_def *key_def)
{
	uint32_t part_count = mp_decode_array(&key);
	return tuple_compare_with_key(tuple, key, part_count, key_def);
}

/** @sa vy_tuple_compare_with_raw_key(). */
static inline int
vy_tuple_compare_with_key(const struct tuple *tuple, const struct tuple *key,
			  const struct key_def *key_def)
{
	const char *key_mp = tuple_data(key);
	uint32_t part_count = mp_decode_array(&key_mp);
	return tuple_compare_with_key(tuple, key_mp, part_count, key_def);
}

/** @sa tuple_compare. */
static inline int
vy_stmt_compare(const struct tuple *a, const struct tuple *b,
		const struct key_def *key_def)
{
	bool a_is_tuple = vy_stmt_type(a) == IPROTO_REPLACE ||
			  vy_stmt_type(a) == IPROTO_UPSERT ||
			  vy_stmt_type(a) == IPROTO_DELETE;
	bool b_is_tuple = vy_stmt_type(b) == IPROTO_REPLACE ||
			  vy_stmt_type(b) == IPROTO_UPSERT ||
			  vy_stmt_type(b) == IPROTO_DELETE;
	if (a_is_tuple && b_is_tuple) {
		return vy_tuple_compare(a, b, key_def);
	} else if (a_is_tuple && !b_is_tuple) {
		return vy_tuple_compare_with_key(a, b, key_def);
	} else if (!a_is_tuple && b_is_tuple) {
		return -vy_tuple_compare_with_key(b, a, key_def);
	} else {
		assert(!a_is_tuple && !b_is_tuple);
		return vy_key_compare(a, b, key_def);
	}
}

/** @sa tuple_compare_with_raw_key. */
static inline int
vy_stmt_compare_with_raw_key(const struct tuple *stmt, const char *key,
			     const struct key_def *key_def)
{
	if (vy_stmt_type(stmt) == IPROTO_REPLACE ||
	    vy_stmt_type(stmt) == IPROTO_UPSERT ||
	    vy_stmt_type(stmt) == IPROTO_DELETE)
		return vy_tuple_compare_with_raw_key(stmt, key, key_def);
	return key_compare(tuple_data(stmt), key, key_def);
}

/**
 * Create the SELECT statement from raw MessagePack data.
 * @param format     Format of an index.
 * @param key        MessagePack data that contain an array of
 *                   fields WITHOUT the array header.
 * @param part_count Count of the key fields that will be saved as
 *                   result.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct tuple *
vy_stmt_new_select(struct tuple_format *format, const char *key,
		   uint32_t part_count);

/**
 * Copy the key in a new memory area.
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
char *
vy_key_dup(const char *key);

/**
 * Create a new surrogate DELETE from @a key using format.
 *
 * Example:
 * key: {a3, a5}
 * key_def: { 3, 5 }
 * result: {nil, nil, a3, nil, a5}
 *
 * @param format  Target tuple format.
 * @param key    MessagePack array with key fields.
 * @param def    Definition of the result statement.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format error.
 */
struct tuple *
vy_stmt_new_surrogate_delete_from_key(struct tuple_format *format,
				      const char *key,
				      const struct key_def *def);

/**
 * Create a new surrogate DELETE from @a tuple using @a format.
 * A surrogate tuple has format->field_count fields from the source
 * with all unindexed fields replaced with MessagePack NIL.
 *
 * Example:
 * original:      {a1, a2, a3, a4, a5}
 * index key_def: {2, 4}
 * result:        {null, a2, null, a4, null}
 *
 * @param format Target tuple format.
 * @param src    Source tuple from the primary index.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or fields format error.
 */
struct tuple *
vy_stmt_new_surrogate_delete(struct tuple_format *format,
			     const struct tuple *tuple);

/**
 * @copydoc vy_stmt_new_surrogate_replace()
 */
struct tuple *
vy_stmt_new_surrogate_replace(struct tuple_format *format,
			      const struct tuple *tuple);

/**
 * Create the REPLACE statement from raw MessagePack data.
 * @param format Format of a tuple for offsets generating.
 * @param tuple_begin MessagePack data that contain an array of fields WITH the
 *                    array header.
 * @param tuple_end End of the array that begins from @param tuple_begin.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct tuple *
vy_stmt_new_replace(struct tuple_format *format, const char *tuple,
                    const char *tuple_end);

 /**
 * Create the UPSERT statement from raw MessagePack data.
 * @param tuple_begin MessagePack data that contain an array of fields WITH the
 *                    array header.
 * @param tuple_end End of the array that begins from @param tuple_begin.
 * @param format Format of a tuple for offsets generating.
 * @param part_count Part count from key definition.
 * @param operations Vector of update operations.
 * @param ops_cnt Length of the update operations vector.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct tuple *
vy_stmt_new_upsert(struct tuple_format *format,
		   const char *tuple_begin, const char *tuple_end,
		   struct iovec *operations, uint32_t ops_cnt);

/**
 * Create REPLACE statement from UPSERT statement.
 *
 * @param upsert upsert statement.
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
struct tuple *
vy_stmt_replace_from_upsert(const struct tuple *upsert);

/**
 * Extract MessagePack data from the REPLACE/UPSERT statement.
 * @param stmt An UPSERT or REPLACE statement.
 * @param[out] p_size Size of the MessagePack array in bytes.
 *
 * @return MessagePack array of tuple fields.
 */
static inline const char *
vy_upsert_data_range(const struct tuple *tuple, uint32_t *p_size)
{
	assert(vy_stmt_type(tuple) == IPROTO_UPSERT);
	const char *mp = tuple_data(tuple);
	assert(mp_typeof(*mp) == MP_ARRAY);
	const char *mp_end = mp;
	mp_next(&mp_end);
	assert(mp < mp_end);
	*p_size = mp_end - mp;
	return mp;
}

/**
 * Extract the operations array from the UPSERT statement.
 * @param stmt An UPSERT statement.
 * @param mp_size Out parameter for size of the returned array.
 *
 * @retval Pointer on MessagePack array of update operations.
 */
static inline const char *
vy_stmt_upsert_ops(const struct tuple *tuple, uint32_t *mp_size)
{
	assert(vy_stmt_type(tuple) == IPROTO_UPSERT);
	const char *mp = tuple_data(tuple);
	mp_next(&mp);
	*mp_size = tuple_data(tuple) + tuple->bsize - mp;
	return mp;
}

/**
 * Extract a SELECT statement with only indexed fields from raw data.
 * @param stmt Raw data of struct vy_stmt.
 * @param key_def key definition.
 * @param a region for temporary allocations. Automatically shrinked
 * to the original size.
 *
 * @retval not NULL Success.
 * @retval NULL Memory allocation error.
 */
struct tuple *
vy_stmt_extract_key(const struct tuple *stmt, const struct key_def *key_def,
		    struct region *gc);


/**
 * Create the SELECT statement from MessagePack array.
 * @param format  Format of an index.
 * @param key     MessagePack array of key fields.
 * @param key_def Definition of the key.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
static inline struct tuple *
vy_key_from_msgpack(struct tuple_format *format, const char *key)
{
	uint32_t part_count;
	/*
	 * The statement already is a key, so simply copy it in
	 * the new struct vy_stmt as SELECT.
	 */
	part_count = mp_decode_array(&key);
	return vy_stmt_new_select(format, key, part_count);
}

/**
 * Encode vy_stmt as xrow_header
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
vy_stmt_encode(const struct tuple *value, const struct key_def *key_def,
	       struct xrow_header *xrow);

/**
 * Reconstruct vinyl tuple info and data from xrow
 *
 * @retval stmt on success
 * @retval NULL on error
 */
struct tuple *
vy_stmt_decode(struct xrow_header *xrow, struct tuple_format *format,
	       const struct key_def *def);

/**
 * Format a key into string.
 * Example: [1, 2, "string"]
 * \sa mp_snprint()
 */
int
vy_key_snprint(char *buf, int size, const char *key);

/**
 * Format a statement into string.
 * Example: REPLACE([1, 2, "string"], lsn=48)
 */
int
vy_stmt_snprint(char *buf, int size, const struct tuple *stmt);

/*
* Format a key into string using a static buffer.
* Useful for gdb and say_debug().
* \sa vy_key_snprint()
*/
const char *
vy_key_str(const char *key);

/*
* Format a statement into string using a static buffer.
* Useful for gdb and say_debug().
* \sa vy_stmt_snprint()
*/
const char *
vy_stmt_str(const struct tuple *stmt);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STMT_H */
