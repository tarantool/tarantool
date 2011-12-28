/*
 * Copyright (C) 2011 Mail.RU
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

#include "tree.h"
#include "box.h"
#include "tuple.h"
#include <salloc.h>
#include <pickle.h>

/* {{{ Tree internal data types. **********************************/

/**
 * Tree types
 *
 * There are four specialized forms of tree indexes optimized for different
 * combinations of index fields.
 *
 * In the most general case tuples consist of variable length fields and the
 * index uses a sparsely distributed subset of these fields. So to determine
 * the field location in the tuple it is required to scan all the preceding
 * fields. We use the SPARSE tree index structure to avoid such scans on each
 * access. It pre-computes and stores the field offsets (or the field values)
 * on the per-tuple basis in the sparse_part_t array.
 *
 * In case index fields form a dense sequence it is possible to find
 * each successive field location based on the previous field location. So it
 * is only required to store the offset of the first field of the index per
 * each tuple. In this case we use the DENSE tree index structure.
 *
 * In case the index consists of only one small field it is cheaper to
 * store the field value directly in the index rather than store the offset
 * to the value. In this case we use the NUM32 tree index structure.
 *
 * In case the first field offset in a dense sequence is constant there
 * is no need to store any extra data in the tree. This happens if the first
 * index field is also the first tuple field or all the preceding fields are
 * fixed (that is NUMs). In this case we use the FIXED tree structure.
 *
 * Note that currently we do not know types of the fields that are not used
 * in any index. So we cannot actually determine if the fields preceding to
 * the index are fixed length. Therefore we may miss the opportunity to use
 * this optimization in some cases.
 */
enum tree_type { TREE_SPARSE, TREE_DENSE, TREE_NUM32, TREE_FIXED };

/**
 * Representation of a STR field within a sparse tree index.

 * Depending on the STR length we keep either the offset of the field within
 * the tuple or a copy of the field. Specifically, if the STR length is less
 * than or equal to 7 then the length is stored in the "length" field while
 * the copy of the STR data in the "data" field. Otherwise the STR offset in
 * the tuple is stored in the "offset" field. The "length" field in this case
 * is set to 0xFF. The actual length has to be read from the tuple.
 */
struct sparse_str
{
	union
	{
		u8 data[7];
		u32 offset;
	};
	u8 length;
} __attribute__((packed));

#define BIG_LENGTH 0xff

/**
 * Reprsentation of a tuple field within a sparse tree index.
 *
 * For all NUMs and short STRs it keeps a copy of the field, for long STRs
 * it keeps the offset of the field in the tuple.
 */
union sparse_part {
	u32 num32;
	u64 num64;
	struct sparse_str str;
};

#define _SIZEOF_SPARSE_PARTS(part_count) \
	(sizeof(union sparse_part) * (part_count))

#define SIZEOF_SPARSE_PARTS(def) _SIZEOF_SPARSE_PARTS((def)->part_count)

/**
 * Tree nodes for different tree types
 */

struct sparse_node {
	struct box_tuple *tuple;
	union sparse_part parts[];
} __attribute__((packed));

struct dense_node {
	struct box_tuple *tuple;
	u32 offset;
} __attribute__((packed));

struct num32_node {
	struct box_tuple *tuple;
	u32 value;
} __attribute__((packed));

struct fixed_node {
	struct box_tuple *tuple;
};

/**
 * Key data with possibly partially specified number of key parts.
 */
struct key_data
{
	u8 *data;
	int part_count;
	union sparse_part parts[];
};

#define SIZEOF_KEY_DATA(key_def) \
	(sizeof(struct key_data) + SIZEOF_SPARSE_PARTS((key_def)->part_count))

/**
 * Collection of offsets to data of key parts
 */
struct data_offset
{
	int part_count;
	u32 *parts; 
};

/* }}} */

/* {{{ Tree auxiliary functions. **********************************/

/**
 * Find if the field has fixed offset.
 */
