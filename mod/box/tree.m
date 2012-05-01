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
#include <pickle.h>

/* {{{ Utilities. *************************************************/

/**
 * Unsigned 32-bit int comparison.
 */
static inline int
u32_cmp(u32 a, u32 b)
{
	return a < b ? -1 : (a > b);
}

/**
 * Unsigned 64-bit int comparison.
 */
static inline int
u64_cmp(u64 a, u64 b)
{
	return a < b ? -1 : (a > b);
}

/**
 * Tuple addrress comparison.
 */
static inline int
ta_cmp(struct box_tuple *tuple_a, struct box_tuple *tuple_b)
{
	if (!tuple_a)
		return 0;
	if (!tuple_b)
		return 0;
	return tuple_a < tuple_b ? -1 : (tuple_a > tuple_b);
}

/* }}} */

/* {{{ Tree internal data types. **********************************/

/**
 * Tree types
 *
 * There are four specialized kinds of tree indexes optimized for different
 * combinations of index fields.
 *
 * In the most general case tuples consist of variable length fields and the
 * index uses a sparsely distributed subset of these fields. So to determine
 * the field location in the tuple it is required to scan all the preceding
 * fields. To avoid such scans on each access to a tree node we use the SPARSE
 * tree index structure. It pre-computes on the per-tuple basis the required
 * field offsets (or immediate field values for NUMs) and stores them in the
 * corresponding tree node.
 *
 * In case the index fields form a dense sequence it is possible to find
 * each successive field location based on the previous field location. So it
 * is only required to find the offset of the first index field in the tuple
 * and store it in the tree node. In this case we use the DENSE tree index
 * structure.
 *
 * In case the index consists of only one small field it is cheaper to
 * store the field value immediately in the node rather than store the field
 * offset. In this case we use the NUM32 tree index structure.
 *
 * In case the first field offset in a dense sequence is constant there is no
 * need to store any extra data in the node. For instance, the first index
 * field may be the first field in the tuple so the offset is always zero or
 * all the preceding fields may be fixed-size NUMs so the offset is a non-zero
 * constant. In this case we use the FIXED tree structure.
 *
 * Note that there may be fields with unknown types. In particular, if a field
 * is not used by any index then it doesn't have to be typed. So in many cases
 * we cannot actually determine if the fields preceding to the index are fixed
 * size or not. Therefore we may miss the opportunity to use this optimization
 * in such cases.
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
 * Representation of data for key search. The data corresponds to some
 * struct key_def. The part_count field from struct key_data may be less
 * than or equal to the part_count field from the struct key_def. Thus
 * the search data may be partially specified.
 *
 * For simplicity sake the key search data uses sparse_part internally
 * regardless of the target kind of tree because there is little benefit
 * of having the most compact representation of transient search data.
 */
