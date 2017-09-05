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
	MemtxSpace(Engine *e, struct tuple_format *m_format);
	virtual ~MemtxSpace();
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

	virtual void checkIndexDef(struct space *new_space,
				   struct index_def *index_def) override;
	virtual Index *createIndex(struct space *space,
				   struct index_def *index_def) override;
	virtual void addPrimaryKey(struct space *space) override;
	virtual void dropPrimaryKey(struct space *space) override;
	virtual void buildSecondaryKey(struct space *old_space,
				       struct space *new_space,
				       Index *new_index) override;
	virtual void prepareTruncateSpace(struct space *old_space,
					  struct space *new_space) override;
	virtual void commitTruncateSpace(struct space *old_space,
					 struct space *new_space) override;
	virtual void prepareAlterSpace(struct space *old_space,
				       struct space *new_space) override;
	virtual void commitAlterSpace(struct space *old_space,
				      struct space *new_space) override;
	virtual void initSystemSpace(struct space *space) override;

	virtual size_t
	bsize() const override;

	/**
	 * Change binary size of a space subtracting old tuple's
	 * size and adding new tuple's size. Used also for
	 * rollback by swaping old and new tuple.
	 * @param old_tuple Old tuple (replaced or deleted).
	 * @param new_tuple New tuple (inserted).
	 */
	void
	updateBsize(const struct tuple *old_tuple,
		    const struct tuple *new_tuple);

public:
	/**
	 * A pointer to replace function, set to different values
	 * at different stages of recovery.
	 */
	engine_replace_f replace;
private:
	void
	prepareReplace(struct txn_stmt *stmt, struct request *request);
	void
	prepareDelete(struct txn_stmt *stmt, struct space *space,
		      struct request *request);
	void
	prepareUpdate(struct txn_stmt *stmt, struct space *space,
		      struct request *request);
	void
	prepareUpsert(struct txn_stmt *stmt, struct space *space,
		      struct request *request);

private:
	struct tuple_format *m_format;
	/* Number of bytes used in memory by tuples in the space. */
	size_t m_bsize;
};

#endif /* TARANTOOL_BOX_MEMTX_SPACE_H_INCLUDED */
