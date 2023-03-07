#ifndef TARANTOOL_BOX_INDEX_H_INCLUDED
#define TARANTOOL_BOX_INDEX_H_INCLUDED
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
#include <stdbool.h>
#include "small/rlist.h"
#include "trigger.h"
#include "trivia/util.h"
#include "iterator_type.h"
#include "index_def.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct engine;
struct space;
struct space_read_view;
struct index;
struct index_read_view;
struct index_read_view_iterator;
struct index_def;
struct key_def;
struct info_handler;

typedef struct tuple box_tuple_t;
typedef struct key_def box_key_def_t;

/** Context passed to box_on_select trigger callback. */
struct box_on_select_ctx {
	/** Target space. */
	struct space *space;
	/** Target index in the space. */
	struct index *index;
	/** Iterator type. */
	enum iterator_type type;
	/** Key (MsgPack array). */
	const char *key;
};

/**
 * Triggers invoked on select (by box_select, box_index_iterator, etc).
 * Trigger callback is passed on_box_select_ctx.
 */
extern struct rlist box_on_select;

/** Runs box_on_select triggers. */
static inline void
box_run_on_select(struct space *space, struct index *index,
		  enum iterator_type type, const char *key)
{
	if (likely(rlist_empty(&box_on_select)))
		return;
	struct box_on_select_ctx ctx = {
		.space = space,
		.index = index,
		.type = type,
		.key = key,
	};
	trigger_run(&box_on_select, &ctx);
}

/** \cond public */

typedef struct iterator box_iterator_t;

/**
 * Allocate and initialize iterator for space_id, index_id.
 *
 * A returned iterator must be destroyed by box_iterator_free().
 *
 * \param space_id space identifier.
 * \param index_id index identifier.
 * \param type \link iterator_type iterator type \endlink
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key
 * \retval NULL on error (check box_error_last())
 * \retval iterator otherwise
 * \sa box_iterator_next()
 * \sa box_iterator_free()
 */
box_iterator_t *
box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
		   const char *key, const char *key_end);
/**
 * Retrieve the next item from the \a iterator.
 *
 * \param iterator an iterator returned by box_index_iterator().
 * \param[out] result a tuple or NULL if there is no more data.
 * \retval -1 on error (check box_error_last() for details)
 * \retval 0 on success. The end of data is not an error.
 */
int
box_iterator_next(box_iterator_t *iterator, box_tuple_t **result);

/**
 * Destroy and deallocate iterator.
 *
 * \param iterator an iterator returned by box_index_iterator()
 */
void
box_iterator_free(box_iterator_t *iterator);

/**
 * Return the number of element in the index.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \retval -1 on error (check box_error_last())
 * \retval >= 0 otherwise
 */
ssize_t
box_index_len(uint32_t space_id, uint32_t index_id);

/**
 * Return the number of bytes used in memory by the index.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \retval -1 on error (check box_error_last())
 * \retval >= 0 otherwise
 */
ssize_t
box_index_bsize(uint32_t space_id, uint32_t index_id);

/**
 * Return a random tuple from the index (useful for statistical analysis).
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param rnd random seed
 * \param[out] result a tuple or NULL if index is empty
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:random(rnd) \endcode
 */
int
box_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd,
		box_tuple_t **result);

/**
 * Get a tuple from index by the key.
 *
 * Please note that this function works much more faster than
 * box_select() or box_index_iterator() + box_iterator_next().
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key
 * \param[out] result a tuple or NULL if index is empty
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \pre key != NULL
 * \sa \code box.space[space_id].index[index_id]:get(key) \endcode
 */
int
box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result);

/**
 * Return a first (minimal) tuple matched the provided key.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key.
 * \param[out] result a tuple or NULL if index is empty
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:min(key) \endcode
 */
int
box_index_min(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result);

/**
 * Return a last (maximal) tuple matched the provided key.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key.
 * \param[out] result a tuple or NULL if index is empty
 * \retval -1 on error (check box_error_last())
 * \retval 0 on success
 * \sa \code box.space[space_id].index[index_id]:max(key) \endcode
 */
