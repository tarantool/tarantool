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
#include "tt_static.h"
#include "tt_uuid.h"
#include "tuple_format.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct slab_arena;
struct quota;
struct key_part;

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
		   bool dontdump, const char *arena_name);

void
tuple_arena_destroy(struct slab_arena *arena);

/**
 * Creates a new format for standalone tuples.
 * Tuples created with the new format are allocated from the runtime arena.
 * In contrast to the preallocated tuple_format_runtime, which has no
 * information about fields, the new format uses the field definitions from the
 * Msgpack encoded format clause.
 *
 * In some cases (formats received over IPROTO or formats for read views) we
 * only need to get the 'name' field options and ignore the rest, hence the
 * `names_only` flag is provided.
 *
 * On success, returns the new format. On error, returns NULL and sets diag.
 */
struct tuple_format *
runtime_tuple_format_new(const char *format_data, size_t format_data_len,
			 bool names_only);

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
 * \retval 0 always
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
box_tuple_field_count(box_tuple_t *tuple);

/**
 * Return the number of bytes used to store internal tuple data (MsgPack Array).
 * \param tuple a tuple
 */
size_t
box_tuple_bsize(box_tuple_t *tuple);

/**
 * Dump raw MsgPack data to the memory buffer \a buf of size \a size.
 *
 * Store tuple fields in the memory buffer.
 * \retval -1 on error.
 * \retval number of bytes written on success.
 * Upon successful return, the function returns the number of bytes written.
 * If buffer size is not enough then the return value is the number of bytes
 * which would have been written if enough space had been available.
 */
ssize_t
box_tuple_to_buf(box_tuple_t *tuple, char *buf, size_t size);

/**
 * Return the associated format.
 * \param tuple tuple
 * \return tuple_format
 */
box_tuple_format_t *
box_tuple_format(box_tuple_t *tuple);

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
box_tuple_field(box_tuple_t *tuple, uint32_t fieldno);

/**
 * Return a raw tuple field in the MsgPack format pointed by
 * a JSON path.
 *
 * The JSON path includes the outmost field. For example, "c" in
 * ["a", ["b", "c"], "d"] can be accessed using "[2][2]" path (if
 * index_base is 1, as in Lua). If index_base is set to 0, the
 * same field will be pointed by the "[1][1]" path.
 *
 * The first JSON path token may be a field name if the tuple
 * has associated format with named fields. A field of a nested
 * map can be accessed in the same way: "foo.bar" or ".foo.bar".
 *
 * The return value is valid until the tuple is destroyed, see
 * box_tuple_ref().
 *
 * Return NULL if the field does not exist or if the JSON path is
 * malformed or invalid. Multikey JSON path token [*] is treated
 * as invalid in this context.
 *
 * \param tuple a tuple
 * \param path a JSON path
 * \param path_len a length of @a path
 * \param index_base 0 if array element indexes in @a path are
 *        zero-based (like in C) or 1 if they're one-based (like
 *        in Lua)
 * \retval a pointer to a field data if the field exists or NULL
 */
API_EXPORT const char *
box_tuple_field_by_path(box_tuple_t *tuple, const char *path,
			uint32_t path_len, int index_base);
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
 * Destroy and free tuple iterator.
 */
void
box_tuple_iterator_free(box_tuple_iterator_t *it);

/**
 * Return zero-based next position in iterator.
 * That is, this function return the field id of field that will be
 * returned by the next call to box_tuple_next(). Returned value is zero
 * after initialization or rewind and box_tuple_field_count()
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
 * Requested fieldno returned by next call to box_tuple_next().
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
 * \pre box_tuple_position(it) is zero-based id of returned field
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
 * \retval tuple
 * \pre data, end is valid MsgPack Array
 * \sa \code box.tuple.new(data) \endcode
 */
box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end);

/**
 * Update a tuple.
 *
 * Function returns a copy of tuple with updated fields.
 * Pay attention that original tuple is left without changes.
 *
 * @param tuple Tuple to update.
 * @param expr MessagePack array of operations.
 * @param expr_end End of the @a expr.
 *
 * @retval NULL The tuple was not updated.
 * @retval box_tuple_t A copy of original tuple with updated fields.
 *
 * @sa box_update()
 */
box_tuple_t *
box_tuple_update(box_tuple_t *tuple, const char *expr, const char *expr_end);

