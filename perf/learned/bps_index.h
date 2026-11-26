/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "learned.h"

#include "small/matras.h"

/** Element of the tree, like struct memtx_tree_data<true>. */
struct bps_elem {
	/** The record. */
	struct rec *rec;
	/** Comparison hint of its key. */
	uint64_t hint;
};

/** Search key, like struct memtx_tree_key_data<true>. */
struct bps_key {
	/** The key. */
	uint64_t key;
	/** Its comparison hint. */
	uint64_t hint;
};

/**
 * Hint of an unsigned key, like hint_uint() in tuple_compare.cc but without
 * the type class, which is the same for all the keys. It is exact below
 * 2^59 - 1, all the larger keys share the maximal hint.
 */
static inline uint64_t
key_hint(uint64_t key)
{
	const uint64_t int_max = (1ULL << 59) - 1;
	return key >= int_max ? (1ULL << 60) - 1 : key + (1ULL << 59);
}

/**
 * Like tuple_compare(): different hints decide, equal hints require reading
 * the records.
 */
static inline int
bps_elem_compare(const struct bps_elem *a, const struct bps_elem *b)
{
	if (a->hint != b->hint)
		return a->hint < b->hint ? -1 : 1;
	return a->rec->key < b->rec->key ? -1 : a->rec->key > b->rec->key;
}

/** Like tuple_compare_with_key(), see bps_elem_compare(). */
static inline int
bps_key_compare(const struct bps_elem *a, const struct bps_key *b)
{
	if (a->hint != b->hint)
		return a->hint < b->hint ? -1 : 1;
	return a->rec->key < b->key ? -1 : a->rec->key > b->key;
}

/** The same settings as in memtx_tree.cc. */
#define BPS_INNER_CARD
#define BPS_TREE_NAME bench_tree
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_COMPARE(a, b, arg) bps_elem_compare(&(a), &(b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) bps_key_compare(&(a), b)
#define BPS_TREE_IS_IDENTICAL(a, b) ((a).rec == (b).rec)
#define BPS_TREE_NO_DEBUG 1
#define bps_tree_elem_t struct bps_elem
#define bps_tree_key_t struct bps_key *
#include "salad/bps_tree.h"
#undef BPS_INNER_CARD
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_NO_DEBUG
#undef bps_tree_elem_t
#undef bps_tree_key_t

/** Size of the tree extents, the same as MEMTX_EXTENT_SIZE. */
static const uint32_t BPS_EXTENT_SIZE = 16 * 1024;

static void *
bps_extent_alloc(struct matras_allocator *)
{
	return xmalloc(BPS_EXTENT_SIZE);
}

static void
bps_extent_free(struct matras_allocator *, void *extent)
{
	free(extent);
}

/** bps_tree with hints set up like the memtx TREE index. */
class BpsIndex {
public:
	BpsIndex()
	{
		matras_allocator_create(&allocator, BPS_EXTENT_SIZE,
					bps_extent_alloc, bps_extent_free);
		bench_tree_create(&tree, 0, &allocator, NULL);
	}

	~BpsIndex()
	{
		bench_tree_destroy(&tree);
		matras_allocator_destroy(&allocator);
	}

	/** Bulk loads records sorted by key into the empty index. */
	void
	build(const std::vector<struct rec *> &recs)
	{
		std::vector<struct bps_elem> elems(recs.size());
		for (size_t i = 0; i < recs.size(); i++)
			elems[i] = make_elem(recs[i]);
		if (bench_tree_build(&tree, elems.data(), elems.size()) != 0)
			die("cannot bulk load the bps tree");
	}

	/** Finds the record by the key, NULL if there is none. */
	struct rec *
	get(uint64_t key)
	{
		struct bps_key k = {key, key_hint(key)};
		struct bps_elem *elem = bench_tree_find(&tree, &k);
		return elem == NULL ? NULL : elem->rec;
	}

	/** Finds the first record with the key >= the given one. */
	struct rec *
	lower_bound(uint64_t key)
	{
		struct bps_key k = {key, key_hint(key)};
		struct bench_tree_iterator it =
			bench_tree_lower_bound(&tree, &k, NULL);
		struct bps_elem *elem =
			bench_tree_iterator_get_elem(&tree, &it);
		return elem == NULL ? NULL : elem->rec;
	}

	/** Hash of at most len records starting from lower_bound(key). */
	uint64_t
	scan(uint64_t key, uint64_t len)
	{
		struct bps_key k = {key, key_hint(key)};
		struct bench_tree_iterator it =
			bench_tree_lower_bound(&tree, &k, NULL);
		uint64_t hash = 0;
		for (uint64_t i = 0; i < len; i++) {
			struct bps_elem *elem =
				bench_tree_iterator_get_elem(&tree, &it);
			if (elem == NULL)
				break;
			hash = scan_hash(hash, elem->rec);
			bench_tree_iterator_next(&tree, &it);
		}
		return hash;
	}

	/** Number of records with keys in [lo, hi), O(log n). */
	uint64_t
	count(uint64_t lo, uint64_t hi)
	{
		return offset(hi) - offset(lo);
	}

	/** Returns false if the key is already there. */
	bool
	insert(struct rec *rec)
	{
		struct bps_elem replaced = {NULL, 0};
		if (bench_tree_insert(&tree, make_elem(rec), &replaced,
				      NULL) != 0)
			die("cannot insert into the bps tree");
		return replaced.rec == NULL;
	}

	/** Returns false if there is no such key. */
	bool
	erase(struct rec *rec)
	{
		return bench_tree_delete(&tree, make_elem(rec), NULL) == 0;
	}

	/** Number of the records. */
	size_t
	size()
	{
		return bench_tree_size(&tree);
	}

	/** Memory used by the index in bytes, without the records. */
	size_t
	mem_used()
	{
		return bench_tree_mem_used(&tree);
	}

private:
	static struct bps_elem
	make_elem(struct rec *rec)
	{
		return {rec, key_hint(rec->key)};
	}

	/** Number of records with keys < the given one. */
	size_t
	offset(uint64_t key)
	{
		struct bps_key k = {key, key_hint(key)};
		size_t offset;
		bench_tree_lower_bound_get_offset(&tree, &k, NULL, &offset);
		return offset;
	}

	/** Allocator of the tree extents. */
	struct matras_allocator allocator;
	/** The tree. */
	struct bench_tree tree;
};