int
box_index_max(uint32_t space_id, uint32_t index_id, const char *key,
	      const char *key_end, box_tuple_t **result);

/**
 * Count the number of tuple matched the provided key.
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param type iterator type - enum \link iterator_type \endlink
 * \param key encoded key in MsgPack Array format ([part1, part2, ...]).
 * \param key_end the end of encoded \a key.
 * \retval -1 on error (check box_error_last())
 * \retval >=0 on success
 * \sa \code box.space[space_id].index[index_id]:count(key,
 *     { iterator = type }) \endcode
 */
ssize_t
box_index_count(uint32_t space_id, uint32_t index_id, int type,
		const char *key, const char *key_end);

/**
 * Extract key from tuple according to key definition of given
 * index. Returned buffer is allocated on box_txn_alloc() with
 * this key.
 * @param tuple Tuple from which need to extract key.
 * @param space_id Space identifier.
 * @param index_id Index identifier.
 * @param[out] key_size where to store the size of the extracted
 *             key; pass NULL here if it is not needed
 * @retval not NULL Success
 * @retval     NULL Memory Allocation error
 */
char *
box_tuple_extract_key(box_tuple_t *tuple, uint32_t space_id,
		      uint32_t index_id, uint32_t *key_size);

/** \endcond public */

/**
 * Allocate and initialize iterator for space_id, index_id. If packed_pos is
 * not NULL, iterator will start right after tuple with position, described by
 * this argument. A returned iterator must be destroyed by box_iterator_free().
 */
box_iterator_t *
box_index_iterator_after(uint32_t space_id, uint32_t index_id, int type,
			 const char *key, const char *key_end,
			 const char *packed_pos, const char *packed_pos_end);

/**
 * A helper for position extractors. Get packed position of tuple in
 * index by its cmp_def. Returned position is allocated on the fiber region.
 */
int
box_iterator_position_from_tuple(const char *tuple, const char *tuple_end,
				 struct key_def *cmp_def,
				 const char **packed_pos,
				 const char **packed_pos_end);

/**
 * Get packed position of tuple in index to pass it to box_select
 * (multikey and func indexes are not supported). Returned position
 * is allocated on the fiber region.
 */
int
box_index_tuple_position(uint32_t space_id, uint32_t index_id,
			 const char *tuple, const char *tuple_end,
			 const char **packed_pos, const char **packed_pos_end);

/**
 * Index statistics (index:stat())
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param info info handler
 * \retval -1 on error (check box_error_last())
 * \retval >=0 on success
 */
int
box_index_stat(uint32_t space_id, uint32_t index_id,
	       struct info_handler *info);

/**
 * Trigger index compaction (index:compact())
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \retval -1 on error (check box_error_last())
 * \retval >=0 on success
 */
int
box_index_compact(uint32_t space_id, uint32_t index_id);

struct iterator {
	/**
	 * Same as next(), but returns a tuple as it is stored in the index,
	 * without any transformations. Used internally by engines. For
	 * example, memtx returns a tuple without decompression.
	 */
	int (*next_internal)(struct iterator *it, struct tuple **ret);
	/**
	 * Iterate to the next tuple.
	 * The tuple is returned in @ret (NULL if EOF).
	 * Returns 0 on success, -1 on error.
	 */
	int (*next)(struct iterator *it, struct tuple **ret);
	/**
	 * Get position of iterator - extracted cmp_def of last fetched
	 * tuple with MP_ARRAY header. If iterator is exhausted,
	 * it returns position of last tuple, if there were no tuples
	 * fetched, empty pos (NULL) is returned.
	 * Returned position is allocated on current fiber's region.
	 */
	int (*position)(struct iterator *it, const char **pos, uint32_t *size);
	/** Destroy the iterator. */
	void (*free)(struct iterator *);
	/** Space cache version at the time of the last index lookup. */
	uint32_t space_cache_version;
	/** ID of the space the iterator is for. */
	uint32_t space_id;
	/** ID of the index the iterator is for. */
	uint32_t index_id;
	/**
	 * Allocator that was used for allocation of fields pos_buf requires
	 * its size on deallocation.
	 */
	uint32_t pos_buf_size;
	/**
	 * Pointer to a buffer which was allocated for start position with
	 * runtime_alloc. Will be freed on iterator_delete.
	 * Needed for box_index_iterator_after.
	 */
	char *pos_buf;
	/**
	 * Pointer to the index the iterator is for.
	 * Guaranteed to be valid only if the schema
	 * state has not changed since the last lookup.
	 */
	struct index *index;
	/**
	 * Pointer to the space this iterator is for.
	 * Don't access directly, use iterator_space().
	 */
	struct space *space;
};