/**
 * Update a tuple.
 *
 * The same as box_tuple_update(), but ignores errors. In case of an error the
 * tuple is left intact, but an error message is printed. Only client errors are
 * ignored, such as a bad field type, or wrong field index/name. System errors,
 * such as OOM, are not ignored and raised just like with a normal
 * box_tuple_update(). Note that only bad operations are ignored. All correct
 * operations are applied.
 *
 * Despite the function name (upsert, update and insert), the function does not
 * insert any tuple.
 *
 * @param tuple Tuple to update.
 * @param expr MessagePack array of operations.
 * @param expr_end End of the @a expr.
 *
 * @retval NULL The tuple was not updated.
 * @retval box_tuple_t A copy of original tuple with updated fields.
 *
 * @sa box_upsert()
 */
box_tuple_t *
box_tuple_upsert(box_tuple_t *tuple, const char *expr, const char *expr_end);

/**
 * Check tuple data correspondence to the space format.
 * @param tuple  Tuple to validate.
 * @param format Format to which the tuple must match.
 *
 * @retval  0 The tuple is valid.
 * @retval -1 The tuple is invalid.
 */
int
box_tuple_validate(box_tuple_t *tuple, box_tuple_format_t *format);

/** \endcond public */

/**
 * Tuple flags. Each value is a position of corresponding bit in
 * tuple->flags member.
 */
enum tuple_flag {
	/**
	 * Flag that shows that the tuple has more references in separate
	 * external storage, see @sa tuple_upload_refs and tuple_acquire_refs.
	 * For private use only.
	 */
	TUPLE_HAS_UPLOADED_REFS = 0,
	/**
	 * The tuple (if it's found in index for example) could be invisible
	 * for current transactions. The flag means that the tuple must
	 * be clarified by transaction engine.
	 */
	TUPLE_IS_DIRTY = 1,
	/**
	 * The tuple belongs to a data-temporary space so it can be freed
	 * immediately while a snapshot is in progress.
	 */
	TUPLE_IS_TEMPORARY = 2,
	tuple_flag_MAX,
};

/**
 * An atom of Tarantool storage. Represents MsgPack Array.
 * Tuple has the following structure:
 *                           uint32       uint32     bsize
 *                          +-------------------+-------------+
 * tuple_begin, ..., raw =  | offN | ... | off1 | MessagePack |
 * |                        +-------------------+-------------+
 * |                                            ^
 * +---------------------------------------data_offset
 *
 * Each 'off_i' is the offset to the i-th indexed field.
 */
struct PACKED tuple
{
	/**
	 * Local reference counter. After hitting the limit a part of this
	 * counter is uploaded to external storage that is acquired back
	 * when the counter falls back to zero.
	 * Is always nonzero in normal reference counted tuples.
	 * Must not be accessed directly, use @sa tuple_create,
	 * tuple_ref_init, tuple_ref, tuple_unref instead.
	 */
	uint8_t local_refs;
	/** Tuple flags (e.g. see TUPLE_HAS_UPLOADED_REFS). */
	uint8_t flags;
	/** Format identifier. */
	uint16_t format_id;
	/**
	 * A pair of fields for storing data offset and data bsize:
	 * Data offset - to the MessagePack from the begin of the tuple.
	 * Data bsize - length of the MessagePack data in raw part of the tuple.
	 * These two values can be stored in bulky and compact modes.
	 * In bulky mode offset is simple stored in data_offset_bsize_raw
	 * member while bsize is stored in bsize_bulky.
	 * In compact mode bsize_bulky is not used and both values are combined
	 * in data_offset_bsize_raw: offset in high byte, bsize in low byte,
	 * and the highest bit is set to 1 as a flag of compact mode.
	 * They're for internal use only, must be set in tuple_create and
	 * accessed by tuple_data_offset, tuple_bsize etc.
	 */
	uint16_t data_offset_bsize_raw;
	uint32_t bsize_bulky;
	/**
	 * Engine specific fields and offsets array concatenated
	 * with MessagePack fields array.
	 * char raw[0];
	 * Note that in compact mode bsize_bulky is no used and dynamic data
	 * can be written starting from bsize_bulky member.
	 */
};

static_assert(sizeof(struct tuple) == 10, "Just to be sure");

/** Type of the arena where the tuple is allocated. */
enum tuple_arena_type {
	TUPLE_ARENA_MEMTX = 0,
	TUPLE_ARENA_MALLOC = 1,
	TUPLE_ARENA_RUNTIME = 2,
	tuple_arena_type_MAX
};

/** Arena type names. */
extern const char *tuple_arena_type_strs[tuple_arena_type_MAX];

/** Information about the tuple. */
struct tuple_info {
	/** Size of the MsgPack data. See also tuple_bsize(). */
	size_t data_size;
	/** Header size depends on the engine and on the compact/bulky mode. */
	size_t header_size;
	/** Size of the field_map. See also field_map_build_size(). */
	size_t field_map_size;
	/**
	 * The amount of excess memory used to store the tuple in mempool.
	 * Note that this value is calculated not during the actual allocation,
	 * but afterwards. This means that it can be incorrect if the state of
	 * the allocator changed. See also small_alloc_info().
	 */
	size_t waste_size;
	/** Type of the arena where the tuple is allocated. */
	enum tuple_arena_type arena_type;
};

