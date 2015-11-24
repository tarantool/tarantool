#ifndef TARANTOOL_BOX_INDEX_H_INCLUDED
#define TARANTOOL_BOX_INDEX_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

typedef struct tuple box_tuple_t;
/** A space iterator */
typedef struct iterator box_iterator_t;

/**
 * Controls how to iterate over tuples in an index.
 * Different index types support different iterator types.
 * For example, one can start iteration from a particular value
 * (request key) and then retrieve all tuples where keys are
 * greater or equal (= GE) to this key.
 *
 * If iterator type is not supported by the selected index type,
 * iterator constructor must fail with ER_UNSUPPORTED. To be
 * selectable for primary key, an index must support at least
 * ITER_EQ and ITER_GE types.
 *
 * NULL value of request key corresponds to the first or last
 * key in the index, depending on iteration direction.
 * (first key for GE and GT types, and last key for LE and LT).
 * Therefore, to iterate over all tuples in an index, one can
 * use ITER_GE or ITER_LE iteration types with start key equal
 * to NULL.
 * For ITER_EQ, the key must not be NULL.
 */

enum iterator_type {
	/* ITER_EQ must be the first member for request_create  */
	ITER_EQ               =  0, /* key == x ASC order                  */
	ITER_REQ              =  1, /* key == x DESC order                 */
	ITER_ALL              =  2, /* all tuples                          */
	ITER_LT               =  3, /* key <  x                            */
	ITER_LE               =  4, /* key <= x                            */
	ITER_GE               =  5, /* key >= x                            */
	ITER_GT               =  6, /* key >  x                            */
	ITER_BITS_ALL_SET     =  7, /* all bits from x are set in key      */
	ITER_BITS_ANY_SET     =  8, /* at least one x's bit is set         */
	ITER_BITS_ALL_NOT_SET =  9, /* all bits are not set                */
	ITER_OVERLAPS         = 10, /* key overlaps x                      */
	ITER_NEIGHBOR         = 11, /* typles in distance ascending order from specified point */
	iterator_type_MAX     = ITER_NEIGHBOR + 1
};

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

/** \endcond public */


/** \cond public */

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
 * If NULL then equvivalent to an empty array.
 * \param key_end the end of encoded \a key.
 * Must be NULL if \a key is NULL.
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
 * If NULL then equvivalent to an empty array.
 * \param key_end the end of encoded \a key.
 * Must be NULL if \a key is NULL.
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
 * If NULL then equvivalent to an empty array.
 * \param key_end the end of encoded \a key.
 * Must be NULL if \a key is NULL.
 * \retval -1 on error (check box_error_last())
 * \retval >=0 on success
 * \sa \code box.space[space_id].index[index_id]:count(key,
 *     { iterator = type }) \endcode
 */
ssize_t
box_index_count(uint32_t space_id, uint32_t index_id, int type,
		const char *key, const char *key_end);

/** \endcond public */

extern const char *iterator_type_strs[];

#if defined(__cplusplus)
} /* extern "C" */
#include "key_def.h"

struct iterator {
	struct tuple *(*next)(struct iterator *);
	void (*free)(struct iterator *);
	/* optional parameters used in lua */
	int sc_version;
	uint32_t space_id;
	uint32_t index_id;
	class Index *index;
};

static inline bool
iterator_type_is_reverse(enum iterator_type type)
{
	return type == ITER_REQ || type == ITER_LT || type == ITER_LE;
}

/**
 * Check that the key has correct part count and correct part size
 * for use in an index iterator.
 *
 * @param key_def key definition
 * @param type iterator type (see enum iterator_type)
 * @param key msgpack-encoded key
 * @param part_count number of parts in \a key
 */
void
key_validate(struct key_def *key_def, enum iterator_type type, const char *key,
	     uint32_t part_count);

/**
 * Check that the supplied key is valid for a search in a unique
 * index (i.e. the key must be fully specified).
 */
void
primary_key_validate(struct key_def *key_def, const char *key,
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

class Index {
public:
	/* Description of a possibly multipart key. */
	struct key_def *key_def;
	/* Schema version on index construction moment */
	int sc_version;

protected:
	/**
	 * Initialize index instance.
	 *
	 * @param key_def  key part description
	 */
	Index(struct key_def *key_def);

public:
	virtual ~Index();

	Index(const Index &) = delete;
	Index& operator=(const Index&) = delete;

	virtual size_t size() const;
	virtual struct tuple *min(const char *key, uint32_t part_count) const;
	virtual struct tuple *max(const char *key, uint32_t part_count) const;
	virtual struct tuple *random(uint32_t rnd) const;
	virtual size_t count(enum iterator_type type, const char *key,
			     uint32_t part_count) const;
	virtual struct tuple *findByKey(const char *key, uint32_t part_count) const = 0;
	virtual struct tuple *findByTuple(struct tuple *tuple) const;
	virtual struct tuple *replace(struct tuple *old_tuple,
				      struct tuple *new_tuple,
				      enum dup_replace_mode mode) = 0;
	virtual size_t bsize() const;

	/**
	 * Create a structure to represent an iterator. Must be
	 * initialized separately.
	 */
	virtual struct iterator *allocIterator() const = 0;
	virtual void initIterator(struct iterator *iterator,
				  enum iterator_type type,
				  const char *key, uint32_t part_count) const = 0;

	/**
	 * Create a read view for iterator so further index modifications
	 * will not affect the iteration results.
	 */
	virtual void createReadViewForIterator(struct iterator *iterator);
	/**
	 * Destroy a read view of an iterator. Must be called for iterators,
	 * for which createReadViewForIterator() was called.
	 */
	virtual void destroyReadViewForIterator(struct iterator *iterator);
};

struct IteratorGuard
{
	struct iterator *it;
	IteratorGuard(struct iterator *it_arg) : it(it_arg) {}
	~IteratorGuard() { it->free(it); }
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

/** Get index ordinal number in space. */
static inline uint32_t
index_id(const Index *index)
{
	return index->key_def->iid;
}

static inline const char *
index_name(const Index *index)
{
	return index->key_def->name;
}

/** True if this index is a primary key. */
static inline bool
index_is_primary(const Index *index)
{
	return index_id(index) == 0;
}

#endif /* defined(__plusplus) */

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