/**
 * Initialize a base iterator structure.
 *
 * This function is supposed to be used only by
 * index implementation so never call it directly,
 * use index_create_iterator() instead.
 */
void
iterator_create(struct iterator *it, struct index *index);

/** iterator_space() slow path. */
struct space *
iterator_space_slow(struct iterator *it);

/**
 * Returns the space this iterator is for or NULL if the iterator is invalid
 * (e.g. the index was dropped).
 */
static inline struct space *
iterator_space(struct iterator *it)
{
	extern uint32_t space_cache_version;
	if (likely(it->space != NULL &&
		   it->space_cache_version == space_cache_version))
		return it->space;
	return iterator_space_slow(it);
}

/**
 * Iterate to the next tuple.
 *
 * The tuple is returned in @ret (NULL if EOF).
 * Returns 0 on success, -1 on error.
 */
int
iterator_next(struct iterator *it, struct tuple **ret);

/**
 * Iterate to the next tuple as is, without any transformations.
 *
 * The tuple is returned in @ret (NULL if EOF).
 * Returns 0 on success, -1 on error.
 */
int
iterator_next_internal(struct iterator *it, struct tuple **ret);

/** Buffer size required for successful packing. */
size_t
iterator_position_pack_bufsize(const char *pos, const char *pos_end);

/**
 * Pack (encode to base64 format) iterator position with MP_ARRAY header to
 * preallocated buffer. The buffer size must be at least
 * iterator_position_pack_bufsize.
 */
void
iterator_position_pack(const char *pos, const char *pos_end,
		       char *buf, size_t buf_size,
		       const char **packed_pos, const char **packed_pos_end);

/** Buffer size required for successful unpacking. */
size_t
iterator_position_unpack_bufsize(const char *packed_pos,
				 const char *packed_pos_end);

/**
 * Unpack iterator position descriptor from MsgPack to preallocated buffer.
 * Returned position is a MsgPack key with MP_ARRAY header. The buffer size
 * must be at least iterator_position_unpack_bufsize.
 * Returns 0 on success, -1 on failure, diag is set.
 */
int
iterator_position_unpack(const char *packed_pos, const char *packed_pos_end,
			 char *buf, size_t buf_size, const char **pos,
			 const char **pos_end);

/**
 * Check an unpacked iterator position against the search criteria.
 * Note that both the position and the search key are given without
 * MP_ARRAY header.
 * Returns 0 on success, -1 on failure, diag is set.
 */
int
iterator_position_validate(const char *pos, uint32_t pos_part_count,
			   const char *key, uint32_t key_part_count,
			   struct key_def *cmp_def, enum iterator_type type);

/**
 * Get position of iterator - extracted cmp_def of last fetched
 * tuple (with MP_ARRAY header). If iterator is exhausted,
 * it returns position of last tuple, if there were no tuples
 * fetched, empty pos (NULL) is returned.
 * Returned position is allocated on current fiber's region.
 */
int
iterator_position(struct iterator *it, const char **pos, uint32_t *size);

/**
 * Destroy an iterator instance and free associated memory.
 */
void
iterator_delete(struct iterator *it);

/**
 * Check that the key has correct part count and correct part size
 * for use in an index iterator.
 *
 * @param index_def key definition
 * @param type iterator type (see enum iterator_type)
 * @param key msgpack-encoded key
 * @param part_count number of parts in \a key
 *
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
key_validate(const struct index_def *index_def, enum iterator_type type,
	     const char *key, uint32_t part_count);

/**
 * Check that the supplied key is valid for a search in a unique
 * index (i.e. the key must be fully specified).
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
exact_key_validate(struct key_def *key_def, const char *key,
		   uint32_t part_count);

/**
 * Check that the supplied key is valid for representing iterator
 * position (i.e. the key must be fully specified, but nulls are allowed).
 * @retval 0  The key is valid.
 * @retval -1 The key is invalid.
 */
