#ifndef TARANTOOL_BOX_PHIA_ENGINE_H_INCLUDED
#define TARANTOOL_BOX_PHIA_ENGINE_H_INCLUDED
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
#include "third_party/tarantool_ev.h"
#include "small/mempool.h"
#include "small/region.h"

struct PhiaEngine: public Engine {
	PhiaEngine();
	~PhiaEngine();
	virtual void init() override;
	virtual Handler *open() override;
	virtual Index *createIndex(struct key_def *) override;
	virtual void dropIndex(Index*) override;
	virtual void keydefCheck(struct space *space, struct key_def *f) override;
	virtual void begin(struct txn *txn) override;
	virtual void prepare(struct txn *txn) override;
	virtual void commit(struct txn *txn, int64_t signature) override;
	virtual void rollback(struct txn *txn) override;
	virtual void bootstrap() override;
	virtual void beginInitialRecovery() override;
	virtual void beginFinalRecovery() override;
	virtual void endRecovery() override;
	virtual void join(struct xstream *stream) override;
	virtual int beginCheckpoint() override;
	virtual int waitCheckpoint(struct vclock *vclock) override;
public:
	struct phia_env *env;
	int recovery_complete;
};

extern "C" {
typedef void (*phia_info_f)(const char*, const char*, void*);
int phia_info(const char*, phia_info_f, void*);
}
void phia_workers_start(struct phia_env *);

struct phia_tuple;
struct phia_tx;
struct phia_cursor;

int
phia_coget(struct phia_tx *tx, struct phia_index *index,
	   struct phia_tuple *key, struct phia_tuple **result);
int
phia_index_coget(struct phia_index *index, struct phia_tuple *key,
		 struct phia_tuple **result);
int
phia_cursor_conext(struct phia_cursor *tx, struct phia_tuple **result);

struct tuple *
phia_convert_tuple(struct phia_index *index, struct phia_tuple *phia_tuple,
		   struct key_def *key_def, struct tuple_format *format);

#endif /* TARANTOOL_BOX_PHIA_ENGINE_H_INCLUDED */
