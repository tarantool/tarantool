/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "alex.h"

#include "learned.h"

/** The default of the library. */
static const int ALEX_DEFAULT_NODE_SIZE = 16 * 1024 * 1024;

/** The library needs a node to hold several keys. */
static const int ALEX_MIN_NODE_SIZE = 1024;

/** ALEX with the interface of BpsIndex. */
class AlexIndex {
public:
	/**
	 * The node size in bytes limits the cost of a node expansion: it is
	 * done by one insert and would block the TX thread.
	 */
	explicit AlexIndex(int max_node_size)
	{
		if (max_node_size < ALEX_MIN_NODE_SIZE)
			die("ALEX node size must be at least %d",
			    ALEX_MIN_NODE_SIZE);
		tree.set_max_node_size(max_node_size);
	}

	void
	build(const std::vector<struct rec *> &recs)
	{
		std::vector<std::pair<uint64_t, struct rec *>> values;
		values.reserve(recs.size());
		for (struct rec *rec : recs)
			values.emplace_back(rec->key, rec);
		tree.bulk_load(values.data(), values.size());
	}

	struct rec *
	get(uint64_t key)
	{
		struct rec **rec = tree.get_payload(key);
		return rec == NULL ? NULL : *rec;
	}

	struct rec *
	lower_bound(uint64_t key)
	{
		auto it = tree.lower_bound(key);
		return it.is_end() ? NULL : it.payload();
	}

	uint64_t
	scan(uint64_t key, uint64_t len)
	{
		uint64_t hash = 0;
		auto it = tree.lower_bound(key);
		for (uint64_t i = 0; i < len && !it.is_end(); i++, ++it)
			hash = scan_hash(hash, it.payload());
		return hash;
	}

	/** O(n) of the range: ALEX does not store subtree sizes. */
	uint64_t
	count(uint64_t lo, uint64_t hi)
	{
		uint64_t count = 0;
		auto it = tree.lower_bound(lo);
		for (; !it.is_end() && it.key() < hi; ++it)
			count++;
		return count;
	}

	bool
	insert(struct rec *rec)
	{
		return tree.insert(rec->key, rec).second;
	}

	bool
	erase(struct rec *rec)
	{
		return tree.erase_one(rec->key) == 1;
	}

	size_t
	size()
	{
		return tree.size();
	}

	size_t
	mem_used()
	{
		return tree.model_size() + tree.data_size();
	}

private:
	/** Unique keys, the payload is a record. */
	alex::Alex<uint64_t, struct rec *, alex::AlexCompare,
		   std::allocator<std::pair<uint64_t, struct rec *>>,
		   false> tree;
};