int
exact_key_validate_nullable(struct key_def *key_def, const char *key,
			    uint32_t part_count);

/**
 * The manner in which replace in a unique index must treat
 * duplicates (tuples with the same value of indexed key),
 * possibly present in the index.
 */
enum dup_replace_mode {
	/**
	 * If a duplicate is found, delete it and insert
	 * a new tuple instead. Otherwise, insert a new tuple.
	 */
	DUP_REPLACE_OR_INSERT,
	/**
	 * If a duplicate is found, produce an error.
	 * I.e. require that no old key exists with the same
	 * value.
	 */
	DUP_INSERT,
	/**
	 * Unless a duplicate exists, throw an error.
	 */
	DUP_REPLACE
};

struct index_vtab {
	/** Free an index instance. */
	void (*destroy)(struct index *);
	/**
	 * Called after WAL write to commit index creation.
	 * Must not fail.
	 *
	 * @signature is the LSN that was assigned to the row
	 * that created the index. If the index was created by
	 * a snapshot row, it is set to the snapshot signature.
	 */
	void (*commit_create)(struct index *index, int64_t signature);
	/**
	 * Called if index creation failed, either due to
	 * WAL write error or build error.
	 */
	void (*abort_create)(struct index *index);
	/**
	 * Called after WAL write to comit index definition update.
	 * Must not fail.
	 *
	 * @signature is the LSN that was assigned to the row
	 * that modified the index.
	 */
	void (*commit_modify)(struct index *index, int64_t signature);
	/**
	 * Called after WAL write to commit index drop.
	 * Must not fail.
	 *
	 * @signature is the LSN that was assigned to the row
	 * that dropped the index.
	 */
	void (*commit_drop)(struct index *index, int64_t signature);
	/**
	 * Called after index definition update that did not
	 * require index rebuild.
	 */
	void (*update_def)(struct index *);
	/**
	 * Return true if the index depends on the primary
	 * key definition and hence needs to be updated if
	 * the primary key is modified.
	 */
	bool (*depends_on_pk)(struct index *);
	/**
	 * Return true if the change of index definition
	 * cannot be done without rebuild.
	 */
	bool (*def_change_requires_rebuild)(struct index *index,
					    const struct index_def *new_def);

	ssize_t (*size)(struct index *);
	ssize_t (*bsize)(struct index *);
	int (*min)(struct index *index, const char *key,
		   uint32_t part_count, struct tuple **result);
	int (*max)(struct index *index, const char *key,
		   uint32_t part_count, struct tuple **result);
	int (*random)(struct index *index, uint32_t rnd, struct tuple **result);
	ssize_t (*count)(struct index *index, enum iterator_type type,
			 const char *key, uint32_t part_count);
	/*
	 * Same as get(), but returns a tuple as it is stored in the index,
	 * without any transformations. Used internally by engines. For
	 * example, memtx returns a tuple without decompression.
	 */
	int (*get_internal)(struct index *index, const char *key,
			    uint32_t part_count, struct tuple **result);
	int (*get)(struct index *index, const char *key,
		   uint32_t part_count, struct tuple **result);
	/**
	 * Main entrance point for changing data in index. Once built and
	 * before deletion this is the only way to insert, replace and delete
	 * data from the index.
	 * @param mode - @sa dup_replace_mode description
	 * @param result - here the replaced or deleted tuple is placed.
	 * @param successor - if the index supports ordering, then in case of
	 *  insert (!) here the successor tuple is returned. In other words,
	 *  here will be stored the tuple, before which new tuple is inserted.
	 */
	int (*replace)(struct index *index, struct tuple *old_tuple,
		       struct tuple *new_tuple, enum dup_replace_mode mode,
		       struct tuple **result, struct tuple **successor);
	/**
	 * Create an index iterator. Iterator can be placed right after
	 * position, passed in pos argument. Argument pos is an extracted
	 * cmp_def without MP_ARRAY header or NULL.
	 */
	struct iterator *(*create_iterator)(struct index *index,
					    enum iterator_type type,
					    const char *key,
					    uint32_t part_count,
					    const char *pos);
	/** Create an index read view. */
	struct index_read_view *(*create_read_view)(struct index *index);
	/** Introspection (index:stat()) */
	void (*stat)(struct index *, struct info_handler *);
	/**
	 * Trigger asynchronous index compaction. What exactly
	 * is implied under 'compaction' depends on the engine.
	 */
	void (*compact)(struct index *);
	/** Reset all incremental statistic counters. */
	void (*reset_stat)(struct index *);
	/**
	 * Two-phase index creation: begin building, add tuples, finish.
	 */
	void (*begin_build)(struct index *);
	/**
	 * Optional hint, given to the index, about
	 * the total size of the index. Called after
	 * begin_build().
	 */
	int (*reserve)(struct index *index, uint32_t size_hint);
	int (*build_next)(struct index *index, struct tuple *tuple);
	void (*end_build)(struct index *index);
};