/** Fill `info' with the information about the `tuple'. */
static inline void
tuple_info(struct tuple *tuple, struct tuple_info *info)
{
	struct tuple_format *format = tuple_format_by_id(tuple->format_id);
	format->vtab.tuple_info(format, tuple, info);
}

static_assert(DIV_ROUND_UP(tuple_flag_MAX, 8) <=
	      sizeof(((struct tuple *)0)->flags),
	      "enum tuple_flag doesn't fit into tuple->flags");

/** Set flag of the tuple. */
static inline void
tuple_set_flag(struct tuple *tuple, enum tuple_flag flag)
{
	assert(flag < tuple_flag_MAX);
	tuple->flags |= (1 << flag);
}

/** Test if tuple has a flag. */
static inline bool
tuple_has_flag(struct tuple *tuple, enum tuple_flag flag)
{
	assert(flag < tuple_flag_MAX);
	return (tuple->flags & (1 << flag)) != 0;
}

/** Clears tuple flag. */
static inline void
tuple_clear_flag(struct tuple *tuple, enum tuple_flag flag)
{
	assert(flag < tuple_flag_MAX);
	tuple->flags &= ~(1 << flag);
}

enum {
	/** Maximal value of tuple::local_refs. */
	TUPLE_LOCAL_REF_MAX = UINT8_MAX,
	/** The size that is unused in struct tuple in compact mode. */
	TUPLE_COMPACT_SAVINGS = sizeof(((struct tuple *)(NULL))->bsize_bulky),
};

/**
 * Check that data_offset is valid and can be stored in tuple.
 * @param data_offset - see member description in struct tuple.
 *
 * @return 0 on success, -1 otherwise (diag is set).
 */
static inline int
tuple_check_data_offset(uint32_t data_offset)
{
	if (data_offset > INT16_MAX) {
		/** tuple->data_offset is 15 bits */
		diag_set(ClientError, ER_TUPLE_METADATA_IS_TOO_BIG,
			 data_offset);
		return -1;
	}
	return 0;
}

/**
 * Test whether the tuple can be stored compactly.
 * @param data_offset - see member description in struct tuple.
 * @param bsize - see member description in struct tuple.
 * @return
 */
static inline bool
tuple_can_be_compact(uint16_t data_offset, uint32_t bsize)
{
	/*
	 * Compact mode requires data_offset to be 7 bits max and bsize
	 * to be 8 bits max; see tuple::data_offset_bsize_raw for explanation.
	 */
	return data_offset <= INT8_MAX && bsize <= UINT8_MAX;
}

/**
 * Initialize a tuple. Must be called right after allocation of a new tuple.
 * Should only be called for newly created uninitialized tuples.
 * If the tuple copied from another tuple and only initialization of reference
 * count is needed, it's better to call tuple_ref_init.
 * tuple_check_data_offset should be called before to ensure args are correct.
 *
 * @param tuple - Tuple to initialize.
 * @param refs - initial reference count to set.
 * @param format_id - see member description in struct tuple.
 * @param data_offset - see member description in struct tuple.
 * @param bsize - see member description in struct tuple.
 * @param make_compact - construct compact tuple, see description in tuple.
 * @return 0 on success, -1 on error (diag is set).
 */
static inline void
tuple_create(struct tuple *tuple, uint8_t refs, uint16_t format_id,
	     uint16_t data_offset, uint32_t bsize, bool make_compact)
{
	assert(data_offset <= INT16_MAX);
	tuple->local_refs = refs;
	tuple->flags = 0;
	tuple->format_id = format_id;
	if (make_compact) {
		assert(tuple_can_be_compact(data_offset, bsize));
		uint16_t combined = 0x8000;
		combined |= data_offset << 8;
		combined |= bsize;
		tuple->data_offset_bsize_raw = combined;
	} else {
		tuple->data_offset_bsize_raw = data_offset;
		tuple->bsize_bulky = bsize;
	}
}

/**
 * Initialize tuple reference counter to a given value.
 * Should only be called for newly created uninitialized tuples.
 * For initialization of brand new tuples it's better to use tuple_create.
 *
 * @param tuple Tuple to reference
 * @param refs initial reference count to set.
 */
static inline void
tuple_ref_init(struct tuple *tuple, uint8_t refs)
{
	tuple->local_refs = refs;
	tuple_clear_flag(tuple, TUPLE_HAS_UPLOADED_REFS);
}

