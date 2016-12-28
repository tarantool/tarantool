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
#include <sys/uio.h> /* struct iovec */
#include <msgpuck.h>

#include "iproto_constants.h"
#include "key_def.h"
#include "tuple_compare.h"
#include "tuple_format.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;

/**
 * There are two groups of statements:
 *
 *  - SELECT and DELETE are "key" statements.
 *  - DELETE, UPSERT and REPLACE are "tuple" statements.
 *
 * REPLACE/UPSERT statements structure:
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
 * SELECT/DELETE statements structure.
 * +--------------+-----------------+
 * | array header | part1 ... partN |  -  MessagePack data
 * +--------------+-----------------+
 *
 * Field 'operations' is used for storing operations of UPSERT statement.
 */
struct vy_stmt {
	int64_t  lsn;
	/**
	 * Size of the MessagePack data in raw part of the
	 * statement. It includes upsert operations, if the
	 * statement is UPSERT.
	 */
	uint32_t bsize;
	uint16_t refs; /* atomic */
	uint8_t  type; /* IPROTO_SELECT/REPLACE/UPSERT/DELETE */
	/**
	 * Number of UPSERT statements for the same key preceding
	 * this statement. Used to trigger upsert squashing in the
	 * background (see vy_range_set_upsert()).
	 */
	uint8_t n_upserts;
	/** Offsets count before MessagePack data. */
	uint16_t data_offset;
	/**
	 * Offsets array concatenated with MessagePack fields
	 * array.
	 * char raw[0];
	 */
};

/** Create a tuple in the vinyl engine format. @sa tuple_new(). */
struct tuple *
vy_tuple_new(struct tuple_format *format, const char *data, const char *end);

/**
 * Free the tuple of a vinyl space.
 * @pre tuple->refs  == 0
 */
void
vy_tuple_delete(struct tuple_format *format, struct tuple *tuple);

/**
 * Get the pointer behind last element of the tuple offsets array.
 * @sa struct vy_stmt and its offsets array.
 */
static inline const uint32_t *
vy_stmt_field_map(const struct vy_stmt *stmt)
{
	assert(stmt->type == IPROTO_UPSERT || stmt->type == IPROTO_REPLACE);
	return (const uint32_t *) ((const char *) stmt + stmt->data_offset);
}

/**
 * Increase the reference counter of statement.
 *
 * @param stmt statement.
 */
void
vy_stmt_ref(struct vy_stmt *stmt);

/**
 * Decrease the reference counter of statement.
 *
 * @param stmt statement.
 */
void
vy_stmt_unref(struct vy_stmt *stmt);

/**
 * Duplicate statememnt
 *
 * @param stmt statement
 * @return new statement of the same type with the same data.
 */
struct vy_stmt *
vy_stmt_dup(const struct vy_stmt *stmt);

/** TODO: internal, move to .cc file */
struct vy_stmt *
vy_stmt_alloc(uint32_t size);

/**
 * Return the total size of statement in bytes
 *
 * @param stmt statement
 * @retval the total size of statement in bytes.
 */
static inline uint32_t
vy_stmt_size(const struct vy_stmt *statement)
{
	/* data_offset includes sizeof(struct vy_stmt). */
	return statement->bsize + statement->data_offset;
}

/**
 * There are two groups of comparators - for raw data and for full statements.
 * Specialized comparators are faster than general-purpose comparators.
 * For example, vy_stmt_compare - slowest comparator because it in worst case
 * checks all combinations of key and tuple types, but
 * vy_key_compare - fastest comparator, because it shouldn't check statement
 * types.
 */

/**
 * Compare key statements by their raw data.
 * @param key_a Left operand of comparison.
 * @param key_b Right operand of comparison.
 * @param key_def Definition of the format of both statements.
 *
 * @retval 0   if key_a == key_b
 * @retval > 0 if key_a > key_b
 * @retval < 0 if key_a < key_b
 */
static inline int
vy_key_compare_raw(const char *key_a, const char *key_b,
		   const struct key_def *key_def)
{
	uint32_t part_count_a = mp_decode_array(&key_a);
	uint32_t part_count_b = mp_decode_array(&key_b);
	return tuple_compare_key_raw(key_a, part_count_a, key_b, part_count_b,
				     key_def);
}

/**
 * Extract MessagePack data from the SELECT/DELETE statement.
 * @param stmt    An SELECT or DELETE statement.
 *
 * @return MessagePack array of key parts.
 */
static inline const char *
vy_stmt_data(const struct vy_stmt *stmt)
{
	return (const char *) stmt + stmt->data_offset;
}

