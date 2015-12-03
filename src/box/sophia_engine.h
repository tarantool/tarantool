#ifndef TARANTOOL_BOX_SOPHIA_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_SOPHIA_ENGINE_H_INCLUDED
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
#include "third_party/tarantool_ev.h"

struct SophiaEngine: public Engine {
	SophiaEngine();
	~SophiaEngine();
	virtual void init();
	virtual Handler *open();
	virtual Index *createIndex(struct key_def *);
	virtual void dropIndex(Index*);
	virtual void keydefCheck(struct space *space, struct key_def *f);
	virtual void begin(struct txn *txn);
	virtual void prepare(struct txn *txn);
	virtual void commit(struct txn *txn, int64_t signature);
	virtual void rollbackStatement(struct txn_stmt *stmt);
	virtual void rollback(struct txn *txn);
	virtual void beginJoin();
	virtual void recoverToCheckpoint(int64_t);
	virtual void endRecovery();
	virtual void join(struct relay *relay);
	virtual int beginCheckpoint(int64_t);
	virtual int waitCheckpoint();
	virtual void commitCheckpoint();
	virtual void abortCheckpoint();
	void *env;
private:
	int64_t m_prev_commit_lsn;
	int64_t m_prev_checkpoint_lsn;
	int64_t m_checkpoint_lsn;
public:
	int recovery_complete;
	struct cord *cord;
	ev_async watcher;
	ev_idle idle;
};

extern "C" {
typedef void (*sophia_info_f)(const char*, const char*, void*);
int  sophia_info(const char*, sophia_info_f, void*);
}
void sophia_error(void*);

#endif /* TARANTOOL_BOX_SOPHIA_ENGINE_H_INCLUDED */