struct index {
	/** Virtual function table. */
	const struct index_vtab *vtab;
	/** Engine used by this index. */
	struct engine *engine;
	/* Description of a possibly multipart key. */
	struct index_def *def;
	/** Reference counter. */
	int refs;
	/** Space cache version at the time of construction. */
	uint32_t space_cache_version;
	/** Globally unique ID. */
	uint32_t unique_id;
	/** Compact ID - index in space->index array. */
	uint32_t dense_id;
	/**
	 * List of gap_item's describing gap reads in the index with NULL
	 * successor. Those gap reads happen when reading from empty index,
	 * or when reading from rightmost part of ordered index (TREE).
	 * @sa struct gap_item.
	 */
	struct rlist read_gaps;
	/** List of full scans of the index. @sa struct full_scan_item. */
	struct rlist full_scans;
};

/**
 * Auxiliary struct used for returning tuple data fetched from a read view.
 */
struct read_view_tuple {
	/**
	 * If a read view was created while a space upgrade was running,
	 * a tuple fetched from the read view may be either upgraded or not.
	 * This flag is set if the tuple needs to be upgraded.
	 */
	bool needs_upgrade;
	/** Pointer to tuple data. */
	const char *data;
	/** Size of tuple data. */
	uint32_t size;
};

/** Object returned if there's no more tuples matching the search criteria. */
static inline struct read_view_tuple
read_view_tuple_none(void)
{
	struct read_view_tuple tuple;
	tuple.needs_upgrade = false;
	tuple.data = NULL;
	tuple.size = 0;
	return tuple;
}

/** Index read view virtual function table. */
struct index_read_view_vtab {
	/** Free an index read view instance. */
	void
	(*free)(struct index_read_view *rv);
	/**
	 * Look up a tuple by a full key in a read view.
	 *
	 * The tuple data and size are returned in the result argument.
	 * If the key isn't found, the data is set to NULL.
	 *
	 * Note, unless the read_view_opts::disable_decompression flag was set
	 * at read_view_open, the returned data may be allocated on the fiber
	 * region, and the user is supposed to call region_truncate after using
	 * the data.
	 *
	 * Returns 0 on success. On error returns -1 and sets diag.
	 */
	int (*get_raw)(struct index_read_view *rv,
		       const char *key, uint32_t part_count,
		       struct read_view_tuple *result);
	/**
	 * Create an index read view iterator. Iterator can be placed right
	 * after position, passed in pos argument. Argument pos is an extracted
	 * cmp_def without MP_ARRAY header or NULL.
	 */
	int
	(*create_iterator)(struct index_read_view *rv, enum iterator_type type,
			   const char *key, uint32_t part_count,
			   const char *pos,
			   struct index_read_view_iterator *it);
};