struct key_data
{
	u8 *data;
	int part_count;
	union sparse_part parts[];
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
 * Find the first index field.
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
 * Find the appropriate tree type for a given key.
 */
static enum tree_type
find_tree_type(struct space *space, struct key_def *key_def)
{
	int dense = 1;
	int fixed = 1;

	/* Scan for the first tuple field used by the index */
	int field = find_first_field(key_def);
	if (find_fixed_offset(space, field, 0) < 0) {
		fixed = 0;
	}

	/* Check that there are no gaps after the first field */
	for (; field < key_def->max_fieldno; ++field) {
		int part = key_def->cmp_order[field];
		if (part == -1) {
			dense = 0;
			break;
		}
	}

	/* Return the appropriate type */
	if (!dense) {
		return TREE_SPARSE;
	} else if (fixed) {
		return TREE_FIXED;
	} else if (key_def->part_count == 1 && key_def->parts[0].type == NUM) {
		return TREE_NUM32;
	} else {
		return TREE_DENSE;
	}
}

/**
 * Check if key parts make a linear sequence of fields.
 */
static bool
key_is_linear(struct key_def *key_def)
{
	if (key_def->part_count > 1) {
		int prev = key_def->parts[0].fieldno;
		for (int i = 1; i < key_def->part_count; ++i) {
			int next = key_def->parts[i].fieldno;
			if (next != (prev + 1)) {
				return false;
			}
			prev = next;
		}
	}
	return true;
}

/**
 * Find field offsets/values for a sparse node.
 */
static void
fold_with_sparse_parts(struct key_def *key_def, struct box_tuple *tuple, union sparse_part* parts)
{
	u8 *part_data = tuple->data;

	memset(parts, 0, sizeof(parts[0]) * key_def->part_count);

	for (int field = 0; field < key_def->max_fieldno; ++field) {
		assert(field < tuple->cardinality);

		u8 *data = part_data;
		u32 len = load_varint32((void**) &data);

		int part = key_def->cmp_order[field];
		if (part != -1) {
			if (key_def->parts[part].type == NUM) {
				if (len != sizeof parts[part].num32) {
					tnt_raise(IllegalParams, :"key is not u32");
				}
				memcpy(&parts[part].num32, data, len);
			} else if (key_def->parts[part].type == NUM64) {
				if (len != sizeof parts[part].num64) {
					tnt_raise(IllegalParams, :"key is not u64");
				}
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
 * Find field offsets/values for a key.
 */
static void
fold_with_key_parts(struct key_def *key_def, struct key_data *key_data)
{
	u8 *part_data = key_data->data;
	union sparse_part* parts = key_data->parts;

	memset(parts, 0, sizeof(parts[0]) * key_data->part_count);

	int part_count = MIN(key_def->part_count, key_data->part_count);
	for (int part = 0; part < part_count; ++part) {
		u8 *data = part_data;
		u32 len = load_varint32((void**) &data);

		if (key_def->parts[part].type == NUM) {
			if (len != sizeof parts[part].num32)
				tnt_raise(IllegalParams, :"key is not u32");
			memcpy(&parts[part].num32, data, len);
		} else if (key_def->parts[part].type == NUM64) {
			if (len != sizeof parts[part].num64)
				tnt_raise(IllegalParams, :"key is not u64");
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
 * Find the offset for a dense node.
 */
static u32
fold_with_dense_offset(struct key_def *key_def, struct box_tuple *tuple)
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
 * Find the value for a num32 node.
 */
static u32
fold_with_num32_value(struct key_def *key_def, struct box_tuple *tuple)
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
 * Compare a part for two keys.
 */
static int
sparse_part_compare(enum field_data_type type,
		    const u8 *data_a, union sparse_part part_a,
		    const u8 *data_b, union sparse_part part_b)
{
	if (type == NUM) {
		return u32_cmp(part_a.num32, part_b.num32);
	} else if (type == NUM64) {
		return u64_cmp(part_a.num64, part_b.num64);
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
			cmp = (int) al - (int) bl;
		}

		return cmp;
	}
}

/**
 * Compare a key for two sparse nodes.
 */
static int
sparse_node_compare(struct key_def *key_def,
		    struct box_tuple *tuple_a,
		    const union sparse_part* parts_a,
		    struct box_tuple *tuple_b,
		    const union sparse_part* parts_b)
{
	for (int part = 0; part < key_def->part_count; ++part) {
		int r = sparse_part_compare(key_def->parts[part].type,
					    tuple_a->data, parts_a[part],
					    tuple_b->data, parts_b[part]);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare a key for a key search data and a sparse node.
 */
static int
sparse_key_node_compare(struct key_def *key_def,
			const struct key_data *key_data,
			struct box_tuple *tuple,
			const union sparse_part* parts)
{
	int part_count = MIN(key_def->part_count, key_data->part_count);
	for (int part = 0; part < part_count; ++part) {
		int r = sparse_part_compare(key_def->parts[part].type,
					    key_data->data,
					    key_data->parts[part],
					    tuple->data, parts[part]);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare a part for two dense keys.
 */
static int
dense_part_compare(enum field_data_type type,
		   const u8 *ad, u32 al,
		   const u8 *bd, u32 bl)
{
	if (type == NUM) {
		u32 an, bn;
		assert(al == sizeof an && bl == sizeof bn);
		memcpy(&an, ad, sizeof an);
		memcpy(&bn, bd, sizeof bn);
		return u32_cmp(an, bn);
	} else if (type == NUM64) {
		u64 an, bn;
		assert(al == sizeof an && bl == sizeof bn);
		memcpy(&an, ad, sizeof an);
		memcpy(&bn, bd, sizeof bn);
		return u64_cmp(an, bn);
	} else {
		int cmp = memcmp(ad, bd, MIN(al, bl));
		if (cmp == 0) {
			cmp = (int) al - (int) bl;
		}
		return cmp;
	}
}

/**
 * Compare a key for two dense nodes.
 */
static int
dense_node_compare(struct key_def *key_def, u32 first_field,
		   struct box_tuple *tuple_a, u32 offset_a,
		   struct box_tuple *tuple_b, u32 offset_b)
{
	int part_count = key_def->part_count;
	assert(first_field + part_count <= tuple_a->cardinality);
	assert(first_field + part_count <= tuple_b->cardinality);

	/* Allocate space for offsets. */
	u32 *off_a = alloca(2 * part_count * sizeof(u32));
	u32 *off_b = off_a + part_count;

	/* Find field offsets. */
	off_a[0] = offset_a;
	off_b[0] = offset_b;
	if (part_count > 1) {
		u8 *ad = tuple_a->data + offset_a;
		u8 *bd = tuple_b->data + offset_b;
		for (int i = 1; i < part_count; ++i) {
			u32 al = load_varint32((void**) &ad);
			u32 bl = load_varint32((void**) &bd);
			ad += al;
			bd += bl;
			off_a[i] = ad - tuple_a->data;
			off_b[i] = bd - tuple_b->data;
		}
	}

	/* Compare key parts. */
	for (int part = 0; part < part_count; ++part) {
		int field = key_def->parts[part].fieldno;
		u8 *ad = tuple_a->data + off_a[field - first_field];
		u8 *bd = tuple_b->data + off_b[field - first_field];
		u32 al = load_varint32((void *) &ad);
		u32 bl = load_varint32((void *) &bd);
		int r = dense_part_compare(key_def->parts[part].type,
					   ad, al, bd, bl);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare a part for two dense keys with parts in linear order.
 */
static int
linear_node_compare(struct key_def *key_def,
		    u32 first_field  __attribute__((unused)),
		    struct box_tuple *tuple_a, u32 offset_a,
		    struct box_tuple *tuple_b, u32 offset_b)
{
	int part_count = key_def->part_count;
	assert(first_field + part_count <= tuple_a->cardinality);
	assert(first_field + part_count <= tuple_b->cardinality);

	/* Compare key parts. */
	u8 *ad = tuple_a->data + offset_a;
	u8 *bd = tuple_b->data + offset_b;
	for (int part = 0; part < part_count; ++part) {
		u32 al = load_varint32((void**) &ad);
		u32 bl = load_varint32((void**) &bd);
		int r = dense_part_compare(key_def->parts[part].type,
					   ad, al, bd, bl);
		if (r) {
			return r;
		}
		ad += al;
		bd += bl;
	}
	return 0;
}

/**
 * Compare a part for a key search data and a dense key.
 */
static int
dense_key_part_compare(enum field_data_type type,
		       const u8 *data_a, union sparse_part part_a,
		       const u8 *bd, u32 bl)
{
	if (type == NUM) {
		u32 an, bn;
		an = part_a.num32;
		assert(bl == sizeof bn);
		memcpy(&bn, bd, sizeof bn);
		return u32_cmp(an, bn);
	} else if (type == NUM64) {
		u64 an, bn;
		an = part_a.num64;
		assert(bl == sizeof bn);
		memcpy(&bn, bd, sizeof bn);
		return u64_cmp(an, bn);
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
			cmp = (int) al - (int) bl;
		}

		return cmp;
	}
}

/**
 * Compare a key for a key search data and a dense node.
 */
static int
dense_key_node_compare(struct key_def *key_def,
		       const struct key_data *key_data,
		       u32 first_field, struct box_tuple *tuple, u32 offset)
{
	int part_count = key_def->part_count;
	assert(first_field + part_count <= tuple->cardinality);

	/* Allocate space for offsets. */
	u32 *off = alloca(part_count * sizeof(u32));

	/* Find field offsets. */
	off[0] = offset;
	if (part_count > 1) {
		u8 *data = tuple->data + offset;
		for (int i = 1; i < part_count; ++i) {
			u32 len = load_varint32((void**) &data);
			data += len;
			off[i] = data - tuple->data;
		}
	}

	/* Compare key parts. */
	if (part_count > key_data->part_count)
		part_count = key_data->part_count;
	for (int part = 0; part < part_count; ++part) {
		int field = key_def->parts[part].fieldno;
		const u8 *bd = tuple->data + off[field - first_field];
		u32 bl = load_varint32((void *) &bd);
		int r = dense_key_part_compare(key_def->parts[part].type,
					       key_data->data,
					       key_data->parts[part],
					       bd, bl);
		if (r) {
			return r;
		}
	}
	return 0;
}

/**
 * Compare a key for a key search data and a dense node with parts in
 * linear order.
 */
static int
linear_key_node_compare(struct key_def *key_def,
			const struct key_data *key_data,
			u32 first_field __attribute__((unused)),
			struct box_tuple *tuple, u32 offset)
{
	int part_count = key_def->part_count;
	assert(first_field + part_count <= tuple->cardinality);

	/* Compare key parts. */
	if (part_count > key_data->part_count)
		part_count = key_data->part_count;
	u8 *bd = tuple->data + offset;
	for (int part = 0; part < part_count; ++part) {
		u32 bl = load_varint32((void *) &bd);
		int r = dense_key_part_compare(key_def->parts[part].type,
					       key_data->data,
					       key_data->parts[part],
					       bd, bl);
		if (r) {
			return r;
		}
		bd += bl;
	}
	return 0;
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
tree_iterator_reverse_next(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_reverse_next);
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_reverse_next(it->iter);
	return [it->index unfold: node];
}

static struct box_tuple *
tree_iterator_next_equal(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_next);
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_next(it->iter);
	if (node != NULL
	    && it->index->tree.compare(&it->key_data, node, it->index) == 0) {
		return [it->index unfold: node];
	}

	return NULL;
}

static struct box_tuple *
tree_iterator_reverse_next_equal(struct iterator *iterator)
{
	assert(iterator->next == tree_iterator_reverse_next);
	struct tree_iterator *it = tree_iterator(iterator);

	void *node = sptree_index_iterator_reverse_next(it->iter);
	if (node != NULL
	    && it->index->tree.compare(&it->key_data, node, it->index) == 0) {
		return [it->index unfold: node];
	}

	return NULL;
}

static void
tree_iterator_free(struct iterator *iterator)
{
	assert(iterator->free == tree_iterator_free);
	struct tree_iterator *it = tree_iterator(iterator);
	if (it->iter)
		sptree_index_iterator_free(it->iter);

	free(it);
}

/* }}} */

/* {{{ TreeIndex -- base tree index class *************************/

@implementation TreeIndex

@class SparseTreeIndex;
@class DenseTreeIndex;
@class Num32TreeIndex;
@class FixedTreeIndex;

+ (Index *) alloc: (struct key_def *) key_def :(struct space *) space
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
	panic("tree index type not implemented");
}

- (void) free
{
	sptree_index_destroy(&tree);
	[super free];
}

- (void) enable
{
	memset(&tree, 0, sizeof tree);
	if (index_is_primary(self)) {
		sptree_index_init(&tree,
				  [self node_size], NULL, 0, 0,
				  [self key_node_cmp], [self node_cmp],
				  self);
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

- (struct box_tuple *) find: (void *) key : (int) key_cardinality
{
	struct key_data *key_data
		= alloca(sizeof(struct key_data) +
			 _SIZEOF_SPARSE_PARTS(key_cardinality));

	if (key_cardinality > key_def->part_count)
		tnt_raise(ClientError, :ER_KEY_CARDINALITY,
			  key_cardinality, key_def->part_count);

	if (key_cardinality < key_def->part_count)
		tnt_raise(ClientError, :ER_EXACT_MATCH,
			  key_cardinality, key_def->part_count);

	key_data->data = key;
	key_data->part_count = key_cardinality;
	fold_with_key_parts(key_def, key_data);

	void *node = sptree_index_find(&tree, key_data);
	return [self unfold: node];
}

- (struct box_tuple *) findByTuple: (struct box_tuple *) tuple
{
	struct key_data *key_data
		= alloca(sizeof(struct key_data) + _SIZEOF_SPARSE_PARTS(tuple->cardinality));

	key_data->data = tuple->data;
	key_data->part_count = tuple->cardinality;
	fold_with_sparse_parts(key_def, tuple, key_data->parts);

	void *node = sptree_index_find(&tree, key_data);
	return [self unfold: node];
}

- (void) remove: (struct box_tuple *) tuple
{
	void *node = alloca([self node_size]);
	[self fold: node :tuple];
	sptree_index_delete(&tree, node);
}

- (void) replace: (struct box_tuple *) old_tuple
		: (struct box_tuple *) new_tuple
{
	if (new_tuple->cardinality < key_def->max_fieldno)
		tnt_raise(ClientError, :ER_NO_SUCH_FIELD,
			  key_def->max_fieldno);

	void *node = alloca([self node_size]);
	if (old_tuple) {
		[self fold: node :old_tuple];
		sptree_index_delete(&tree, node);
	}
	[self fold: node :new_tuple];
	sptree_index_insert(&tree, node);
}

- (struct iterator *) allocIterator
{
	assert(key_def->part_count);
	struct tree_iterator *it
		= malloc(sizeof(struct tree_iterator) + SIZEOF_SPARSE_PARTS(key_def));

	if (it) {
		memset(it, 0, sizeof(struct tree_iterator));
		it->index = self;
		it->base.free = tree_iterator_free;
	}
	return (struct iterator *) it;
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
{
	[self initIterator: iterator :type :NULL :0];
}

- (void) initIterator: (struct iterator *) iterator :(enum iterator_type) type
                        :(void *) key
			:(int) key_cardinality
{
	assert(iterator->free == tree_iterator_free);
	struct tree_iterator *it = tree_iterator(iterator);

	if (key_cardinality > key_def->part_count)
		tnt_raise(ClientError, :ER_KEY_CARDINALITY,
			  key_cardinality, key_def->part_count);

	it->key_data.data = key;
	it->key_data.part_count = key_cardinality;
	fold_with_key_parts(key_def, &it->key_data);

	if (type == ITER_FORWARD) {
		it->base.next = tree_iterator_next;
		it->base.next_equal = tree_iterator_next_equal;
		sptree_index_iterator_init_set(&tree, &it->iter, &it->key_data);
	} else if (type == ITER_REVERSE) {
		it->base.next = tree_iterator_reverse_next;
		it->base.next_equal = tree_iterator_reverse_next_equal;
		sptree_index_iterator_reverse_init_set(&tree, &it->iter, &it->key_data);
	}
}

- (void) build: (Index *) pk
{
	u32 n_tuples = [pk size];
	u32 estimated_tuples = n_tuples * 1.2;
	int node_size = [self node_size];

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
	[pk initIterator: it :ITER_FORWARD];

	struct box_tuple *tuple;
	for (u32 i = 0; (tuple = it->next(it)) != NULL; ++i) {
		void *node = ((u8 *) nodes + i * node_size);
		[self fold: node :tuple];
	}

	if (n_tuples) {
		say_info("Sorting %"PRIu32 " keys in index %" PRIu32 "...", n_tuples,
			 index_n(self));
	}

	/* If n_tuples == 0 then estimated_tuples = 0, elem == NULL, tree is empty */
	sptree_index_init(&tree,
			  node_size, nodes, n_tuples, estimated_tuples,
			  [self key_node_cmp],
			  key_def->is_unique ? [self node_cmp] : [self dup_node_cmp],
			  self);
}

- (size_t) node_size
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (tree_cmp_t) node_cmp
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (tree_cmp_t) dup_node_cmp
{
	[self subclassResponsibility: _cmd];
	return 0;
}

- (tree_cmp_t) key_node_cmp
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

@end

/* }}} */

/* {{{ SparseTreeIndex ********************************************/

@interface SparseTreeIndex: TreeIndex
@end

static int
sparse_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	SparseTreeIndex *index = (SparseTreeIndex *) arg;
	const struct sparse_node *node_xa = node_a;
	const struct sparse_node *node_xb = node_b;
	return sparse_node_compare(index->key_def,
				   node_xa->tuple, node_xa->parts,
				   node_xb->tuple, node_xb->parts);
}

static int
sparse_dup_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	int r = sparse_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct sparse_node *node_xa = node_a;
		const struct sparse_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
sparse_key_node_cmp(const void *key, const void *node, void *arg)
{
	SparseTreeIndex *index = (SparseTreeIndex *) arg;
	const struct key_data *key_data = key;
	const struct sparse_node *node_x = node;
	return sparse_key_node_compare(index->key_def, key_data,
				       node_x->tuple, node_x->parts);
}

@implementation SparseTreeIndex

- (size_t) node_size
{
	return sizeof(struct sparse_node) + SIZEOF_SPARSE_PARTS(key_def);
}

- (tree_cmp_t) node_cmp
{
	return sparse_node_cmp;
}

- (tree_cmp_t) dup_node_cmp
{
	return sparse_dup_node_cmp;
}

- (tree_cmp_t) key_node_cmp
{
	return sparse_key_node_cmp;
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct sparse_node *node_x = node;
	node_x->tuple = tuple;
	fold_with_sparse_parts(key_def, tuple, node_x->parts);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct sparse_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

@end

/* }}} */

/* {{{ DenseTreeIndex *********************************************/

@interface DenseTreeIndex: TreeIndex {
	@public
	u32 first_field;
	bool is_linear;
}
@end

static int
dense_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	DenseTreeIndex *index = (DenseTreeIndex *) arg;
	const struct dense_node *node_xa = node_a;
	const struct dense_node *node_xb = node_b;
	return dense_node_compare(index->key_def, index->first_field,
				  node_xa->tuple, node_xa->offset,
				  node_xb->tuple, node_xb->offset);
}

static int
dense_dup_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	int r = dense_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct dense_node *node_xa = node_a;
		const struct dense_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
dense_key_node_cmp(const void *key, const void * node, void *arg)
{
	DenseTreeIndex *index = (DenseTreeIndex *) arg;
	const struct key_data *key_data = key;
	const struct dense_node *node_x = node;
	return dense_key_node_compare(index->key_def, key_data,
				      index->first_field,
				      node_x->tuple, node_x->offset);
}

static int
linear_dense_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	DenseTreeIndex *index = (DenseTreeIndex *) arg;
	const struct dense_node *node_xa = node_a;
	const struct dense_node *node_xb = node_b;
	return linear_node_compare(index->key_def, index->first_field,
				   node_xa->tuple, node_xa->offset,
				   node_xb->tuple, node_xb->offset);
}

static int
linear_dense_dup_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	int r = linear_dense_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct dense_node *node_xa = node_a;
		const struct dense_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
linear_dense_key_node_cmp(const void *key, const void * node, void *arg)
{
	DenseTreeIndex *index = (DenseTreeIndex *) arg;
	const struct key_data *key_data = key;
	const struct dense_node *node_x = node;
	return linear_key_node_compare(index->key_def, key_data,
				       index->first_field,
				       node_x->tuple, node_x->offset);
}

@implementation DenseTreeIndex

- (void) enable
{
	[super enable];
	first_field = find_first_field(key_def);
	is_linear = key_is_linear(key_def);
}

- (size_t) node_size
{
	return sizeof(struct dense_node);
}

- (tree_cmp_t) node_cmp
{
	return is_linear ? linear_dense_node_cmp : dense_node_cmp;
}

- (tree_cmp_t) dup_node_cmp
{
	return is_linear ? linear_dense_dup_node_cmp : dense_dup_node_cmp;
}

- (tree_cmp_t) key_node_cmp
{
	return is_linear ? linear_dense_key_node_cmp : dense_key_node_cmp;
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct dense_node *node_x = node;
	node_x->tuple = tuple;
	node_x->offset = fold_with_dense_offset(key_def, tuple);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct dense_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

@end

/* }}} */

/* {{{ Num32TreeIndex *********************************************/

@interface Num32TreeIndex: TreeIndex
@end

static int
num32_node_cmp(const void * node_a, const void * node_b, void *arg)
{
	(void) arg;
	const struct num32_node *node_xa = node_a;
	const struct num32_node *node_xb = node_b;
	return u32_cmp(node_xa->value, node_xb->value);
}

static int
num32_dup_node_cmp(const void * node_a, const void * node_b, void *arg)
{
	int r = num32_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct num32_node *node_xa = node_a;
		const struct num32_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
num32_key_node_cmp(const void * key, const void * node, void *arg)
{
	(void) arg;
	const struct key_data *key_data = key;
	const struct num32_node *node_x = node;
	return u32_cmp(key_data->parts[0].num32, node_x->value);
}

@implementation Num32TreeIndex

- (size_t) node_size
{
	return sizeof(struct num32_node);
}

- (tree_cmp_t) node_cmp
{
	return num32_node_cmp;
}

- (tree_cmp_t) dup_node_cmp
{
	return num32_dup_node_cmp;
}

- (tree_cmp_t) key_node_cmp
{
	return num32_key_node_cmp;
}

- (void) fold: (void *) node :(struct box_tuple *) tuple
{
	struct num32_node *node_x = (struct num32_node *) node;
	node_x->tuple = tuple;
	node_x->value = fold_with_num32_value(key_def, tuple);
}

- (struct box_tuple *) unfold: (const void *) node
{
	const struct num32_node *node_x = node;
	return node_x ? node_x->tuple : NULL;
}

@end

/* }}} */

/* {{{ FixedTreeIndex *********************************************/

@interface FixedTreeIndex: TreeIndex {
	@public
	u32 first_field;
	u32 first_offset;
	bool is_linear;
}
@end

static int
fixed_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	FixedTreeIndex *index = (FixedTreeIndex *) arg;
	const struct fixed_node *node_xa = node_a;
	const struct fixed_node *node_xb = node_b;
	return dense_node_compare(index->key_def, index->first_field,
				  node_xa->tuple, index->first_offset,
				  node_xb->tuple, index->first_offset);
}

static int
fixed_dup_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	int r = fixed_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct fixed_node *node_xa = node_a;
		const struct fixed_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
fixed_key_node_cmp(const void *key, const void * node, void *arg)
{
	FixedTreeIndex *index = (FixedTreeIndex *) arg;
	const struct key_data *key_data = key;
	const struct fixed_node *node_x = node;
	return dense_key_node_compare(index->key_def, key_data,
				      index->first_field,
				      node_x->tuple, index->first_offset);
}

static int
linear_fixed_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	FixedTreeIndex *index = (FixedTreeIndex *) arg;
	const struct fixed_node *node_xa = node_a;
	const struct fixed_node *node_xb = node_b;
	return linear_node_compare(index->key_def, index->first_field,
				   node_xa->tuple, index->first_offset,
				   node_xb->tuple, index->first_offset);
}

static int
linear_fixed_dup_node_cmp(const void *node_a, const void *node_b, void *arg)
{
	int r = linear_fixed_node_cmp(node_a, node_b, arg);
	if (r == 0) {
		const struct fixed_node *node_xa = node_a;
		const struct fixed_node *node_xb = node_b;
		r = ta_cmp(node_xa->tuple, node_xb->tuple);
	}
	return r;
}

static int
linear_fixed_key_node_cmp(const void *key, const void * node, void *arg)
{
	FixedTreeIndex *index = (FixedTreeIndex *) arg;
	const struct key_data *key_data = key;
	const struct fixed_node *node_x = node;
	return linear_key_node_compare(index->key_def, key_data,
					 index->first_field,
					 node_x->tuple, index->first_offset);
}

@implementation FixedTreeIndex

- (void) enable
{
	[super enable];
	first_field = find_first_field(key_def);
	first_offset = find_fixed_offset(space, first_field, 0);
	is_linear = key_is_linear(key_def);
}

- (size_t) node_size
{
	return sizeof(struct fixed_node);
}

- (tree_cmp_t) node_cmp
{
	return is_linear ? linear_fixed_node_cmp : fixed_node_cmp;
}

- (tree_cmp_t) dup_node_cmp
{
	return is_linear ? linear_fixed_dup_node_cmp : fixed_dup_node_cmp;
}

- (tree_cmp_t) key_node_cmp
{
	return is_linear ? linear_fixed_key_node_cmp : fixed_key_node_cmp;
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

@end

/* }}} */