static int
find_fixed_offset(struct space *space, int fieldno, int skip)
{
	int i = skip;
	int offset = 0;

	while (i < fieldno) {
		/* if the field is unknown give up on it */
		if (i >= space->field_count || space->field_types[i] == UNKNOWN) {
			return -1;
		}

		/* On a fixed length field account for the appropiate
		   varint length code and for the actual data length */
		if (space->field_types[i] == NUM) {
			offset += 1 + 4;
		} else if (space->field_types[i] == NUM64) {
			offset += 1 + 8;
		}
		/* On a variable length field give up */
		else {
			return -1;
		}

		++i;
	}

	return offset;
}

/**
 * Find the appropriate tree type for a given key.
 */
static enum tree_type
find_tree_type(struct space *space, struct key_def *key_def)
{
	int dense = 1;
	int fixed = 1;
	int first = 1;
	int skip = 0;

	for (int field = 0; field < key_def->max_fieldno; ++field) {
		int part = key_def->cmp_order[field];
		if (part != -1) {
			if (find_fixed_offset(space, field, skip) < 0) {
				if (first) {
					fixed = 0;
				} else {
					dense = 0;
					break;
				}
			}
			first = 0;
			skip = field;
		}
	}

	if (!dense) {
		return TREE_SPARSE;
	} else if (fixed) {
		return TREE_FIXED;
	} else if (key_def->part_count == 1 && key_def->parts[0].type == NUM) {
		return TREE_NUM32;
	} else{
		return TREE_DENSE;
	}
}

/**
 * Find first index field for a dense/fixed node
 */
static u32
find_first_field(struct key_def *key_def)
{
	for (int field = 0; field < key_def->max_fieldno; ++field) {
		int part = key_def->cmp_order[field];
		if (part != -1) {
			return field;
		}
	}
	panic("index field not found");
}

/**
 * Find field offsets/values for a sparse node
 */
static void
fold_sparse_parts(struct key_def *key_def, struct box_tuple *tuple, union sparse_part* parts)
{
	u8 *part_data = tuple->data;

	for (int field = 0; field < key_def->max_fieldno; ++field) {
		assert(field < tuple->cardinality);

		u8 *data = part_data;
		u32 len = load_varint32((void**) &data);

		int part = key_def->cmp_order[field];
		if (part != -1) {
			if (key_def->parts[part].type == NUM) {
				assert(len == sizeof parts[part].num32);
				memcpy(&parts[part].num32, data, len);
			} else if (key_def->parts[part].type == NUM64) {
				assert(len == sizeof parts[part].num64);
				memcpy(&parts[part].num64, data, len);
			} else if (len <= sizeof(parts[part].str.data)) {
				parts[part].str.length = len;
				memcpy(parts[part].str.data, data, len);
			} else {
				parts[part].str.length = BIG_LENGTH;
				parts[part].str.offset = (u32) (part_data - tuple->data);
			}
		}

		part_data = data + len;
	}
}

/**
 * Find field offsets/values for a key
 */
static void
fold_key_parts(struct key_def *key_def, struct key_data *key_data)
{
	u8 *part_data = key_data->data;
	union sparse_part* parts = key_data->parts;

	for (int part = 0; part < key_data->part_count; ++part) {
		u8 *data = part_data;
		u32 len = load_varint32((void**) &data);

		if (key_def->parts[part].type == NUM) {
			assert(len == sizeof parts[part].num32);
			memcpy(&parts[part].num32, data, len);
		} else if (key_def->parts[part].type == NUM64) {
			assert(len == sizeof parts[part].num64);
			memcpy(&parts[part].num64, data, len);
		} else if (len <= sizeof(parts[part].str.data)) {
			parts[part].str.length = len;
			memcpy(parts[part].str.data, data, len);
		} else {
			parts[part].str.length = BIG_LENGTH;
			parts[part].str.offset = (u32) (part_data - key_data->data);
		}

		part_data = data + len;
	}
}

/**
 * Find the offset for a dense node
 */
