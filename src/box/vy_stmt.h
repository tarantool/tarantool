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
	 * background (see vy_range_set_upsert()). This member is
	 * stored only for UPSERT statements in the extra memory
	 * space before offsets table.
	 *
	 *     uint8_t n_upserts;
	 *
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
	assert(tuple_format(stmt)->extra_size == sizeof(uint8_t));
	return *((const uint8_t *) tuple_extra(stmt));
}

/** Set upserts count of the vinyl statement. */
static inline void
vy_stmt_set_n_upserts(struct tuple *stmt, uint8_t n)
{
	struct tuple_format *format = tuple_format(stmt);
	assert(format->extra_size == sizeof(uint8_t));
	char *extra = (char *) stmt + stmt->data_offset -
		      tuple_format_meta_size(format);
	*((uint8_t *) extra) = n;
}

/** Get the column mask of the specified tuple. */
static inline uint64_t
vy_stmt_column_mask(const struct tuple *tuple)
{
	enum iproto_type type = vy_stmt_type(tuple);
	assert(type == IPROTO_REPLACE || type == IPROTO_DELETE);
	(void) type;
	if (tuple_format(tuple)->extra_size == sizeof(uint64_t)) {
		/* Tuple has column mask */
		const char *extra = tuple_extra(tuple);
		return load_u64(extra);
	}
	return UINT64_MAX; /* return default value */
}

/**
 * Set the column mask in the tuple.
 * @param tuple       Tuple to set column mask.
 * @param column_mask Bitmask of the updated columns.
 */
static inline void
vy_stmt_set_column_mask(struct tuple *tuple, uint64_t column_mask)
{
	enum iproto_type type = vy_stmt_type(tuple);
	assert(type == IPROTO_REPLACE || type == IPROTO_DELETE);
	assert(tuple_format(tuple)->extra_size == sizeof(uint64_t));
	(void) type;
	char *extra = (char *) tuple_extra(tuple);
	store_u64(extra, column_mask);
}

/**
 * Free the tuple of a vinyl space.
 * @pre tuple->refs  == 0
 */
void
vy_tuple_delete(struct tuple_format *format, struct tuple *tuple);

/**
 * Duplicate the statememnt.
 *
 * @param stmt statement
 * @return new statement of the same type with the same data.
 */
struct tuple *
vy_stmt_dup(const struct tuple *stmt, struct tuple_format *format);

struct lsregion;

/**
 * Duplicate the statement, using the lsregion as allocator.
 * @param stmt      Statement to duplicate.
 * @param lsregion  Allocator.
 * @param alloc_id  Allocation identifier for the lsregion.
 *
 * @retval not NULL The new statement with the same data.
 * @retval     NULL Memory error.
 */
struct tuple *
vy_stmt_dup_lsregion(const struct tuple *stmt, struct lsregion *lsregion,
		     int64_t alloc_id);

/**
 * Return true if @a stmt was allocated on lsregion.
 * @param stmt a statement
 * @retval true if @a stmt was allocated on lsregion
 * @retval false otherwise
 */
static inline bool
vy_stmt_is_region_allocated(const struct tuple *stmt)
{
	return stmt->refs == 0;
}

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

/** @sa tuple_compare_with_key. */
static inline int
vy_stmt_compare_with_key(const struct tuple *stmt, const struct tuple *key,
			 const struct key_def *key_def)
{
	assert(vy_stmt_type(key) == IPROTO_SELECT);
	return vy_stmt_compare_with_raw_key(stmt, tuple_data(key), key_def);
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
 * @param key     MessagePack array with key fields.
 * @param key_def Key definition of the result statement.
 * @param format  Target tuple format.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format error.
 */
struct tuple *
vy_stmt_new_surrogate_delete_from_key(const char *key,
				      const struct key_def *def,
				      struct tuple_format *format);

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
 * @param replace_format Format for new REPLACE statement.
 * @param upsert         Upsert statement.
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
struct tuple *
vy_stmt_replace_from_upsert(struct tuple_format *replace_format,
			    const struct tuple *upsert);

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
	/* UPSERT must have the n_upserts field. */
	assert(tuple_format(tuple)->extra_size == sizeof(uint8_t));
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
 * Create the SELECT statement from MessagePack array.
 * @param format  Format of an index.
 * @param key     MessagePack array of key fields.
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
 * Encode vy_stmt for a primary key as xrow_header
 *
 * @param value statement to encode
 * @param key_def key definition
 * @param space_id is written to the request header unless it is 0.
 * Pass 0 to save some space in xrow.
 * @param xrow[out] xrow to fill
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
vy_stmt_encode_primary(const struct tuple *value,
		       const struct key_def *key_def, uint32_t space_id,
		       struct xrow_header *xrow);

/**
 * Encode vy_stmt for a secondary key as xrow_header
 *
 * @param value statement to encode
 * @param key_def key definition
 * @param xrow[out] xrow to fill
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
vy_stmt_encode_secondary(const struct tuple *value,
			 const struct key_def *key_def,
			 struct xrow_header *xrow);

/**
 * Reconstruct vinyl tuple info and data from xrow
 *
 * @retval stmt on success
 * @retval NULL on error
 */
struct tuple *
vy_stmt_decode(struct xrow_header *xrow, const struct key_def *key_def,
	       struct tuple_format *format, bool is_primary);

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

/**
 * Create a tuple format with column mask of an update operation.
 * @sa vy_index.column_mask, vy_can_skip_update().
 * @param space_format A base tuple format.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format register error.
 */
struct tuple_format *
vy_tuple_format_new_with_colmask(struct tuple_format *space_format);

/**
 * Create a tuple format for UPSERT tuples. UPSERTs has an additional
 * extra byte before an offsets table, that stores the count
 * of squashed upserts @sa vy_squash.
 * @param space_format A base tuple format.
 *
 * @retval not NULL Success.
 * @retval     NULL Memory or format register error.
 */
struct tuple_format *
vy_tuple_format_new_upsert(struct tuple_format *space_format);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STMT_H */