/**
 * Index read view.
 *
 * An index read view is a frozen image of the index at the time of the read
 * view creation. It only has read-only methods. The API is similar to the API
 * of an index, but a read view returns raw data (not wrapped in struct tuple).
 *
 * Note about multi-threading: a read view may only be created and destroyed in
 * the tx thread, but it may be used in any other thread.
 */
struct index_read_view {
	/** Virtual function table. */
	const struct index_read_view_vtab *vtab;
	/** Copy of the index definition. */
	struct index_def *def;
	/** Pointer to the space read view that owns this index. */
	struct space_read_view *space;
};

/** Base class for iterator over an index read view. */
struct index_read_view_iterator_base {
	/** Pointer to the index read view. */
	struct index_read_view *index;
	/**
	 * Iterate to the next tuple in the read view.
	 *
	 * The tuple data and size are returned in the result argument.
	 * On EOF the data is set to NULL.
	 *
	 * Note, unless the read_view_opts::disable_decompression flag was set
	 * at read_view_open, the returned data may be allocated on the fiber
	 * region, and the user is supposed to call region_truncate after using
	 * the data.
	 *
	 * Returns 0 on success. On error returns -1 and sets diag.
	 */
	int
	(*next_raw)(struct index_read_view_iterator *iterator,
		    struct read_view_tuple *result);
	/**
	 * Get position of iterator - extracted cmp_def of last fetched
	 * tuple with MP_ARRAY header. If iterator is exhausted,
	 * it returns position of last tuple, if there were no tuples
	 * fetched, empty pos (NULL) is returned.
	 * Returned position is allocated on current fiber's region.
	 */
	int
	(*position)(struct index_read_view_iterator *iterator,
		    const char **pos, uint32_t *size);
};

/** Size of the index_read_view_iterator struct. */
#define INDEX_READ_VIEW_ITERATOR_SIZE 64

static_assert(sizeof(struct index_read_view_iterator_base) <=
	      INDEX_READ_VIEW_ITERATOR_SIZE,
	      "sizeof(struct index_read_view_iterator_base) must be less than "
	      "or equal to INDEX_READ_VIEW_ITERATOR_SIZE");

/**
 * Iterator over an index read view.
 *
 * Implemented as an opaque fixed-size structure so that it can be declared on
 * stack for any kind of read view iterator.
 */
struct index_read_view_iterator {
	union {
		/* Base class. */
		struct index_read_view_iterator_base base;
		/* Implementation dependent content. */
		char data[INDEX_READ_VIEW_ITERATOR_SIZE];
	};
};

/**
 * Check if replacement of an old tuple with a new one is
 * allowed.
 */
static inline uint32_t
replace_check_dup(struct tuple *old_tuple, struct tuple *dup_tuple,
		  enum dup_replace_mode mode)
{
	if (dup_tuple == NULL) {
		if (mode == DUP_REPLACE) {
			assert(old_tuple != NULL);
			/*
			 * dup_replace_mode is DUP_REPLACE, and
			 * a tuple with the same key is not found.
			 */
			return ER_CANT_UPDATE_PRIMARY_KEY;
		}
	} else { /* dup_tuple != NULL */
		if (dup_tuple != old_tuple &&
		    (old_tuple != NULL || mode == DUP_INSERT)) {
			/*
			 * There is a duplicate of new_tuple,
			 * and it's not old_tuple: we can't
			 * possibly delete more than one tuple
			 * at once.
			 */
			return ER_TUPLE_FOUND;
		}
	}
	return 0;
}

/**
 * Initialize an index instance.
 * Note, this function copies the given index definition.
 */
int
index_create(struct index *index, struct engine *engine,
	     const struct index_vtab *vtab, struct index_def *def);

/** Free an index instance. */
void
index_delete(struct index *index);

/**
 * Increment the reference counter of an index to prevent
 * it from being destroyed when the space it belongs to is
 * freed.
 */
static inline void
index_ref(struct index *index)
{
	assert(index->refs > 0);
	index->refs++;
}

/**
 * Decrement the reference counter of an index.
 * Destroy the index if it isn't used anymore.
 */
static inline void
index_unref(struct index *index)
{
	assert(index->refs > 0);
	if (--index->refs == 0)
		index_delete(index);
}

