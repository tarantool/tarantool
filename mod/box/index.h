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
#include <stdbool.h>
#include <util.h>

struct box_tuple;
struct space;
struct index;

/*
 * Possible field data types. Can't use STRS/ENUM macros for them,
 * since there is a mismatch between enum name (STRING) and type
 * name literal ("STR"). STR is already used as Objective C type.
 */
enum field_data_type { NUM, NUM64, STRING, field_data_type_MAX };
extern const char *field_data_type_strs[];

enum index_type { HASH, TREE, index_type_MAX };
extern const char *index_type_strs[];

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
};

@class Index;

@interface Index: Object {
 @public
	struct space *space;
	/*
	 * Pre-allocated iterator to speed up the main case of
	 * box_process(). Should not be used elsewhere.
	 */
	struct iterator *position;
	/* Description of a possibly multipart key. */
	struct key_def key_def;
	enum index_type type;
	bool enabled;
	/* Relative offset of the index in its namespace. */
	u32 n;
};

+ (Index *) alloc: (enum index_type) type_arg :(struct key_def *) key_def_arg;
/**
 * Initialize index instance.
 *
 * @param space    space the index belongs to
 * @param key      key part description
 */
- (id) init: (enum index_type) type_arg :(struct key_def *) key_def_arg
	:(struct space *) space_arg :(u32) n_arg;
/** Destroy and free index instance. */
- (void) free;
/**
 * Finish index construction.
 */
- (void) enable;
- (size_t) size;
- (struct box_tuple *) min;
- (struct box_tuple *) max;
- (struct box_tuple *) find: (void *) key_arg; /* only for unique lookups */
- (struct box_tuple *) findByTuple: (struct box_tuple *) tuple;
- (void) remove: (struct box_tuple *) tuple;
- (void) replace: (struct box_tuple *) old_tuple :(struct box_tuple *) new_tuple;
/**
 * Create a structure to represent an iterator. Must be
 * initialized separately.
 */
- (struct iterator *) allocIterator;
- (void) initIterator: (struct iterator *) iterator;
- (void) initIterator: (struct iterator *) iterator :(void *) key_arg
			:(int) part_count;
@end

struct iterator {
	struct box_tuple *(*next)(struct iterator *);
	struct box_tuple *(*next_equal)(struct iterator *);
	void (*free)(struct iterator *);
};

#define foreach_index(n, index_var)					\
	Index *index_var;						\
	for (Index **index_ptr = space[(n)].index;			\
	     *index_ptr != nil; index_ptr++)				\
		if ((index_var = *index_ptr)->enabled)

void build_indexes(void);

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