static u32
fold_dense_offset(struct key_def *key_def, struct box_tuple *tuple)
{
	u8 *tuple_data = tuple->data;

	for (int field = 0; field < key_def->max_fieldno; ++field) {
		assert(field < tuple->cardinality);

		u8 *data = tuple_data;
		u32 len = load_varint32((void**) &data);

		int part = key_def->cmp_order[field];
		if (part != -1) {
			return (u32) (tuple_data - tuple->data);
		}

		tuple_data = data + len;
	}

	panic("index field not found");
}

/**
 * Find the value for a num32 node
 */
static u32
fold_num32_value(struct key_def *key_def, struct box_tuple *tuple)
{
	u8 *tuple_data = tuple->data;

	for (int field = 0; field < key_def->max_fieldno; ++field) {
		assert(field < tuple->cardinality);

		u8 *data = tuple_data;
		u32 len = load_varint32((void**) &data);

		int part = key_def->cmp_order[field];
		if (part != -1) {
			u32 value;
			assert(len == sizeof value);
			memcpy(&value, data, sizeof value);
			return value;
		}

		tuple_data = data + len;
	}

	panic("index field not found");
}

/**
 * Compare a key part of sparse nodes
 */
static int
sparse_part_compare(
	enum field_data_type type,
	const u8 *data_a, union sparse_part part_a,
	const u8 *data_b, union sparse_part part_b)
{
	if (type == NUM) {
		u64 an = part_a.num32;
		u64 bn = part_b.num32;
		return (int) (((i64)an - (i64)bn) >> 32);
	} else if (type == NUM64) {
		u64 an = part_a.num64;
		u64 bn = part_b.num64;
		return an == bn ? 0 : an > bn ? 1 : -1;
	} else {
		int cmp;
		const u8 *ad, *bd;
		u32 al = part_a.str.length;
		u32 bl = part_b.str.length;
		if (al == BIG_LENGTH) {
			ad = data_a + part_a.str.offset;
			al = load_varint32((void **) &ad);
		} else {
			assert(al <= sizeof(part_a.str.data));
			ad = part_a.str.data;
		}
		if (bl == BIG_LENGTH) {
			bd = data_b + part_b.str.offset;
			bl = load_varint32((void **) &bd);
		} else {
			assert(bl <= sizeof(part_b.str.data));
			bd = part_b.str.data;
		}

		cmp = memcmp(ad, bd, MIN(al, bl));
		if (cmp == 0) {
			cmp = (int) ((((i64)(u64)al) - ((i64)(u64)bl)) >> 32);
		}

		return cmp;
	}
}

/**
 * Compare the keys of sparse nodes
 */
static int
sparse_node_compare(
	struct key_def *key_def,
	struct box_tuple *tuple_a, const union sparse_part* parts_a,
	struct box_tuple *tuple_b, const union sparse_part* parts_b)
{
	for (int part = 0; part < key_def->part_count; ++part) {
		int r = sparse_part_compare(
			key_def->parts[part].type,
			tuple_a->data, parts_a[part],
			tuple_b->data, parts_b[part]);
		if (r) {
			return r;
		}
	}
	return 0;
}

