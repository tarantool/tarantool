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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "trivia/util.h"
#include "cfg.h"
#include "scoped_guard.h"

#include "vinyl_index.h"
#include "vinyl_space.h"
#include "xrow.h"
#include "tuple.h"
#include "txn.h"
#include "index.h"
#include "relay.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "request.h"
#include "iproto_constants.h"
#include "vinyl.h"

/* Used by lua/info.c */
extern "C" struct vinyl_env *
vinyl_engine_get_env()
{
	VinylEngine *e = (VinylEngine *)engine_find("vinyl");
	return e->env;
}

VinylEngine::VinylEngine()
	:Engine("vinyl")
	 ,recovery_complete(0)
{
	flags = 0;
	env = NULL;
}

VinylEngine::~VinylEngine()
{
	if (env)
		vinyl_env_delete(env);
}

void
VinylEngine::init()
{
	env = vinyl_env_new();
	if (env == NULL)
		panic("failed to create vinyl environment");
}

void
VinylEngine::bootstrap()
{
	vinyl_bootstrap(env);
	recovery_complete = 1;
}

void
VinylEngine::beginInitialRecovery()
{
	vinyl_begin_initial_recovery(env);
}

void
VinylEngine::beginFinalRecovery()
{
	vinyl_begin_final_recovery(env);
}

void
VinylEngine::endRecovery()
{
	assert(!recovery_complete);
	/* complete two-phase recovery */
	vinyl_end_recovery(env);
	recovery_complete = 1;
}

Handler *
VinylEngine::open()
{
	return new VinylSpace(this);
}

struct vinyl_send_row_arg {
	struct xstream *stream;
	uint32_t space_id;
};

static int
vinyl_send_row(void *arg, const char *tuple, uint32_t tuple_size, int64_t lsn)
{
	struct xstream *stream = ((struct vinyl_send_row_arg *) arg)->stream;
	uint32_t space_id = ((struct vinyl_send_row_arg *) arg)->space_id;

	struct request_replace_body body;
	body.m_body = 0x82; /* map of two elements. */
	body.k_space_id = IPROTO_SPACE_ID;
	body.m_space_id = 0xce; /* uint32 */
	body.v_space_id = mp_bswap_u32(space_id);
	body.k_tuple = IPROTO_TUPLE;
	struct xrow_header row;
	row.type = IPROTO_INSERT;
	row.server_id = 0;
	row.lsn = lsn;
	row.bodycnt = 2;
	row.body[0].iov_base = &body;
	row.body[0].iov_len = sizeof(body);
	row.body[1].iov_base = (char *) tuple;
	row.body[1].iov_len = tuple_size;
	try {
		xstream_write(stream, &row);
	} catch (Exception *e) {
		 return -1;
	}
	return 0;
}

struct join_send_space_arg {
	struct vinyl_env *env;
	struct xstream *stream;
};

static void
join_send_space(struct space *sp, void *data)
{
	struct xstream *stream = ((struct join_send_space_arg *) data)->stream;
	if (space_is_temporary(sp))
		return;
	if (!space_is_vinyl(sp))
		return;
	VinylIndex *pk = (VinylIndex *) space_index(sp, 0);
	if (!pk)
		return;

	/* send database */
	struct vinyl_send_row_arg arg = { stream, sp->def.id };
	if (vy_index_send(pk->db, vinyl_send_row, &arg) != 0)
		diag_raise();
}

/**
 * Relay all data currently stored in Vinyl engine
 * to the replica.
 */
void
VinylEngine::join(struct xstream *stream)
{
	struct join_send_space_arg arg = { env, stream };
	space_foreach(join_send_space, &arg);
}

Index*
VinylEngine::createIndex(struct key_def *key_def)
{
	switch (key_def->type) {
	case TREE: return new VinylIndex(key_def);
	default:
		unreachable();
		return NULL;
	}
}

void
VinylEngine::dropIndex(Index *index)
{
	VinylIndex *i = (VinylIndex *)index;
	/* schedule asynchronous drop */
	int rc = vinyl_index_drop(i->db);
	if (rc == -1)
		diag_raise();
	i->db  = NULL;
	i->env = NULL;
}

void
VinylEngine::keydefCheck(struct space *space, struct key_def *key_def)
{
	if (key_def->type != TREE) {
		tnt_raise(ClientError, ER_INDEX_TYPE,
		          key_def->name,
		          space_name(space));
	}
}

void
VinylEngine::begin(struct txn *txn)
{
	assert(txn->engine_tx == NULL);
	txn->engine_tx = vinyl_begin(env);
	if (txn->engine_tx == NULL)
		diag_raise();
}

void
VinylEngine::prepare(struct txn *txn)
{
	struct vinyl_tx *tx = (struct vinyl_tx *) txn->engine_tx;

	int rc = vinyl_prepare(env, tx);
	switch (rc) {
	case 1: /* rollback, will be done by all-engines transaction manager */
		tnt_raise(ClientError, ER_TRANSACTION_CONFLICT);
		break;
	case -1:
		diag_raise();
		break;
	}
}

void
VinylEngine::commit(struct txn *txn, int64_t lsn)
{
	struct vinyl_tx *tx = (struct vinyl_tx *) txn->engine_tx;
	if (tx) {
		int rc = vinyl_commit(env, tx, txn->n_rows ? lsn : 0);
		if (rc == -1) {
			panic("vinyl commit failed: txn->signature = %"
			      PRIu64, lsn);
		}
		txn->engine_tx = NULL;
	}
}

void
VinylEngine::rollback(struct txn *txn)
{
	if (txn->engine_tx == NULL)
		return;

	struct vinyl_tx *tx = (struct vinyl_tx *) txn->engine_tx;
	vinyl_rollback(env, tx);
	txn->engine_tx = NULL;
}

int
VinylEngine::beginCheckpoint()
{
	int rc = vinyl_checkpoint(env);
	if (rc == -1)
		diag_raise();
	return 0;
}

int
VinylEngine::waitCheckpoint(struct vclock*)
{
	for (;;) {
		if (!vinyl_checkpoint_is_active(env))
			break;
		fiber_yield_timeout(.020);
	}
	return 0;
}