/**
 * Check that the tuple has zero references (i.e. has zero reference count).
 * Note that direct (manual) modification of refs is prohibited, and internally
 * tuples are immediately deleted after hitting zero reference count. Thus
 * zero reference count can mean the following:
 * 1. The tuple was created with zero count and haven't been referenced yet.
 * 2. The tuple is designed not to be referenced, for example it is allocated
 *  on different kind of allocator.
 * 3. We are in tuple deallocation procedure that is started after reference
 *  count became zero.
 */
static inline bool
tuple_is_unreferenced(struct tuple *tuple)
{
	/*
	 * There's no need to look at external storage of reference counts,
	 * because by design when local_refs falls to zero the storage is
	 * checked immediately and the tuple is either deleted or local_refs
	 * is set to nonzero value (TUPLE_UPLOAD_REFS).
	 */
	return tuple->local_refs == 0;
}

/** Check that the tuple is in compact mode. */
static inline bool
tuple_is_compact(struct tuple *tuple)
{
	return tuple->data_offset_bsize_raw & 0x8000;
}

/** Offset to the MessagePack from the beginning of the tuple. */
static inline uint16_t
tuple_data_offset(struct tuple *tuple)
{
	uint16_t res = tuple->data_offset_bsize_raw;
	/*
	 * We have two variants depending on high bit of res (res & 0x8000):
	 * 1) nonzero, compact mode, the result is in high byte, excluding
	 *  high bit: (res & 0x7fff) >> 8.
	 * 2) zero, bulky mode, the result is just res.
	 * We could make `if` statement here, but it would cost too much.
	 * We can make branchless code instead. We can rewrite the result:
	 * 1) In compact mode: (res & 0x7fff) >> 8
	 * 2) In bulky mode:   (res & 0x7fff) >> 0
	 * Or, simply:
	 * In any case mode: (res & 0x7fff) >> (8 * (is_compact ? 1 : 0)).
	 * On the other hand the compact 0/1 bit can be simply acquired
	 * by shifting res >> 15.
	 */
	uint16_t is_compact_bit = res >> 15;
	res = (res & 0x7fff) >> (is_compact_bit * 8);
#ifndef NDEBUG
	uint16_t simple = (tuple->data_offset_bsize_raw & 0x8000) ?
			  (tuple->data_offset_bsize_raw >> 8) & 0x7f :
			  tuple->data_offset_bsize_raw;
	assert(res == simple);
#endif
	return res;
}

/** Size of MessagePack data of the tuple. */
static inline uint32_t
tuple_bsize(struct tuple *tuple)
{
	if (tuple->data_offset_bsize_raw & 0x8000) {
		return tuple->data_offset_bsize_raw & 0xff;
	} else {
		return tuple->bsize_bulky;
	}
}

/** Size of the tuple including size of struct tuple. */
static inline size_t
tuple_size(struct tuple *tuple)
{
	/* data_offset includes sizeof(struct tuple). */
	return tuple_data_offset(tuple) + tuple_bsize(tuple);
}

/**
 * Get pointer to MessagePack data of the tuple.
 * @param tuple tuple.
 * @return MessagePack array.
 */
static inline const char *
tuple_data(struct tuple *tuple)
{
	return (const char *) tuple + tuple_data_offset(tuple);
}

/**
 * Wrapper around tuple_data() which returns NULL if @tuple == NULL.
 */