/** @sa vy_key_compare_raw. */
static inline int
vy_key_compare(const struct vy_stmt *left, const struct vy_stmt *right,
	       const struct key_def *key_def)
{
	assert(left->type == IPROTO_SELECT || left->type == IPROTO_DELETE);
	assert(right->type == IPROTO_SELECT || right->type == IPROTO_DELETE);
	return vy_key_compare_raw((const char *) (left + 1),
				  (const char *) (right + 1), key_def);
}

/**
 * Compare statements by their raw data.
 * @param stmt_a Left operand of comparison.
 * @param stmt_b Right operand of comparison.
 * @param a_type iproto_type of stmt_data_a
 * @param b_type iproto_type of stmt_data_b
 * @param key_def Definition of the format of both statements.
 *
 * @retval 0   if a == b
 * @retval > 0 if a > b
 * @retval < 0 if a < b
 */
static inline int
vy_tuple_compare(const struct vy_stmt *left, const struct vy_stmt *right,
		 const struct tuple_format *format,
		 const struct key_def *key_def)
{
	assert(left->type == IPROTO_REPLACE || left->type == IPROTO_UPSERT);
	assert(right->type == IPROTO_REPLACE || right->type == IPROTO_UPSERT);
	const uint32_t *left_offsets = vy_stmt_field_map(left);
	const uint32_t *right_offsets = vy_stmt_field_map(right);
	const char *ldata = vy_stmt_data(left);
	assert(mp_typeof(*ldata) == MP_ARRAY);

	const char *rdata = vy_stmt_data(right);
	assert(mp_typeof(*rdata) == MP_ARRAY);
	return tuple_compare_default_raw(format, ldata, left_offsets,
					 format, rdata, right_offsets,
					 key_def);
}

/*
 * Compare a tuple statement with a key statement using their raw data.
 * @param tuple_stmt the raw data of a tuple statement
 * @param key raw data of a key statement
 *
 * @retval > 0  tuple > key.
 * @retval == 0 tuple == key in all fields
 * @retval == 0 tuple is prefix of key
 * @retval == 0 key is a prefix of tuple
 * @retval < 0  tuple < key.
 */
static inline int
vy_tuple_compare_with_key(const struct vy_stmt *tuple,
			  const struct vy_stmt *key,
			  const struct tuple_format *format,
			  const struct key_def *key_def)
{
	const uint32_t *offsets = vy_stmt_field_map(tuple);
	const char *tuple_data = vy_stmt_data(tuple);
	const char *key_data = vy_stmt_data(key);
	uint32_t part_count = mp_decode_array(&key_data);
	return tuple_compare_with_key_default_raw(format, tuple_data, offsets,
						  key_data, part_count,
						  key_def);
}

/** @sa vy_stmt_compare_raw. */
static inline int
vy_stmt_compare(const struct vy_stmt *a, const struct vy_stmt *b,
		const struct tuple_format *format,
		const struct key_def *key_def)
{
	bool a_is_tuple = a->type == IPROTO_REPLACE || a->type == IPROTO_UPSERT;
	bool b_is_tuple = b->type == IPROTO_REPLACE || b->type == IPROTO_UPSERT;
	if (a_is_tuple && b_is_tuple) {
		return vy_tuple_compare(a, b, format, key_def);
	} else if (a_is_tuple && !b_is_tuple) {
		return vy_tuple_compare_with_key(a, b, format, key_def);
	} else if (!a_is_tuple && b_is_tuple) {
		return -vy_tuple_compare_with_key(b, a, format, key_def);
	} else {
		assert(!a_is_tuple && !b_is_tuple);
		return vy_key_compare(a, b, key_def);
	}
}

/** @sa vy_stmt_compare_with_raw_key. */
static inline int
vy_stmt_compare_with_key(const struct vy_stmt *stmt,
			 const struct vy_stmt *key,
			 const struct tuple_format *format,
			 const struct key_def *key_def)
{
	assert(key->type == IPROTO_SELECT || key->type == IPROTO_DELETE);
	if (stmt->type == IPROTO_REPLACE || stmt->type == IPROTO_UPSERT)
		return vy_tuple_compare_with_key(stmt, key, format, key_def);
	return vy_key_compare(stmt, key, key_def);
}

/**
 * Create the SELECT statement from raw MessagePack data.
 * @param key MessagePack data that contain an array of fields WITHOUT the
 *            array header.
 * @param part_count Count of the key fields that will be saved as result.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct vy_stmt *
vy_stmt_new_select(const char *key, uint32_t part_count);

/**
 * Create the DELETE statement from raw MessagePack data.
 * @param key MessagePack data that contain an array of fields WITHOUT the
 *            array header.
 * @param part_count Count of the key fields that will be saved as result.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct vy_stmt *
vy_stmt_new_delete(const char *key, uint32_t part_count);

/**
 * Create the REPLACE statement from raw MessagePack data.
 * @param tuple_begin MessagePack data that contain an array of fields WITH the
 *                    array header.
 * @param tuple_end End of the array that begins from @param tuple_begin.
 * @param format Format of a tuple for offsets generating.
 * @param part_count Part count from key definition.
 *
 * @retval NULL     Memory allocation error.
 * @retval not NULL Success.
 */
