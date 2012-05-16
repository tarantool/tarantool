#ifndef TARANTOOL_BOX_TXN_H_INCLUDED
#define TARANTOOL_BOX_TXN_H_INCLUDED
/*
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
#include <fiber.h>
struct tuple;
@class Index;

struct txn {
	u16 type;
	u32 flags;

	struct lua_State *L;
	struct port *port;
	struct space *space;
	Index *index;

	struct tuple *old_tuple;
	struct tuple *new_tuple;
	struct tuple *lock_tuple;

	struct tbuf req;
};


/** tuple's flags */
enum tuple_flags {
	/** Waiting on WAL write to complete. */
	WAL_WAIT = 0x1,
	/** A new primary key is created but not yet written to WAL. */
	GHOST = 0x2,
};

static inline struct txn *in_txn() { return fiber->mod_data.txn; }
struct txn *txn_begin();
void txn_commit(struct txn *txn);
void txn_rollback(struct txn *txn);
void txn_lock_tuple(struct txn *txn, struct tuple *tuple);
#endif /* TARANTOOL_BOX_TXN_H_INCLUDED */
