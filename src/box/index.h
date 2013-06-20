#ifndef TARANTOOL_BOX_INDEX_H_INCLUDED
#define TARANTOOL_BOX_INDEX_H_INCLUDED
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
#include <stdbool.h>
#include "tarantool/util.h"

#include "object.h"

struct tuple;
struct space;

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_data_type { UNKNOWN = 0, NUM, NUM64, STRING, field_data_type_MAX };
extern const char *field_data_type_strs[];

#define INDEX_TYPE(_)                                               \
	_(HASH,    0)       /* HASH Index  */                       \
	_(TREE,    1)       /* TREE Index  */                       \
	_(BITSET,  2)       /* BITSET Index  */                     \

ENUM(index_type, INDEX_TYPE);
extern const char *index_type_strs[];

/**
 * @abstract Iterator type
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
#define ITERATOR_TYPE(_)                                             \
	_(ITER_ALL, 0)       /* all tuples                      */   \
	_(ITER_EQ,  1)       /* key == x ASC order              */   \
	_(ITER_REQ, 2)       /* key == x DESC order             */   \
	_(ITER_LT,  3)       /* key <  x                        */   \
	_(ITER_LE,  4)       /* key <= x                        */   \
	_(ITER_GE,  5)       /* key >= x                        */   \
	_(ITER_GT,  6)       /* key >  x                        */   \
	_(ITER_BITS_ALL_SET,     7) /* all bits from x are set in key      */ \
	_(ITER_BITS_ANY_SET,     8) /* at least one x's bit is set         */ \
	_(ITER_BITS_ALL_NOT_SET, 9) /* all bits are not set                */ \

ENUM(iterator_type, ITERATOR_TYPE);
extern const char *iterator_type_strs[];

static inline bool
iterator_type_is_reverse(enum iterator_type type)
{
	return type == ITER_REQ || type == ITER_LT || type == ITER_LE;
}

struct iterator {
	struct tuple *(*next)(struct iterator *);
	void (*free)(struct iterator *);
};

/** Descriptor of a single part in a multipart key. */
struct key_part {
	u32 fieldno;
	enum field_data_type type;
};

/* Descriptor of a multipart key. */
struct key_def {
	/* Description of parts of a multipart index. */
	struct key_part *parts;
	/*
	 * An array holding field positions in 'parts' array.
	 * Imagine there is index[1] = { key_field[0].fieldno=5,
	 * key_field[1].fieldno=3 }.
	 * 'parts' array for such index contains data from
	 * key_field[0] and key_field[1] respectively.
	 * max_fieldno is 5, and cmp_order array holds offsets of
	 * field 3 and 5 in 'parts' array: -1, -1, 0, -1, 1.
	 */
	u32 *cmp_order;
	/* The size of the 'parts' array. */
	u32 part_count;
	/*
	 * The size of 'cmp_order' array (= max fieldno in 'parts'
	 * array).
	 */
	u32 max_fieldno;
	bool is_unique;
	enum index_type type;
};

/** Descriptor of index features. */
struct index_traits
{
	bool allows_partial_key;
};

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


class Index: public Object {
public:

	/* Index owner space */
	struct space *space;
	/* Description of a possibly multipart key. */
	struct key_def *key_def;


	/**
	 * Allocate index instance.
	 *
	 * @param type     index type
	 * @param key_def  key part description
	 * @param space    space the index belongs to
	 */
	static Index *factory(enum index_type type, struct key_def *key_def,
		      struct space *space);

	/**
	 * Initialize index instance.
	 *
	 * @param key_def  key part description
	 * @param space    space the index belongs to
	 */
protected:
	Index(struct key_def *key_def, struct space *space);

public:
	virtual ~Index();

	/**
	 * Two-phase index creation: begin building, add tuples, finish.
	 */
	virtual void beginBuild() = 0;
	virtual void buildNext(struct tuple *tuple) = 0;
	virtual void endBuild() = 0;
	/** Build this index based on the contents of another index. */
	virtual void build(Index *pk) = 0;
	virtual size_t size() const = 0;
	virtual struct tuple *min() const = 0;
	virtual struct tuple *max() const = 0;
	virtual struct tuple *random(u32 rnd) const = 0;
	virtual struct tuple *findByKey(const char *key, u32 part_count) const = 0;
	virtual struct tuple *findByTuple(struct tuple *tuple) const = 0;
	virtual struct tuple *replace(struct tuple *old_tuple,
				      struct tuple *new_tuple,
				      enum dup_replace_mode mode) = 0;
	/**
	 * Create a structure to represent an iterator. Must be
	 * initialized separately.
	 */
	virtual struct iterator *allocIterator() const = 0;
	virtual void initIterator(struct iterator *iterator,
				  enum iterator_type type,
				  const char *key, u32 part_count) const = 0;

	inline struct iterator *position()
	{
		if (m_position == NULL)
			m_position = allocIterator();
		return m_position;
	}
protected:
	static void
	check_key_parts(const struct key_def *key_def, u32 part_count,
			bool partial_key_allowed);

	static uint32_t
	replace_check_dup(struct tuple *old_tuple,
			  struct tuple *dup_tuple,
			  enum dup_replace_mode mode);

private:
	/*
	 * Pre-allocated iterator to speed up the main case of
	 * box_process(). Should not be used elsewhere.
	 */
	struct iterator *m_position;
};

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
