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
#include <mod/box/assoc.h>

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

struct tree_index_member {
	struct box_tuple *tuple;
	struct field key[];
};

#define SIZEOF_TREE_INDEX_MEMBER(index) \
	(sizeof(struct tree_index_member) + sizeof(struct field) * (index)->key_cardinality)

#include <third_party/sptree.h>
SPTREE_DEF(str_t, realloc);

struct index {
	bool enabled;
	bool unique;

	size_t (*size)(struct index *index);
	struct box_tuple *(*find) (struct index * index, void *key);	/* only for unique lookups */
	struct box_tuple *(*find_by_tuple) (struct index * index, struct box_tuple * pattern);
	void (*remove) (struct index * index, struct box_tuple *);
	void (*replace) (struct index * index, struct box_tuple *, struct box_tuple *);
	void (*iterator_init) (struct index *, int cardinality, void *key);
	struct box_tuple *(*iterator_next) (struct index *);
	struct box_tuple *(*iterator_next_nocompare) (struct index *);
	union {
		khash_t(lstr_ptr_map) * str_hash;
		khash_t(int_ptr_map) * int_hash;
		khash_t(int64_ptr_map) * int64_hash;
		khash_t(int_ptr_map) * hash;
		sptree_str_t *tree;
	} idx;
	void *iterator;
	struct tree_index_member *search_pattern;

	struct space *space;

	struct {
		u32 fieldno;
		enum field_data_type type;
	} *key_field;
	u32 *field_cmp_order;
	u32 field_cmp_order_cnt;
	u32 key_cardinality;
	/* relative offset of the index in the namespace */
	u32 n;


	enum index_type type;
};

#define foreach_index(n, index_var)					\
	for (struct index *index_var = space[(n)].index;		\
	     index_var->key_cardinality != 0;				\
	     index_var++)						\
		if (index_var->enabled)

void
index_init(struct index *index, struct space *space, size_t estimated_rows);

void
index_free(struct index *index);

struct box_txn;
void validate_indexes(struct box_txn *txn);
void build_indexes(void);

#endif /* TARANTOOL_BOX_INDEX_H_INCLUDED */
