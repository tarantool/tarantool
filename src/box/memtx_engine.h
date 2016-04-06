#ifndef TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED
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
#include "engine.h"

enum memtx_recovery_state {
	MEMTX_INITIALIZED,
	MEMTX_READING_SNAPSHOT,
	MEMTX_READING_WAL,
	MEMTX_OK,
};

/** Memtx extents pool, available to statistics. */
extern struct mempool memtx_index_extent_pool;

struct MemtxEngine: public Engine {
	MemtxEngine();
	virtual Handler *open() override;
	virtual Index *createIndex(struct key_def *key_def) override;
	virtual void addPrimaryKey(struct space *space) override;
	virtual void dropIndex(Index *index) override;
	virtual void dropPrimaryKey(struct space *space) override;
	virtual bool needToBuildSecondaryKey(struct space *space) override;
	virtual void keydefCheck(struct space *space,
				 struct key_def *key_def) override;
	virtual void begin(struct txn *txn) override;
	virtual void rollbackStatement(struct txn_stmt *stmt) override;
	virtual void rollback(struct txn *txn) override;
	virtual void prepare(struct txn *txn) override;
	virtual void commit(struct txn *txn, int64_t signature) override;
	virtual void beginJoin() override;
	virtual void recoverToCheckpoint(int64_t lsn) override;
	virtual void endRecovery() override;
	virtual void join(struct relay *relay) override;
	virtual int beginCheckpoint(int64_t) override;
	virtual int waitCheckpoint() override;
	virtual void commitCheckpoint() override;
	virtual void abortCheckpoint() override;
	virtual void initSystemSpace(struct space *space) override;
private:
	/** Non-zero if there is a checkpoint (snapshot) in * progress. */
	struct checkpoint *m_checkpoint;
	enum memtx_recovery_state m_state;
};

enum {
	MEMTX_EXTENT_SIZE = 16 * 1024,
	MEMTX_SLAB_SIZE = 4 * 1024 * 1024
};

/**
 * Initialize arena for indexes.
 * The arena is used for memtx_index_extent_alloc
 *  and memtx_index_extent_free.
 * Can be called several times, only first call do the work.
 */
void
memtx_index_arena_init();

/**
 * Allocate a block of size MEMTX_EXTENT_SIZE for memtx index
 */
void *
memtx_index_extent_alloc();

/**
 * Free a block previously allocated by memtx_index_extent_alloc
 */
void
memtx_index_extent_free(void *extent);

/**
 * Reserve num extents in pool.
 * Ensure that next num extent_alloc will succeed w/o an error
 */
void
memtx_index_extent_reserve(int num);

#endif /* TARANTOOL_BOX_MEMTX_ENGINE_H_INCLUDED */
