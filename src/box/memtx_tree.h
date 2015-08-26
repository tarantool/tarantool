#ifndef TARANTOOL_BOX_TREE_INDEX_H_INCLUDED
#define TARANTOOL_BOX_TREE_INDEX_H_INCLUDED
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

#include "memtx_index.h"
#include "memtx_engine.h"

struct tuple;
struct key_data;

int
tree_index_compare(const struct tuple *a, const struct tuple *b, struct key_def *key_def);

int
tree_index_compare_key(const tuple *a, const key_data *b, struct key_def *key_def);

#define BPS_TREE_NAME _index
#define BPS_TREE_BLOCK_SIZE (512)
#define BPS_TREE_EXTENT_SIZE MEMTX_EXTENT_SIZE
#define BPS_TREE_COMPARE(a, b, arg) tree_index_compare(a, b, arg)
#define BPS_TREE_COMPARE_KEY(a, b, arg) tree_index_compare_key(a, b, arg)
#define bps_tree_elem_t struct tuple *
#define bps_tree_key_t struct key_data *
#define bps_tree_arg_t struct key_def *

#include "salad/bps_tree.h"

class MemtxTree: public MemtxIndex {
public:
	MemtxTree(struct key_def *key_def);
	virtual ~MemtxTree();

	virtual void beginBuild();
	virtual void reserve(uint32_t size_hint);
	virtual void buildNext(struct tuple *tuple);
	virtual void endBuild();
	virtual size_t size() const;
	virtual struct tuple *random(uint32_t rnd) const;
	virtual struct tuple *findByKey(const char *key, uint32_t part_count) const;
	virtual struct tuple *replace(struct tuple *old_tuple,
				      struct tuple *new_tuple,
				      enum dup_replace_mode mode);

	virtual size_t bsize() const;
	virtual struct iterator *allocIterator() const;
	virtual void initIterator(struct iterator *iterator,
				  enum iterator_type type,
				  const char *key, uint32_t part_count) const;

	/**
	 * Create a read view for iterator so further index modifications
	 * will not affect the iterator iteration.
	 */
	virtual void createReadViewForIterator(struct iterator *iterator);
	/**
	 * Destroy a read view of an iterator. Must be called for iterators,
	 * for which createReadViewForIterator was called.
	 */
	virtual void destroyReadViewForIterator(struct iterator *iterator);

// protected:
	struct bps_tree_index tree;
	struct tuple **build_array;
	size_t build_array_size, build_array_alloc_size;
};

#endif /* TARANTOOL_BOX_TREE_INDEX_H_INCLUDED */
