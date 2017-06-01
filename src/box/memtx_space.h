#ifndef TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED
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
#include "engine.h"

typedef void
(*engine_replace_f)(struct txn_stmt *, struct space *, enum dup_replace_mode);

void
memtx_replace_no_keys(struct txn_stmt *, struct space *space,
		      enum dup_replace_mode /* mode */);
void
memtx_replace_build_next(struct txn_stmt *stmt, struct space *space,
			 enum dup_replace_mode mode);
void
memtx_replace_primary_key(struct txn_stmt *, struct space *space,
			  enum dup_replace_mode /* mode */);
void
memtx_replace_all_keys(struct txn_stmt *, struct space *space,
		       enum dup_replace_mode /* mode */);

struct MemtxSpace: public Handler {
	MemtxSpace(Engine *e);
	virtual ~MemtxSpace()
	{
		/* do nothing */
		/* engine->close(this); */
	}
	virtual void
	applyInitialJoinRow(struct space *space,
			    struct request *request) override;
	virtual struct tuple *
	executeReplace(struct txn *txn, struct space *space,
		       struct request *request) override;
	virtual struct tuple *
	executeDelete(struct txn *txn, struct space *space,
		      struct request *request) override;
	virtual struct tuple *
	executeUpdate(struct txn *txn, struct space *space,
		      struct request *request) override;
	virtual void
	executeUpsert(struct txn *txn, struct space *space,
		      struct request *request) override;
	virtual void
	executeSelect(struct txn *, struct space *space,
		      uint32_t index_id, uint32_t iterator,
		      uint32_t offset, uint32_t limit,
		      const char *key, const char * /* key_end */,
		      struct port *port) override;

	virtual Index *createIndex(struct space *space,
				   struct index_def *index_def) override;
	virtual void dropIndex(Index *index) override;
	virtual void prepareAlterSpace(struct space *old_space,
				       struct space *new_space) override;
public:
	/**
	 * A pointer to replace function, set to different values
	 * at different stages of recovery.
	 */
	engine_replace_f replace;
private:
	void
	prepareReplace(struct txn_stmt *stmt, struct space *space,
		       struct request *request);
	void
	prepareDelete(struct txn_stmt *stmt, struct space *space,
		      struct request *request);
	void
	prepareUpdate(struct txn_stmt *stmt, struct space *space,
		      struct request *request);
	void
	prepareUpsert(struct txn_stmt *stmt, struct space *space,
		      struct request *request);
};

#endif /* TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED */
