#ifndef TARANTOOL_BOX_VINYL_SPACE_H_INCLUDED
#define TARANTOOL_BOX_VINYL_SPACE_H_INCLUDED
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
#include "vinyl_engine.h"

struct VinylSpace: public Handler {
	VinylSpace(Engine*);
	virtual void
	applyInitialJoinRow(struct space *space,
			    struct request *request) override;
	virtual struct tuple *
	executeReplace(struct txn*, struct space *space,
	               struct request *request) override;
	virtual struct tuple *
	executeDelete(struct txn*, struct space *space,
	              struct request *request) override;
	virtual struct tuple *
	executeUpdate(struct txn*, struct space *space,
	              struct request *request) override;
	virtual void
	executeUpsert(struct txn*, struct space *space,
	              struct request *request) override;
	virtual void dropIndex(Index*) override;
	virtual Index *createIndex(struct space *, struct index_def *) override;
	virtual void prepareAlterSpace(struct space *old_space,
				       struct space *new_space) override;
	/**
	 * If space was altered then this method updates
	 * pointers to the primary index in all secondary ones.
	 */
	virtual void
	commitAlterSpace(struct space *old_space, struct space *new_space)
		override;
};

#endif /* TARANTOOL_BOX_VINYL_SPACE_H_INCLUDED */