static inline void
index_commit_create(struct index *index, int64_t signature)
{
	index->vtab->commit_create(index, signature);
}

static inline void
index_abort_create(struct index *index)
{
	index->vtab->abort_create(index);
}

static inline void
index_commit_modify(struct index *index, int64_t signature)
{
	index->vtab->commit_modify(index, signature);
}

static inline void
index_commit_drop(struct index *index, int64_t signature)
{
	index->vtab->commit_drop(index, signature);
}

static inline void
index_update_def(struct index *index)
{
	index->vtab->update_def(index);
}

static inline bool
index_depends_on_pk(struct index *index)
{
	return index->vtab->depends_on_pk(index);
}

static inline bool
index_def_change_requires_rebuild(struct index *index,
				  const struct index_def *new_def)
{
	return index->vtab->def_change_requires_rebuild(index, new_def);
}

static inline ssize_t
index_size(struct index *index)
{
	return index->vtab->size(index);
}

static inline ssize_t
index_bsize(struct index *index)
{
	return index->vtab->bsize(index);
}

static inline int
index_min(struct index *index, const char *key,
	  uint32_t part_count, struct tuple **result)
{
	return index->vtab->min(index, key, part_count, result);
}

static inline int
index_max(struct index *index, const char *key,
	     uint32_t part_count, struct tuple **result)
{
	return index->vtab->max(index, key, part_count, result);
}

static inline int
index_random(struct index *index, uint32_t rnd, struct tuple **result)
{
	return index->vtab->random(index, rnd, result);
}

static inline ssize_t
index_count(struct index *index, enum iterator_type type,
	    const char *key, uint32_t part_count)
{
	return index->vtab->count(index, type, key, part_count);
}

static inline int
index_get_internal(struct index *index, const char *key,
		   uint32_t part_count, struct tuple **result)
{
	return index->vtab->get_internal(index, key, part_count, result);
}

static inline int
index_get(struct index *index, const char *key,
	   uint32_t part_count, struct tuple **result)
{
	return index->vtab->get(index, key, part_count, result);
}

static inline int
index_replace(struct index *index, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode,
	      struct tuple **result, struct tuple **successor)
{
	return index->vtab->replace(index, old_tuple, new_tuple, mode,
				    result, successor);
}

static inline struct iterator *
index_create_iterator_after(struct index *index, enum iterator_type type,
			    const char *key, uint32_t part_count,
			    const char *pos)
{
	return index->vtab->create_iterator(index, type, key, part_count,
					    pos);
}

static inline struct iterator *
index_create_iterator(struct index *index, enum iterator_type type,
		      const char *key, uint32_t part_count)
{
	return index->vtab->create_iterator(index, type, key, part_count, NULL);
}

static inline struct index_read_view *
index_create_read_view(struct index *index)
{
	return index->vtab->create_read_view(index);
}

static inline void
index_stat(struct index *index, struct info_handler *handler)
{
	index->vtab->stat(index, handler);
}

static inline void
index_compact(struct index *index)
{
	index->vtab->compact(index);
}

static inline void
index_reset_stat(struct index *index)
{
	index->vtab->reset_stat(index);
}

static inline void
index_begin_build(struct index *index)
{
	index->vtab->begin_build(index);
}

static inline int
index_reserve(struct index *index, uint32_t size_hint)
{
	return index->vtab->reserve(index, size_hint);
}

static inline int
index_build_next(struct index *index, struct tuple *tuple)
{
	return index->vtab->build_next(index, tuple);
}

static inline void
index_end_build(struct index *index)
{
	index->vtab->end_build(index);
}

/**
 * Initialize an index read view instance.
 * Note, this function copies the given index definition.
 */
int
index_read_view_create(struct index_read_view *rv,
		       const struct index_read_view_vtab *vtab,
		       struct index_def *def);

/** Free an index read view instance. */
void
index_read_view_delete(struct index_read_view *rv);

static inline int
index_read_view_get_raw(struct index_read_view *rv,
			const char *key, uint32_t part_count,
			struct read_view_tuple *result)
{
	return rv->vtab->get_raw(rv, key, part_count, result);
}

