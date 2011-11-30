#ifndef TARANTOOL_BOX_INDEX_H_INCLUDED
#define TARANTOOL_BOX_INDEX_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#import <objc/Object.h>
#include <assoc.h>

/**
 * A field reference used for TREE indexes. Either stores a copy
 * of the corresponding field in the tuple or points to that field
 * in the tuple (depending on field length).
 */

struct field {
	/** Field data length. */
	u32 len;
	/** Actual field data. For small fields we store the value
	 * of the field (u32, u64, strings up to 8 bytes), for
	 * longer fields, we store a pointer to field data in the
	 * tuple in the primary index.
	 */
	union {
		u32 u32;
		u64 u64;
		u8 data[sizeof(u64)];
		void *data_ptr;
	};
};

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_data_type { NUM, NUM64, STRING, field_data_type_MAX };
extern const char *field_data_type_strs[];

enum index_type { HASH, TREE, index_type_MAX };
extern const char *index_type_strs[];

struct index_tree_el {
	struct box_tuple *tuple;
	struct field key[];
};

#define INDEX_TREE_EL_SIZE(index) \
	(sizeof(struct index_tree_el) + sizeof(struct field) * (index)->key.part_count)

#include <third_party/sptree.h>
SPTREE_DEF(str_t, realloc);

/* Indexes at preallocated search positions.  */
enum { POS_READ = 0, POS_WRITE = 1, POS_MAX = 2 };

/** Descriptor of a single part in a multipart key. */

struct key_part {
	u32 fieldno;
	enum field_data_type type;
};

/* Descriptor of a multipart key. */

struct key {
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
};

@class Index;

@interface Index: Object {
 @public
	bool enabled;
	bool unique;

	size_t (*size)(Index *index);
	struct box_tuple *(*find)(Index *index, void *key); /* only for unique lookups */
	struct box_tuple *(*min)(Index *index);
	struct box_tuple *(*max)(Index *index);
	struct box_tuple *(*find_by_tuple)(Index * index, struct box_tuple * pattern);
	void (*remove)(Index *index, struct box_tuple *);
	void (*replace)(Index *index, struct box_tuple *, struct box_tuple *);
	void (*iterator_init)(Index *, int cardinality, void *key);
	union {
		struct mh_lstrptr_t *str_hash;
		struct mh_i32ptr_t *int_hash;
		struct mh_i64ptr_t *int64_hash;
		struct mh_i32ptr_t *hash;
		sptree_str_t *tree;
	} idx;
	struct iterator {
		union {
			struct sptree_str_t_iterator *t_iter;
			mh_int_t h_iter;
		};
		struct box_tuple *(*next)(Index *);
		struct box_tuple *(*next_equal)(Index *);
	} iterator;
	/* Reusable iteration positions, to save on memory allocation. */
	struct index_tree_el *position[POS_MAX];

	struct space *space;

	/* Description of a possibly multipart key. */
	struct key key;

	/* relative offset of the index in the namespace */
	u32 n;

	enum index_type type;
};

/**
 * Initialize index instance.
 *
 * @param space    space the index belongs to
 */
- (void) init: (struct space *) space_arg;
/**
 * Destroy and free index instance.
 */
- (void) free;
@end

#define foreach_index(n, index_var)					\
	Index *index_var;						\
	for (Index **index_ptr = space[(n)].index;			\
	     *index_ptr != nil; index_ptr++)				\
		if ((index_var = *index_ptr)->enabled)

struct box_txn;
void validate_indexes(struct box_txn *txn);
void build_indexes(void);

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
