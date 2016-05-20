#ifndef TARANTOOL_BOX_PHIA_SPACE_H_INCLUDED
#define TARANTOOL_BOX_PHIA_SPACE_H_INCLUDED
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

struct PhiaSpace: public Handler {
	PhiaSpace(Engine*);
	virtual void
	applySnapshotRow(struct space *space, struct request *request);
	virtual struct tuple *
	executeReplace(struct txn*, struct space *space,
	               struct request *request);
	virtual struct tuple *
	executeDelete(struct txn*, struct space *space,
	              struct request *request);
	virtual struct tuple *
	executeUpdate(struct txn*, struct space *space,
	              struct request *request);
	virtual void
	executeUpsert(struct txn*, struct space *space,
	              struct request *request);
};

struct key_def;
/* TODO: move to phia.c */
extern "C" int
phia_upsert_cb(int count,
	       char **src,    uint32_t *src_size,
	       char **upsert, uint32_t *upsert_size,
	       char **result, uint32_t *result_size,
	       struct key_def *key_def);

#endif /* TARANTOOL_BOX_PHIA_SPACE_H_INCLUDED */