static inline const char *
tuple_data_or_null(struct tuple *tuple)
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
tuple_data_range(struct tuple *tuple, uint32_t *p_size)
{
	*p_size = tuple_bsize(tuple);
	return (const char *) tuple + tuple_data_offset(tuple);
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
tuple_snprint(char *buf, int size, struct tuple *tuple);

/**
 * Format a tuple into string using a static buffer.
 * Useful for debugger. Example: [1, 2, "string"]
 * @param tuple to format
 * @return formatted null-terminated string
 */
const char *
tuple_str(struct tuple *tuple);

/**
 * Format msgpack into string using a static buffer.
 * Useful for debugger. Example: [1, 2, "string"]
 * @param msgpack to format
 * @return formatted null-terminated string
 */
const char *
mp_str(const char *data);

/**
 * Get the format of the tuple.
 * @param tuple Tuple.
 * @retval Tuple format instance.
 */
static inline struct tuple_format *
tuple_format(struct tuple *tuple)
{
	struct tuple_format *format = tuple_format_by_id(tuple->format_id);
	assert(tuple_format_id(format) == tuple->format_id);
	return format;
}

/** Check that some fields in tuple are compressed */
static inline bool
tuple_is_compressed(struct tuple *tuple)
{
        return tuple_format(tuple)->is_compressed;
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
 */
static inline struct tuple *
tuple_new(struct tuple_format *format, const char *data, const char *end)
{
	return format->vtab.tuple_new(format, data, end);
}

/**
 * Free the tuple of any engine.
 * @pre tuple->refs  == 0
 */
static inline void
tuple_delete(struct tuple *tuple)
{
	assert(tuple->local_refs == 0);
	assert(!tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS));
	assert(!tuple_has_flag(tuple, TUPLE_IS_DIRTY));
	struct tuple_format *format = tuple_format(tuple);
	format->vtab.tuple_delete(format, tuple);
}

/**
 * Check tuple data correspondence to space format.
 * Actually, checks everything that is checked by
 * tuple_field_map_create.
 *
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
 * @sa tuple_field_map_create()
 */
static inline const char *
tuple_field_map(struct tuple *tuple)
{
	return tuple_data(tuple);
}

/**
 * @brief Return the number of fields in tuple
 * @param tuple
 * @return the number of fields in tuple
 */
static inline uint32_t
tuple_field_count(struct tuple *tuple)
{
	const char *data = tuple_data(tuple);
	return mp_decode_array(&data);
}

/**
 * Retrieve msgpack data by JSON path.
 * @param data[in, out] Pointer to msgpack with data.
 *                      If the field cannot be retrieved be the
 *                      specified path @path, it is overwritten
 *                      with NULL.
 * @param path The path to process.
 * @param path_len The length of the @path.
 * @param index_base 0 if array element indexes in @a path are
 *        zero-based (like in C) or 1 if they're one-based (like
 *        in Lua).
 * @param multikey_idx The multikey index hint - index of
 *                     multikey index key to retrieve when array
 *                     index placeholder "[*]" is met.
 * @retval 0 On success.
 * @retval -1 In case of error in JSON path.
 */
int
tuple_go_to_path(const char **data, const char *path, uint32_t path_len,
		 int index_base, int multikey_idx);

/**
 * Propagate @a field to MessagePack(field)[index].
 * @param[in][out] field Field to propagate.
 * @param index 0-based index to propagate to.
 *
 * @retval  0 Success, the index was found.
 * @retval -1 Not found.
 */
int
tuple_field_go_to_index(const char **field, uint64_t index);

/**
 * Propagate @a field to MessagePack(field)[key].
 * @param[in][out] field Field to propagate.
 * @param key Key to propagate to.
 * @param len Length of @a key.
 *
 * @retval  0 Success, the index was found.
 * @retval -1 Not found.
 */
int
tuple_field_go_to_key(const char **field, const char *key, int len);

/**
 * Get tuple field by field index, relative JSON path and
 * multikey_idx.
 * @param format Tuple format.
 * @param tuple MessagePack tuple's body.
 * @param field_map Tuple field map.
 * @param fieldno The index of a root field.
 * @param path Relative JSON path to field.
 * @param path_len Length of @a path.
 * @param index_base 0 if array element indexes in @a path are
 *        zero-based (like in C) or 1 if they're one-based (like
 *        in Lua).
 * @param offset_slot_hint The pointer to a variable that contains
 *                         an offset slot. May be NULL.
 *                         If specified AND value by pointer is
 *                         not TUPLE_OFFSET_SLOT_NIL is used to
 *                         access data in a single operation.
 *                         Else it is initialized with offset_slot
 *                         of format field by path.
 * @param multikey_idx The multikey index hint - index of
 *                     multikey item item to retrieve when array
 *                     index placeholder "[*]" is met.
 */
static inline const char *
tuple_field_raw_by_path(struct tuple_format *format, const char *tuple,
			const char *field_map, uint32_t fieldno,
			const char *path, uint32_t path_len,
			int index_base, int32_t *offset_slot_hint,
			int multikey_idx)
{
	int32_t offset_slot;
	if (offset_slot_hint != NULL &&
	    *offset_slot_hint != TUPLE_OFFSET_SLOT_NIL) {
		offset_slot = *offset_slot_hint;
		goto offset_slot_access;
	}
	if (likely(fieldno < format->index_field_count)) {
		uint32_t offset;
		struct tuple_field *field;
		if (path == NULL && fieldno == 0) {
			mp_decode_array(&tuple);
			return tuple;
		}
		field = tuple_format_field_by_path(format, fieldno, path,
						   path_len, index_base);
		assert(field != NULL || path != NULL);
		if (path != NULL && field == NULL)
			goto parse;
		offset_slot = field->offset_slot;
		if (offset_slot == TUPLE_OFFSET_SLOT_NIL)
			goto parse;
		if (offset_slot_hint != NULL) {
			*offset_slot_hint = offset_slot;
			/*
			 * Hint is never requested for a multikey field without
			 * providing a concrete multikey index.
			 */
			assert(!field->is_multikey_part ||
			       multikey_idx != MULTIKEY_NONE);
		} else if (field->is_multikey_part &&
			   multikey_idx == MULTIKEY_NONE) {
			/*
			 * When the field is multikey, the offset slot points
			 * not at the data. It points at 'extra' array of
			 * offsets for this multikey index. That array can only
			 * be accessed if index in that array is known. It is
			 * not known when the field is accessed not in an index.
			 * For example, in an application's Lua code by a JSON
			 * path.
			 */
			goto parse;
		}
offset_slot_access:
		/* Indexed field */
		offset = field_map_get_offset(field_map, offset_slot,
					      multikey_idx);
		if (offset == 0)
			return NULL;
		tuple += offset;
	} else {
		uint32_t field_count;
parse:
		ERROR_INJECT(ERRINJ_TUPLE_FIELD, return NULL);
		field_count = mp_decode_array(&tuple);
		if (unlikely(fieldno >= field_count))
			return NULL;
		for (uint32_t k = 0; k < fieldno; k++)
			mp_next(&tuple);
		if (path != NULL &&
		    unlikely(tuple_go_to_path(&tuple, path, path_len,
					      index_base, multikey_idx) != 0))
			return NULL;
	}
	return tuple;
}

/**
 * Get a field at the specific position in this MessagePack array.
 * Returns a pointer to MessagePack data.
 * @param format tuple format
 * @param tuple a pointer to MessagePack array
 * @param field_map a pointer to the LAST element of field map
 * @param field_no the index of field to return
 *
 * @returns field data if field exists or NULL
 * @sa tuple_field_map_create()
 */
static inline const char *
tuple_field_raw(struct tuple_format *format, const char *tuple,
		const char *field_map, uint32_t field_no)
{
	if (likely(field_no < format->index_field_count)) {
		int32_t offset_slot;
		uint32_t offset = 0;
		struct tuple_field *field;
		if (field_no == 0) {
			mp_decode_array(&tuple);
			return tuple;
		}
		struct json_token *token = format->fields.root.children[field_no];
		field = json_tree_entry(token, struct tuple_field, token);
		offset_slot = field->offset_slot;
		if (offset_slot == TUPLE_OFFSET_SLOT_NIL)
			goto parse;
		offset = field_map_get_offset(field_map, offset_slot,
					      MULTIKEY_NONE);
		if (offset == 0)
			return NULL;
		tuple += offset;
	} else {
parse:
		ERROR_INJECT(ERRINJ_TUPLE_FIELD, return NULL);
		uint32_t field_count = mp_decode_array(&tuple);
		if (unlikely(field_no >= field_count))
			return NULL;
		for ( ; field_no > 0; field_no--)
			mp_next(&tuple);
	}
	return tuple;
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
tuple_field(struct tuple *tuple, uint32_t fieldno)
{
	return tuple_field_raw(tuple_format(tuple), tuple_data(tuple),
			       tuple_field_map(tuple), fieldno);
}

/**
 * Get tuple field by full JSON path.
 * Unlike tuple_field_raw_by_path this function works with full
 * JSON paths, performing root field index resolve on its own.
 * When the first JSON path token has JSON_TOKEN_STR type, routine
 * uses tuple format dictionary to get field index by field name.
 * @param format Tuple format.
 * @param tuple MessagePack tuple's body.
 * @param field_map Tuple field map.
 * @param path Full JSON path to field.
 * @param path_len Length of @a path.
 * @param path_hash Hash of @a path.
 * @param index_base 0 if array element indexes in @a path are
 *        zero-based (like in C) or 1 if they're one-based (like
 *        in Lua).
 *
 * @retval field data if field exists or NULL
 */
const char *
tuple_field_raw_by_full_path(struct tuple_format *format, const char *tuple,
			     const char *field_map, const char *path,
			     uint32_t path_len, uint32_t path_hash,
			     int index_base);

/**
 * Get a tuple field pointed to by an index part and multikey
 * index hint.
 * @param format Tuple format.
 * @param data A pointer to MessagePack array.
 * @param field_map A pointer to the LAST element of field map.
 * @param part Index part to use.
 * @param multikey_idx A multikey index hint.
 * @retval Field data if the field exists or NULL.
 */
static inline const char *
tuple_field_raw_by_part(struct tuple_format *format, const char *data,
			const char *field_map,
			struct key_part *part, int multikey_idx)
{
	int32_t *offset_slot_cache = NULL;
	if (cord_is_main()) {
		offset_slot_cache = &part->offset_slot_cache;
		if (unlikely(part->format_epoch != format->epoch)) {
			assert(format->epoch != 0);
			part->format_epoch = format->epoch;
			/*
			 * Clear the offset slot cache, since it's stale.
			 * The cache will be reset by the lookup.
			 */
			*offset_slot_cache = TUPLE_OFFSET_SLOT_NIL;
		}
	}
	return tuple_field_raw_by_path(format, data, field_map, part->fieldno,
				       part->path, part->path_len,
				       TUPLE_INDEX_BASE,
				       offset_slot_cache, multikey_idx);
}

/**
 * Get a field refereed by index @part in tuple.
 * @param tuple Tuple to get the field from.
 * @param part Index part to use.
 * @param multikey_idx A multikey index hint.
 * @retval Field data if the field exists or NULL.
 */
static inline const char *
tuple_field_by_part(struct tuple *tuple, struct key_part *part,
		    int multikey_idx)
{
	return tuple_field_raw_by_part(tuple_format(tuple), tuple_data(tuple),
				       tuple_field_map(tuple), part,
				       multikey_idx);
}

/**
 * Get count of multikey index keys in tuple by given multikey
 * index definition.
 * @param format Tuple format.
 * @param data A pointer to MessagePack array.
 * @param field_map A pointer to the LAST element of field map.
 * @param key_def Index key_definition.
 * @retval Count of multikey index keys in the given tuple.
 */
uint32_t
tuple_raw_multikey_count(struct tuple_format *format, const char *data,
			 const char *field_map, struct key_def *key_def);

/**
 * Get count of multikey index keys in tuple by given multikey
 * index definition.
 * @param tuple Tuple to get the count of multikey keys from.
 * @param key_def Index key_definition.
 * @retval Count of multikey index keys in the given tuple.
 */
static inline uint32_t
tuple_multikey_count(struct tuple *tuple, struct key_def *key_def)
{
	return tuple_raw_multikey_count(tuple_format(tuple), tuple_data(tuple),
					tuple_field_map(tuple), key_def);
}

/**
 * @brief Tuple iterator
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

/** Return a tuple field and check its type. */
static inline const char *
tuple_next_with_type(struct tuple_iterator *it, enum mp_type type)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next(it);
	if (field == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NO, it->fieldno);
		return NULL;
	}
	enum mp_type actual_type = mp_typeof(*field);
	if (actual_type != type) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 int2str(fieldno + TUPLE_INDEX_BASE),
			 mp_type_strs[type], mp_type_strs[actual_type]);
		return NULL;
	}
	return field;
}