struct vy_stmt *
vy_stmt_new_replace(const char *tuple_begin, const char *tuple_end,
		    const struct tuple_format *format,
		    uint32_t part_count);

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
struct vy_stmt *
vy_stmt_new_upsert(const char *tuple_begin, const char *tuple_end,
		   const struct tuple_format *format, uint32_t part_count,
		   struct iovec *operations, uint32_t ops_cnt);

/**
 * Create REPLACE statement from UPSERT statement.
 *
 * @param upsert upsert statement.
 * @return stmt REPLACE.
 */
struct vy_stmt *
vy_stmt_replace_from_upsert(const struct vy_stmt *upsert);

/**
 * Extract MessagePack data from the SELECT/DELETE statement.
 * @param stmt An SELECT or DELETE statement.
 * @param[out] p_size Size of the MessagePack array in bytes.
 *
 * @return MessagePack array of key parts.
 */
static inline const char *
vy_key_data_range(const struct vy_stmt *stmt, uint32_t *p_size)
{
	assert(stmt->type == IPROTO_SELECT || stmt->type == IPROTO_DELETE);
	*p_size = stmt->bsize;
	return vy_stmt_data(stmt);
}

/* TODO: rename to vy_key_part_count */
static inline uint32_t
vy_stmt_part_count(const struct vy_stmt *stmt)
{
	const char *data = vy_stmt_data(stmt);
	return mp_decode_array(&data);
}

/**
 * Extract MessagePack data from the REPLACE/UPSERT statement.
 * @param stmt An UPSERT or REPLACE statement.
 * @param[out] p_size Size of the MessagePack array in bytes.
 *
 * @return MessagePack array of tuple fields.
 */
static inline const char *
vy_tuple_data_range(const struct vy_stmt *stmt, uint32_t *p_size)
{
	assert(stmt->type == IPROTO_REPLACE || stmt->type == IPROTO_UPSERT);
	const char *mp = vy_stmt_data(stmt);
	assert(mp_typeof(*mp) == MP_ARRAY);
	if (stmt->type == IPROTO_REPLACE) {
		*p_size = stmt->bsize;
		return mp;
	}
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
vy_stmt_upsert_ops(const struct vy_stmt *stmt, uint32_t *mp_size)
{
	assert(stmt->type == IPROTO_UPSERT);
	const char *mp = vy_stmt_data(stmt);
	mp_next(&mp);
	*mp_size = vy_stmt_data(stmt) + stmt->bsize - mp;
	return mp;
}

/**
 * Extract a SELECT statement with only indexed fields from raw data.
 * @param stmt Raw data of struct vy_stmt.
 * @param key_def key definition.
 *
 * @retval not NULL Success.
 * @retval NULL Memory allocation error.
 */
struct vy_stmt *
vy_stmt_extract_key(const struct vy_stmt *stmt, const struct key_def *key_def);

/**
 * Create the SELECT statement from MessagePack array.
 * @param key
 * @param key_def
 *
 * @retval not NULL Success.
 * @retval     NULL Memory error.
 */
static inline struct vy_stmt *
vy_key_from_msgpack(const char *key, const struct key_def *key_def)
{
	(void) key_def; /* unused in release. */
	uint32_t part_count;
	/*
	 * The statement already is a key, so simply copy it in
	 * the new struct vy_stmt as SELECT.
	 */
	part_count = mp_decode_array(&key);
	assert(part_count <= key_def->part_count);
	return vy_stmt_new_select(key, part_count);
}

/**
 * Encode vy_stmt as xrow_header
 *
 * @retval 0 if OK
 * @retval -1 if error
 */
int
vy_stmt_encode(const struct vy_stmt *value, const struct key_def *key_def,
	       struct xrow_header *xrow);

/**
 * Reconstruct vinyl tuple info and data from xrow
 *
 * @retval stmt on success
 * @retval NULL on error
 */
struct vy_stmt *
vy_stmt_decode(struct xrow_header *xrow, const struct tuple_format *format,
	       uint32_t part_count);

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
vy_stmt_snprint(char *buf, int size, const struct vy_stmt *stmt);

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
vy_stmt_str(const struct vy_stmt *stmt);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STMT_H */
