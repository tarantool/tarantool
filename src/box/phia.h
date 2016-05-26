#ifndef INCLUDES_TARANTOOL_BOX_PHIA_H
#define INCLUDES_TARANTOOL_BOX_PHIA_H
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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct phia_env;
struct phia_service;
struct phia_tx;
struct phia_document;
struct phia_cursor;
struct phia_index;
struct phia_confcursor;
struct phia_confkv;
struct key_def;

/*
 * Environment
 */

struct phia_env *
phia_env_new(void);

int
phia_env_delete(struct phia_env *e);

void
phia_raise();

struct phia_confcursor *
phia_confcursor_new(struct phia_env *env);

void
phia_confcursor_delete(struct phia_confcursor *confcursor);

int
phia_confcursor_next(struct phia_confcursor *confcursor, const char **key,
		     const char **value);

void
phia_bootstrap(struct phia_env *e);

void
phia_begin_initial_recovery(struct phia_env *e);

void
phia_begin_final_recovery(struct phia_env *e);

void
phia_end_recovery(struct phia_env *e);

int
phia_checkpoint(struct phia_env *env);

bool
phia_checkpoint_is_active(struct phia_env *env);

/*
 * Workers
 */

struct phia_service *
phia_service_new(struct phia_env *env);

int
phia_service_do(struct phia_service *srv);

void
phia_service_delete(struct phia_service *srv);

/*
 * Transaction
 */

struct phia_tx *
phia_begin(struct phia_env *e);

int
phia_get(struct phia_tx *tx, struct phia_document *key,
	 struct phia_document **result, bool cache_only);

int
phia_replace(struct phia_tx *tx, struct phia_document *doc);

int
phia_upsert(struct phia_tx *tx, struct phia_document *doc);

int
phia_delete(struct phia_tx *tx, struct phia_document *doc);

int
phia_commit(struct phia_tx *tx);

int
phia_rollback(struct phia_tx *tx);

void
phia_tx_set_lsn(struct phia_tx *tx, int64_t lsn);

void
phia_tx_set_half_commit(struct phia_tx *tx, bool half_commit);

/*
 * Index
 */

struct phia_index *
phia_index_by_name(struct phia_env *env, const char *name);

struct phia_index *
phia_index_new(struct phia_env *e, struct key_def *);

int
phia_index_open(struct phia_index *index);

int
phia_index_close(struct phia_index *index);

int
phia_index_drop(struct phia_index *index);

int
phia_index_delete(struct phia_index *index);

int
phia_index_get(struct phia_index *index, struct phia_document *key,
	        struct phia_document **result, bool cache_only);

size_t
phia_index_bsize(struct phia_index *db);

uint64_t
phia_index_size(struct phia_index *db);

/*
 * Index Cursor
 */

enum phia_order {
	PHIA_LT,
	PHIA_LE,
	PHIA_GT,
	PHIA_GE,
	PHIA_EQ
};

struct phia_cursor *
phia_cursor_new(struct phia_index *index, struct phia_document *key,
		enum phia_order order);

void
phia_cursor_delete(struct phia_cursor *cursor);

void
phia_cursor_set_read_commited(struct phia_cursor *cursor, bool read_commited);

int
phia_cursor_next(struct phia_cursor *cursor, struct phia_document **result,
		 bool cache_only);

/*
 * Document
 */

struct phia_document *
phia_document_new(struct phia_index *index);

int
phia_document_delete(struct phia_document *doc);

int
phia_document_set_field(struct phia_document *doc, const char *path,
			const char *value, int size);

char *
phia_document_field(struct phia_document *doc, const char *path, uint32_t *size);

int64_t
phia_document_lsn(struct phia_document *doc);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_TARANTOOL_BOX_PHIA_H */
