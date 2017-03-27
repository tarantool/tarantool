#ifndef TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED
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

struct vy_env;

struct VinylEngine: public Engine {
	VinylEngine();
	~VinylEngine();
	virtual void init() override;
	virtual Handler *open() override;
	virtual void addPrimaryKey(struct space *space) override;
	virtual void buildSecondaryKey(struct space *old_space,
				       struct space *new_space,
				       Index *new_index) override;
	virtual void checkIndexDef(struct space *space, struct index_def *f) override;
	virtual void beginStatement(struct txn *txn) override;
	virtual void begin(struct txn *txn) override;
	virtual void prepare(struct txn *txn) override;
	virtual void commit(struct txn *txn, int64_t signature) override;
	virtual void rollback(struct txn *txn) override;
	virtual void rollbackStatement(struct txn *txn,
				       struct txn_stmt *stmt) override;
	virtual void bootstrap() override;
	virtual void beginInitialRecovery(struct vclock *vclock) override;
	virtual void beginFinalRecovery() override;
	virtual void endRecovery() override;
	virtual void join(struct xstream *stream) override;
	virtual int prepareWaitCheckpoint(struct vclock *vclock) override;
	virtual int waitCheckpoint(struct vclock *vclock) override;
	virtual void commitCheckpoint(struct vclock *vclock) override;
	virtual void abortCheckpoint() override;
public:
	struct vy_env *env;
};

#endif /* TARANTOOL_BOX_VINYL_ENGINE_H_INCLUDED */
