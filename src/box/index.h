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
#include "trivia/util.h"
#include "iterator_type.h"
#include "index_def.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct engine;
struct index;
struct index_def;
struct key_def;
struct info_handler;

/** \cond public */

typedef struct tuple box_tuple_t;
typedef struct key_def box_key_def_t;
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
 * Retrive the next item from the \a iterator.
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
 * \param iterator an interator returned by box_index_iterator()
 */
void
box_iterator_free(box_iterator_t *iterator);

/**
 * @todo: delete, a hack added by @mejedi for sql
 */
const box_key_def_t *
box_iterator_key_def(box_iterator_t *iterator);

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
 * @retval not NULL Success
 * @retval     NULL Memory Allocation error
 */
char *
box_tuple_extract_key(const box_tuple_t *tuple, uint32_t space_id,
		      uint32_t index_id, uint32_t *key_size);

/** \endcond public */

/**
 * Index introspection (index:info())
 *
 * \param space_id space identifier
 * \param index_id index identifier
 * \param info info handler
 * \retval -1 on error (check box_error_last())
 * \retval >=0 on success
 */
int
box_index_info(uint32_t space_id, uint32_t index_id,
	       struct info_handler *info);

struct iterator {
	/**
	 * Iterate to the next tuple.
	 * The tuple is returned in @ret (NULL if EOF).
	 * Returns 0 on success, -1 on error.
	 */
	int (*next)(struct iterator *it, struct tuple **ret);
	/** Destroy the iterator. */
	void (*free)(struct iterator *);
	/** Schema version at the time of the last index lookup. */
	uint32_t schema_version;
	/** ID of the space the iterator is for. */
	uint32_t space_id;
	/** ID of the index the iterator is for. */
	uint32_t index_id;
	/**
	 * Pointer to the index the iterator is for.
	 * Guaranteed to be valid only if the schema
	 * version has not changed since the last lookup.
	 */
	struct index *index;
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

/**
 * Iterate to the next tuple.
 *
 * The tuple is returned in @ret (NULL if EOF).
 * Returns 0 on success, -1 on error.
 */
int
iterator_next(struct iterator *it, struct tuple **ret);

/**
 * Destroy an iterator instance and free associated memory.
 */
void
iterator_delete(struct iterator *it);

/**
 * Snapshot iterator.
 * \sa index::create_snapshot_iterator().
 */
struct snapshot_iterator {
	/**
	 * Iterate to the next tuple in the snapshot.
	 * Returns a pointer to the tuple data and its
	 * size or NULL if EOF.
	 */
	const char *(*next)(struct snapshot_iterator *, uint32_t *size);
	/**
	 * Destroy the iterator.
	 */
	void (*free)(struct snapshot_iterator *);
};

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
	 */
	void (*commit_drop)(struct index *);
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
	int (*get)(struct index *index, const char *key,
		   uint32_t part_count, struct tuple **result);
	int (*replace)(struct index *index, struct tuple *old_tuple,
		       struct tuple *new_tuple, enum dup_replace_mode mode,
		       struct tuple **result);
	/** Create an index iterator. */
	struct iterator *(*create_iterator)(struct index *index,
			enum iterator_type type,
			const char *key, uint32_t part_count);
	/**
	 * Create an ALL iterator with personal read view so further
	 * index modifications will not affect the iteration results.
	 * Must be destroyed by iterator_delete() after usage.
	 */
	struct snapshot_iterator *(*create_snapshot_iterator)(struct index *);
	/** Introspection (index:info()) */
	void (*info)(struct index *, struct info_handler *);
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
	/* Schema version at the time of construction. */
	uint32_t schema_version;
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
			/*
			 * dup_replace_mode is DUP_REPLACE, and
			 * a tuple with the same key is not found.
			 */
			return old_tuple ?
			       ER_CANT_UPDATE_PRIMARY_KEY : ER_TUPLE_NOT_FOUND;
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

/** Build this index based on the contents of another index. */
int
index_build(struct index *index, struct index *pk);

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
index_commit_drop(struct index *index)
{
	index->vtab->commit_drop(index);
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
index_get(struct index *index, const char *key,
	   uint32_t part_count, struct tuple **result)
{
	return index->vtab->get(index, key, part_count, result);
}

static inline int
index_replace(struct index *index, struct tuple *old_tuple,
	      struct tuple *new_tuple, enum dup_replace_mode mode,
	      struct tuple **result)
{
	return index->vtab->replace(index, old_tuple, new_tuple, mode, result);
}

static inline struct iterator *
index_create_iterator(struct index *index, enum iterator_type type,
		      const char *key, uint32_t part_count)
{
	return index->vtab->create_iterator(index, type, key, part_count);
}

static inline struct snapshot_iterator *
index_create_snapshot_iterator(struct index *index)
{
	return index->vtab->create_snapshot_iterator(index);
}

static inline void
index_info(struct index *index, struct info_handler *handler)
{
	index->vtab->info(index, handler);
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

/*
 * Virtual method stubs.
 */
void generic_index_commit_create(struct index *, int64_t);
void generic_index_abort_create(struct index *);
void generic_index_commit_modify(struct index *, int64_t);
void generic_index_commit_drop(struct index *);
void generic_index_update_def(struct index *);
bool generic_index_depends_on_pk(struct index *);
ssize_t generic_index_size(struct index *);
int generic_index_min(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_max(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_random(struct index *, uint32_t, struct tuple **);
ssize_t generic_index_count(struct index *, enum iterator_type,
			    const char *, uint32_t);
int generic_index_get(struct index *, const char *, uint32_t, struct tuple **);
int generic_index_replace(struct index *, struct tuple *, struct tuple *,
			  enum dup_replace_mode, struct tuple **);
struct snapshot_iterator *generic_index_create_snapshot_iterator(struct index *);
void generic_index_info(struct index *, struct info_handler *);
void generic_index_reset_stat(struct index *);
void generic_index_begin_build(struct index *);
int generic_index_reserve(struct index *, uint32_t);
int generic_index_build_next(struct index *, struct tuple *);
void generic_index_end_build(struct index *);

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

/*
 * C++ wrappers around index methods.
 * They throw an exception in case of error.
 */

static inline struct iterator *
index_create_iterator_xc(struct index *index, enum iterator_type type,
			 const char *key, uint32_t part_count)
{
	struct iterator *it = index_create_iterator(index, type,
						    key, part_count);
	if (it == NULL)
		diag_raise();
	return it;
}

static inline struct tuple *
iterator_next_xc(struct iterator *it)
{
	struct tuple *tuple;
	if (iterator_next(it, &tuple) != 0)
		diag_raise();
	return tuple;
}

#endif /* defined(__plusplus) */

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
