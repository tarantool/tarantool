/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <iterator>
#include <map>

#include "learned.h"

/**
 * std::map with the interface of BpsIndex. It is slow but simple, so its
 * results are the reference the other indexes are verified against.
 */
class MapIndex {
public:
	void
	build(const std::vector<struct rec *> &recs)
	{
		for (struct rec *rec : recs)
			map.emplace(rec->key, rec);
	}

	struct rec *
	get(uint64_t key)
	{
		auto it = map.find(key);
		return it == map.end() ? NULL : it->second;
	}

	struct rec *
	lower_bound(uint64_t key)
	{
		auto it = map.lower_bound(key);
		return it == map.end() ? NULL : it->second;
	}

	uint64_t
	scan(uint64_t key, uint64_t len)
	{
		uint64_t hash = 0;
		auto it = map.lower_bound(key);
		for (uint64_t i = 0; i < len && it != map.end(); i++, ++it)
			hash = scan_hash(hash, it->second);
		return hash;
	}

	uint64_t
	count(uint64_t lo, uint64_t hi)
	{
		return std::distance(map.lower_bound(lo), map.lower_bound(hi));
	}

	bool
	insert(struct rec *rec)
	{
		return map.emplace(rec->key, rec).second;
	}

	bool
	erase(struct rec *rec)
	{
		return map.erase(rec->key) == 1;
	}

	size_t
	size()
	{
		return map.size();
	}

private:
	/** The records by key. */
	std::map<uint64_t, struct rec *> map;
};