/** Get next field from iterator as uint32_t. */
static inline int
tuple_next_u32(struct tuple_iterator *it, uint32_t *out)
{
	uint32_t fieldno = it->fieldno;
	const char *field = tuple_next_with_type(it, MP_UINT);
	if (field == NULL)
		return -1;
	uint64_t val = mp_decode_uint(&field);
	*out = val;
	if (val > UINT32_MAX) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 int2str(fieldno + TUPLE_INDEX_BASE),
			 "uint32_t",
			 "uint64_t");
		return -1;
	}
	return 0;
}

/** Get next field from iterator as uint64_t. */
static inline int
tuple_next_u64(struct tuple_iterator *it, uint64_t *out)
{
	const char *field = tuple_next_with_type(it, MP_UINT);
	if (field == NULL)
		return -1;
	*out = mp_decode_uint(&field);
	return 0;
}

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
tuple_field_with_type(struct tuple *tuple, uint32_t fieldno, enum mp_type type)
{
	const char *field = tuple_field(tuple, fieldno);
	if (field == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NO,
			 fieldno + TUPLE_INDEX_BASE);
		return NULL;
	}
	enum mp_type actual_type = mp_typeof(*field);
	if (actual_type != type) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 int2str(fieldno + TUPLE_INDEX_BASE),
			 mp_type_strs[type], mp_type_strs[actual_type]);
		return NULL;
	}
	return field;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as bool.
 */
