#ifndef TARANTOOL_BOX_MEMTX_INDEX_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_INDEX_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "index.h"

class MemtxIndex: public Index {
public:
	MemtxIndex(struct key_def *key_def)
		:Index(key_def), m_position(NULL)
	{ }
	virtual ~MemtxIndex() override {
		if (m_position != NULL)
			m_position->free(m_position);
	}
	virtual struct tuple *min(const char *key,
				  uint32_t part_count) const override;
	virtual struct tuple *max(const char *key,
				  uint32_t part_count) const override;
	virtual size_t count(enum iterator_type type, const char *key,
			     uint32_t part_count) const override;

	inline struct iterator *position() const
	{
		if (m_position == NULL)
			m_position = allocIterator();
		return m_position;
	}

	/**
	 * Two-phase index creation: begin building, add tuples, finish.
	 */
	virtual void beginBuild();
	/**
	 * Optional hint, given to the index, about
	 * the total size of the index. If given,
	 * is given after beginBuild().
	 */
	virtual void reserve(uint32_t /* size_hint */);
	virtual void buildNext(struct tuple *tuple);
	virtual void endBuild();
protected:
	/*
	 * Pre-allocated iterator to speed up the main case of
	 * box_process(). Should not be used elsewhere.
	 */
	mutable struct iterator *m_position;
};

/** Build this index based on the contents of another index. */
void
index_build(MemtxIndex *index, MemtxIndex *pk);

#endif /* TARANTOOL_BOX_MEMTX_INDEX_H_INCLUDED */