static inline int
index_read_view_create_iterator_after(struct index_read_view *rv,
				      enum iterator_type type,
				      const char *key, uint32_t part_count,
				      const char *pos,
				      struct index_read_view_iterator *it)
{
	return rv->vtab->create_iterator(rv, type, key, part_count, pos, it);
}

static inline int
index_read_view_create_iterator(struct index_read_view *rv,
				enum iterator_type type,
				const char *key, uint32_t part_count,
				struct index_read_view_iterator *it)
{
	return rv->vtab->create_iterator(rv, type, key, part_count, NULL, it);
}

static inline void
index_read_view_iterator_destroy(struct index_read_view_iterator *iterator)
{
	TRASH(iterator);
}

static inline int
index_read_view_iterator_next_raw(struct index_read_view_iterator *iterator,
				  struct read_view_tuple *result)
{
	return iterator->base.next_raw(iterator, result);
}

/** Specialization of iterator_position for read view. */
static inline int
index_read_view_iterator_position(struct index_read_view_iterator *iterator,
				  const char **pos, uint32_t *size)
{
	return iterator->base.position(iterator, pos, size);
}

/*
 * Virtual method stubs.
 */
void generic_index_commit_create(struct index *, int64_t);
void generic_index_abort_create(struct index *);
void generic_index_commit_modify(struct index *, int64_t);
void generic_index_commit_drop(struct index *, int64_t);
void generic_index_update_def(struct index *);
bool generic_index_depends_on_pk(struct index *);
bool generic_index_def_change_requires_rebuild(struct index *,
					       const struct index_def *);
ssize_t generic_index_bsize(struct index *);
ssize_t generic_index_size(struct index *);
int generic_index_min(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_max(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_random(struct index *, uint32_t, struct tuple **);
ssize_t generic_index_count(struct index *, enum iterator_type,
			    const char *, uint32_t);
int
generic_index_get_internal(struct index *index, const char *key,
			   uint32_t part_count, struct tuple **result);
int generic_index_get(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_replace(struct index *, struct tuple *, struct tuple *,
			  enum dup_replace_mode,
			  struct tuple **, struct tuple **);
struct index_read_view *
generic_index_create_read_view(struct index *index);
void generic_index_stat(struct index *, struct info_handler *);
void generic_index_compact(struct index *);
void generic_index_reset_stat(struct index *);
void generic_index_begin_build(struct index *);
int generic_index_reserve(struct index *, uint32_t);
struct iterator *
generic_index_create_iterator(struct index *base, enum iterator_type type,
			      const char *key, uint32_t part_count,
			      const char *pos);
int generic_index_build_next(struct index *, struct tuple *);
void generic_index_end_build(struct index *);
int
disabled_index_build_next(struct index *index, struct tuple *tuple);
int
disabled_index_replace(struct index *index, struct tuple *old_tuple,
		       struct tuple *new_tuple, enum dup_replace_mode mode,
		       struct tuple **result, struct tuple **successor);
int
exhausted_iterator_next(struct iterator *it, struct tuple **ret);
int
exhausted_index_read_view_iterator_next_raw(struct index_read_view_iterator *it,
					    struct read_view_tuple *result);
/** Unsupported feature error is returned. */
int
generic_iterator_position(struct iterator *it, const char **pos,
			  uint32_t *size);
int
generic_index_read_view_iterator_position(struct index_read_view_iterator *it,
					  const char **pos, uint32_t *size);

#if defined(__cplusplus)
} /* extern "C" */

#include "diag.h"

/*
 * A wrapper for ClientError(ER_UNSUPPORTED_INDEX_FEATURE, ...) to format
 * nice error messages (see gh-1042). You never need to catch this class.
 */
class UnsupportedIndexFeature: public ClientError {
public:
	UnsupportedIndexFeature(const char *file, unsigned line,
				struct index_def *index_def, const char *what);
};

struct IteratorGuard
{
	struct iterator *it;
	IteratorGuard(struct iterator *it_arg) : it(it_arg) {}
	~IteratorGuard() { iterator_delete(it); }
};

#endif /* defined(__plusplus) */

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