static inline int
tuple_field_bool(struct tuple *tuple, uint32_t fieldno, bool *out)
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
tuple_field_i64(struct tuple *tuple, uint32_t fieldno, int64_t *out)
{
	const char *field = tuple_field(tuple, fieldno);
	if (field == NULL) {
		diag_set(ClientError, ER_NO_SUCH_FIELD_NO, fieldno);
		return -1;
	}
	uint64_t val;
	enum mp_type actual_type = mp_typeof(*field);
	switch (actual_type) {
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
		diag_set(ClientError, ER_FIELD_TYPE,
			 int2str(fieldno + TUPLE_INDEX_BASE),
			 field_type_strs[FIELD_TYPE_INTEGER],
			 mp_type_strs[actual_type]);
		return -1;
	}
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as uint64_t.
 */
static inline int
tuple_field_u64(struct tuple *tuple, uint32_t fieldno, uint64_t *out)
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
tuple_field_u32(struct tuple *tuple, uint32_t fieldno, uint32_t *out)
{
	const char *field = tuple_field_with_type(tuple, fieldno, MP_UINT);
	if (field == NULL)
		return -1;
	uint64_t val = mp_decode_uint(&field);
	*out = val;
	if (val > UINT32_MAX) {
		diag_set(ClientError, ER_FIELD_TYPE,
			 int2str(fieldno + TUPLE_INDEX_BASE),
			 "uint32_t", "uint64_t");
		return -1;
	}
	return 0;
}

/**
 * A convenience shortcut for data dictionary - get a tuple field
 * as a string.
 */
static inline const char *
tuple_field_str(struct tuple *tuple, uint32_t fieldno, uint32_t *len)
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
tuple_field_cstr(struct tuple *tuple, uint32_t fieldno)
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
tuple_field_uuid(struct tuple *tuple, int fieldno, struct tt_uuid *out)
{
	const char *value = tuple_field_cstr(tuple, fieldno);
	if (value == NULL)
		return -1;
	if (tt_uuid_from_string(value, out) != 0) {
		diag_set(ClientError, ER_INVALID_UUID, value);
		return -1;
	}
	return 0;
}

/**
 * Move a portion of reference counter from local_refs to external storage.
 * Must be called when local_refs need to be incremented, but it is already
 * at its storage limit (UINT8_MAX aka TUPLE_LOCAL_REF_MAX).
 */
void
tuple_upload_refs(struct tuple *tuple);

/**
 * Move a portion of reference counter from external storage to local_refs.
 * Must be called when local_refs becomes zero after decrement.
 */
void
tuple_acquire_refs(struct tuple *tuple);

/**
 * Increment tuple reference counter.
 * @param tuple Tuple to reference.
 */
static inline void
tuple_ref(struct tuple *tuple)
{
	if (unlikely(tuple->local_refs >= TUPLE_LOCAL_REF_MAX))
		tuple_upload_refs(tuple);
	tuple->local_refs++;
}

/**
 * Decrement tuple reference counter. If it has reached zero, free the tuple.
 *
 * @pre tuple->refs + count >= 0
 */
static inline void
tuple_unref(struct tuple *tuple)
{
	assert(tuple->local_refs >= 1);
	if (--tuple->local_refs == 0) {
		if (unlikely(tuple_has_flag(tuple, TUPLE_HAS_UPLOADED_REFS)))
			tuple_acquire_refs(tuple);
		else
			tuple_delete(tuple);
	}
}

/**
 * Get number of tuples that have uploaded references.
 */
size_t
tuple_bigref_tuple_count();

extern struct tuple *box_tuple_last;

/**
 * Convert internal `struct tuple` to public `box_tuple_t`.
 * \retval tuple
 * \post \a tuple ref counted until the next call.
 * \sa tuple_ref
 */
static inline box_tuple_t *
tuple_bless(struct tuple *tuple)
{
	assert(tuple != NULL);
	tuple_ref(tuple);
	/* Remove previous tuple */
	if (likely(box_tuple_last != NULL))
		tuple_unref(box_tuple_last);
	/* Remember current tuple */
	box_tuple_last = tuple;
	return tuple;
}

/**
 * \copydoc box_tuple_to_buf()
 */
ssize_t
tuple_to_buf(struct tuple *tuple, char *buf, size_t size);

/**
 * Amount of memory allocated on runtime arena for tuples.
 *
 * This metric disregards the internal fragmentation: it does not
 * count unused parts of slabs.
 *
 * Example: a tuple created using `box.tuple.new(<...>)` from Lua
 * uses this memory.
 */
size_t
tuple_runtime_memory_used(void);

/**
 * Allocate size bytes on runtime_alloc.
 * NB: Allocated memory will be accounted in runtime statisitcs
 * as if it was allocated for tuples.
 */
void *
runtime_memory_alloc(size_t size);

/**
 * Free memory of size bytes allocated on runtime_alloc.
 */
void
runtime_memory_free(void *ptr, size_t size);

#if defined(__cplusplus)
} /* extern "C" */

#include "xrow_update.h"
#include "errinj.h"

/* @copydoc tuple_field_u32() */
static inline uint32_t
tuple_field_u32_xc(struct tuple *tuple, uint32_t fieldno)
{
	uint32_t out;
	if (tuple_field_u32(tuple, fieldno, &out) != 0)
		diag_raise();
	return out;
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_H_INCLUDED */