static int
sparse_key_node_compare(
	struct key_def *key_def, const struct key_data *key_data,
	struct box_tuple *tuple, const union sparse_part* parts)
{
	for (int part = 0; part < key_data->part_count; ++part) {
		int r = sparse_part_compare(
			key_def->parts[part].type,
			key_data->data, key_data->parts[part],
			tuple->data, parts[part]);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare a key part of dense nodes
 */
static int
dense_part_compare(
	enum field_data_type type,
	const u8 *data_a, u32 offset_a,
	const u8 *data_b, u32 offset_b)
{
	const u8 *ad = data_a + offset_a;
	const u8 *bd = data_b + offset_b;
	u32 al = load_varint32((void *) &ad);
	u32 bl = load_varint32((void *) &bd);
	if (type == NUM) {
		u32 an, bn;
		assert(al == sizeof an && bl == sizeof bn);
		memcpy(&an, ad, sizeof an);
		memcpy(&bn, bd, sizeof bn);
		return (int) (((i64)(u64)an - (i64)(u64)bn) >> 32);
	} else if (type == NUM64) {
		u64 an, bn;
		assert(al == sizeof an && bl == sizeof bn);
		memcpy(&an, ad, sizeof an);
		memcpy(&bn, bd, sizeof bn);
		return an == bn ? 0 : an > bn ? 1 : -1;
	} else {
		int cmp = memcmp(ad, bd, MIN(al, bl));
		if (cmp == 0) {
			cmp = (int) ((((i64)(u64)al) - ((i64)(u64)bl)) >> 32);
		}
		return cmp;
	}
}

/**
 * Compare the keys of dense nodes
 */
static int
dense_node_compare(
	struct key_def *key_def,
	u32 first_field,
	struct box_tuple *tuple_a, u32 offset_a,
	struct box_tuple *tuple_b, u32 offset_b)
{
	/* find field offsets */
	u32 off_a[key_def->part_count];
	u32 off_b[key_def->part_count];
	u8 *ad = tuple_a->data + offset_a;
	u8 *bd = tuple_b->data + offset_b;
	for (int i = 0; i < key_def->part_count; ++i) {
		assert(first_field + i < tuple_a->cardinality);
		assert(first_field + i < tuple_b->cardinality);
		off_a[i] = ad - tuple_a->data;
		off_b[i] = bd - tuple_b->data;
		u32 al = load_varint32((void**) &ad);
		u32 bl = load_varint32((void**) &bd);
		ad += al;
		bd += bl;
	}

	/* compare key parts */
	for (int part = 0; part < key_def->part_count; ++part) {
		int field = key_def->parts[part].fieldno;
		int r = dense_part_compare(
			key_def->parts[part].type,
			tuple_a->data, off_a[field - first_field],
			tuple_b->data, off_b[field - first_field]);
		if (r) {
			return r;
		}
	}
	return 0;
}

static int
dense_key_part_compare(
	enum field_data_type type,
	const u8 *data_a, union sparse_part part_a,
	const u8 *data_b, u32 offset_b)
{
	const u8 *bd = data_b + offset_b;
	u32 bl = load_varint32((void *) &bd);
	if (type == NUM) {
		u32 an, bn;
		an = part_a.num32;
		assert(bl == sizeof bn);
		memcpy(&bn, bd, sizeof bn);
		return (int) (((i64)(u64)an - (i64)(u64)bn) >> 32);
	} else if (type == NUM64) {
		u64 an, bn;
		an = part_a.num64;
		assert(bl == sizeof bn);
		memcpy(&bn, bd, sizeof bn);
		return an == bn ? 0 : an > bn ? 1 : -1;
	} else {
		int cmp;
		const u8 *ad;
		u32 al = part_a.str.length;
		if (al == BIG_LENGTH) {
			ad = data_a + part_a.str.offset;
			al = load_varint32((void **) &ad);
		} else {
			assert(al <= sizeof(part_a.str.data));
			ad = part_a.str.data;
		}

		cmp = memcmp(ad, bd, MIN(al, bl));
		if (cmp == 0) {
			cmp = (int) ((((i64)(u64)al) - ((i64)(u64)bl)) >> 32);
		}

		return cmp;
	}
}

static int
dense_key_node_compare(
	struct key_def *key_def, const struct key_data *key_data,
	u32 first_field, struct box_tuple *tuple, u32 offset)
{
	/* find field offsets */
	u32 off[key_def->part_count];
	u8 *data = tuple->data + offset;
	for (int i = 0; i < key_def->part_count; ++i) {
		assert(first_field + i < tuple->cardinality);
		off[i] = data - tuple->data;
		u32 len = load_varint32((void**) &data);
		data += len;
	}

	/* compare key parts */
	for (int part = 0; part < key_data->part_count; ++part) {
		int field = key_def->parts[part].fieldno;
		int r = dense_key_part_compare(
			key_def->parts[part].type,
			key_data->data, key_data->parts[part],
			tuple->data, off[field - first_field]);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare tree nodes
 */
static int
node_compare(const void *node_a, const void *node_b, void *arg)
{
	TreeIndex *index = (TreeIndex *) arg;
	return [index compare: node_a : node_b];
}

/**
 * Compare tree nodes with possibly duplicate key values
 */
static int
node_compare_with_dups(const void *node_a, const void *node_b, void *arg)
{
	TreeIndex *index = (TreeIndex *) arg;
	int r = [index compare: node_a : node_b];
	if (r == 0) {
		struct box_tuple *tuple_a = [index unfold: node_a];
		struct box_tuple *tuple_b = [index unfold: node_b];
		if (tuple_a != NULL && tuple_b != NULL) {
			return (tuple_a - tuple_b);
		}
	}
	return 0;
}

/**
 * Compare a key with a tree node
 */
static int
key_compare(const void *key, const void *node, void *arg)
{
	TreeIndex *index = (TreeIndex *) arg;
	return [index key_compare: key :node ];
}

/* }}} */

/* {{{ Tree iterator **********************************************/

struct tree_iterator {
	struct iterator base;
	TreeIndex *index;
	struct sptree_index_iterator *iter;
	struct key_data key_data;
};

static inline struct tree_iterator *
tree_iterator(struct iterator *it)
{
	return (struct tree_iterator *) it;
}

static struct box_tuple *
tree_iterator_next(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_next(it->iter);
	return [it->index unfold: node];
}

static struct box_tuple *
tree_iterator_next_equal(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_next(it->iter);
	if (node != NULL
	    && key_compare(&it->key_data, node, it->index) == 0) {
		return [it->index unfold: node];
	}

	return NULL;
}

static void
tree_iterator_free(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);
	struct tree_iterator *it = tree_iterator(iterator);

	if (it->iter)
		sptree_index_iterator_free(it->iter);

	sfree(it);
}

/* }}} */

/* {{{ TreeIndex -- base tree index class *************************/

@implementation TreeIndex

@class SparseTreeIndex;
@class DenseTreeIndex;
@class Num32TreeIndex;
@class FixedTreeIndex;

+ (Index *) alloc: (struct key_def *) key_def
		 : (struct space *) space
{
	enum tree_type type = find_tree_type(space, key_def);
	switch (type) {
	case TREE_SPARSE:
		return [SparseTreeIndex alloc];
	case TREE_DENSE:
		return [DenseTreeIndex alloc];
	case TREE_NUM32:
		return [Num32TreeIndex alloc];
	case TREE_FIXED:
		return [FixedTreeIndex alloc];
	}
	panic("bad tree index type");
}

- (void) free
{
	sptree_index_destroy(&tree);
	[super free];
}

- (void) enable
{
	enabled = false;
	memset(&tree, 0, sizeof tree);
	if (n == 0) /* pk */ {
		sptree_index_init(&tree,
				  [self node_size], NULL, 0, 0,
				  key_compare, node_compare,
				  self);
		enabled = true;
	}
}

- (size_t) size
{
	return tree.size;
}

- (struct box_tuple *) min
{
	void *node = sptree_index_first(&tree);
	return [self unfold: node];
}

- (struct box_tuple *) max
{
	void *node = sptree_index_last(&tree);
	return [self unfold: node];
}

- (struct box_tuple *) find: (void *) key
{
	struct key_data *key_data
		= alloca(sizeof(struct key_data) + _SIZEOF_SPARSE_PARTS(1));

	key_data->data = key;
	key_data->part_count = 1;
	fold_key_parts(&key_def, key_data);

	void *node = sptree_index_find(&tree, key_data);
	return [self unfold: node];
}

- (struct box_tuple *) findByTuple: (struct box_tuple *) tuple
{
	struct key_data *key_data
		= alloca(sizeof(struct key_data) + _SIZEOF_SPARSE_PARTS(tuple->cardinality));

	key_data->data = tuple->data;
	key_data->part_count = tuple->cardinality;
	fold_sparse_parts(&key_def, tuple, key_data->parts);

	void *node = sptree_index_find(&tree, key_data);
	return [self unfold: node];
}

- (void) remove: (struct box_tuple *) tuple
{
	struct key_data *key_data
		= alloca(sizeof(struct key_data) + _SIZEOF_SPARSE_PARTS(tuple->cardinality));

	key_data->data = tuple->data;
	key_data->part_count = tuple->cardinality;
	fold_sparse_parts(&key_def, tuple, key_data->parts);

	sptree_index_delete(&tree, key_data);
}

- (void) replace: (struct box_tuple *) old_tuple
		: (struct box_tuple *) new_tuple
{
	if (new_tuple->cardinality < key_def.max_fieldno)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD, key_def.max_fieldno);

	if (old_tuple) {
		[self remove: old_tuple];
	}

	void *node = alloca([self node_size]);
	[self fold: node :new_tuple];
	sptree_index_insert(&tree, node);
}

- (struct iterator *) allocIterator
{
	struct tree_iterator *it
		= salloc(sizeof(struct tree_iterator) + SIZEOF_SPARSE_PARTS(&key_def));

	if (it) {
		memset(it, 0, sizeof(struct tree_iterator));
		it->index = self;
		it->base.next = tree_iterator_next;
		it->base.free = tree_iterator_free;
	}
	return (struct iterator *) it;
}

- (void) initIterator: (struct iterator *) iterator
{
	[ self initIterator: iterator :NULL :0 ];
}

- (void) initIterator: (struct iterator *) iterator
		     : (void *) key
		     : (int) part_count
{
	assert(iterator->next == tree_iterator_next);
	struct tree_iterator *it = tree_iterator(iterator);

	if (key_def.is_unique && part_count == key_def.part_count)
		it->base.next_equal = iterator_first_equal;
	else
		it->base.next_equal = tree_iterator_next_equal;

	it->key_data.data = key;
	it->key_data.part_count = part_count;
	fold_key_parts(&key_def, &it->key_data);
	sptree_index_iterator_init_set(&tree, &it->iter, &it->key_data);
}

- (void) build: (Index *) pk
{
	u32 n_tuples = [pk size];
	u32 estimated_tuples = n_tuples * 1.2;
	int node_size = [self node_size];

	assert(enabled == false);

	void *nodes = NULL;
	if (n_tuples) {
		/*
		 * Allocate a little extra to avoid
		 * unnecessary realloc() when more data is
		 * inserted.
		*/
		size_t sz = estimated_tuples * node_size;
		nodes = malloc(sz);
		if (nodes == NULL) {
			panic("malloc(): failed to allocate %"PRI_SZ" bytes", sz);
		}
	}

	struct iterator *it = pk->position;
	[pk initIterator: it];

	struct box_tuple *tuple;
	for (u32 i = 0; (tuple = it->next(it)) != NULL; ++i) {
		void *node = ((u8 *) nodes + i * node_size);
		[self fold: node :tuple];
	}

	if (n_tuples) {
		say_info("Sorting %"PRIu32 " keys in index %" PRIu32 "...", n_tuples, self->n);
	}

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_index_init(&tree,
			  node_size, nodes, n_tuples, estimated_tuples,
			  key_compare,
			  key_def.is_unique ? node_compare : node_compare_with_dups,
			  self);

	/* Done with it */
	enabled = true;
}

- (size_t) node_size
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	(void) node;
	(void) tuple;
	[self subclassResponsibility: _cmd];
}

- (struct box_tuple *) unfold: (const void *) node
{
	(void) node;
	[self subclassResponsibility: _cmd];
	return NULL;
}

- (int) compare: (const void *) node_a :(const void *) node_b
{
	(void) node_a;
	(void) node_b;
	[self subclassResponsibility: _cmd];
	return 0;
}

- (int) key_compare: (const void *) key :(const void *) node
{
	(void) key;
	(void) node;
	[self subclassResponsibility: _cmd];
	return 0;
}

@end

/* }}} */

/* {{{ SparseTreeIndex ********************************************/

@interface SparseTreeIndex: TreeIndex
@end

@implementation SparseTreeIndex

- (size_t) node_size
{
	return sizeof(struct sparse_node) + SIZEOF_SPARSE_PARTS(&key_def);
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct sparse_node *node_x = node;
	node_x->tuple = tuple;
	fold_sparse_parts(&key_def, tuple, node_x->parts);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct sparse_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

- (int) compare: (const void *) node_a :(const void *) node_b
{
	const struct sparse_node *node_xa = node_a;
	const struct sparse_node *node_xb = node_b;
	return sparse_node_compare(
		&key_def,
		node_xa->tuple, node_xa->parts,
		node_xb->tuple, node_xb->parts);
}

- (int) key_compare: (const void *) key :(const void *) node
{
	const struct key_data *key_data = key;
	const struct sparse_node *node_x = node;
	return sparse_key_node_compare(
		&key_def, key_data,
		node_x->tuple, node_x->parts);
}

@end

/* }}} */

/* {{{ DenseTreeIndex *********************************************/

@interface DenseTreeIndex: TreeIndex {
	u32 first_field;
}
@end

@implementation DenseTreeIndex

- (void) enable
{
	[super enable];
	first_field = find_first_field(&key_def);
}

- (size_t) node_size
{
	return sizeof(struct dense_node);
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct dense_node *node_x = node;
	node_x->tuple = tuple;
	node_x->offset = fold_dense_offset(&key_def, tuple);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct dense_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

- (int) compare: (const void *) node_a :(const void *) node_b
{
	const struct dense_node *node_xa = node_a;
	const struct dense_node *node_xb = node_b;
	return dense_node_compare(
		&key_def, first_field,
		node_xa->tuple, node_xa->offset,
		node_xb->tuple, node_xb->offset);
}

- (int) key_compare: (const void *) key :(const void *) node
{
	const struct key_data *key_data = key;
	const struct dense_node *node_x = node;
	return dense_key_node_compare(
		&key_def, key_data, first_field,
		node_x->tuple, node_x->offset);
}

@end

/* }}} */

/* {{{ Num32TreeIndex *********************************************/

@interface Num32TreeIndex: TreeIndex
@end

@implementation Num32TreeIndex

- (size_t) node_size
{
	return sizeof(struct num32_node);
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct num32_node *node_x = (struct num32_node *) node;
	node_x->tuple = tuple;
	node_x->value = fold_num32_value(&key_def, tuple);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct num32_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

- (int) compare: (const void *) node_a :(const void *) node_b
{
	const struct num32_node *node_xa = node_a;
	const struct num32_node *node_xb = node_b;
	return ((i64) (u64) node_xa->value - (i64) (u64) node_xb->value) >> 32;
}

- (int) key_compare: (const void *) key :(const void *) node
{
	const struct key_data *key_data = key;
	const struct num32_node *node_x = node;
	return ((i64) (u64) key_data->parts[0].num32 - (i64) (u64) node_x->value) >> 32;
}

@end

/* }}} */

/* {{{ FixedTreeIndex *********************************************/

@interface FixedTreeIndex: TreeIndex {
	u32 first_field;
	u32 first_offset;
}
@end

@implementation FixedTreeIndex

- (void) enable
{
	[super enable];
	first_field = find_first_field(&key_def);
	first_offset = find_fixed_offset(space, first_field, 0);
}

- (size_t) node_size
{
	return sizeof(struct fixed_node);
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct fixed_node *node_x = (struct fixed_node *) node;
	node_x->tuple = tuple;
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct fixed_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

- (int) compare: (const void *) node_a :(const void *) node_b
{
	const struct fixed_node *node_xa = node_a;
	const struct fixed_node *node_xb = node_b;
	return dense_node_compare(
		&key_def, first_field,
		node_xa->tuple, first_offset,
		node_xb->tuple, first_offset);
}

- (int) key_compare: (const void *) key :(const void *) node
{
	const struct key_data *key_data = key;
	const struct fixed_node *node_x = node;
	return dense_key_node_compare(
		&key_def, key_data, first_field,
		node_x->tuple, first_offset);
}

@end

/* }}} */
