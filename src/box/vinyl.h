#ifndef INCLUDES_TARANTOOL_BOX_VINYL_H
#define INCLUDES_TARANTOOL_BOX_VINYL_H
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

struct vinyl_env;
struct vinyl_service;
struct vinyl_tx;
struct vinyl_field;
struct vinyl_tuple;
struct vinyl_cursor;
struct vinyl_index;
struct vinyl_confcursor;
struct vinyl_confkv;
struct key_def;
struct tuple;
struct tuple_format;

/*
 * Environment
 */

struct vinyl_env *
vinyl_env_new(void);

int
vinyl_env_delete(struct vinyl_env *e);

void
vinyl_raise();

struct vinyl_confcursor *
vinyl_confcursor_new(struct vinyl_env *env);

void
vinyl_confcursor_delete(struct vinyl_confcursor *confcursor);

int
vinyl_confcursor_next(struct vinyl_confcursor *confcursor, const char **key,
		     const char **value);

void
vinyl_bootstrap(struct vinyl_env *e);

void
vinyl_begin_initial_recovery(struct vinyl_env *e);

void
vinyl_begin_final_recovery(struct vinyl_env *e);

void
vinyl_end_recovery(struct vinyl_env *e);

int
vinyl_checkpoint(struct vinyl_env *env);

bool
vinyl_checkpoint_is_active(struct vinyl_env *env);

/*
 * Workers
 */

struct vinyl_service *
vinyl_service_new(struct vinyl_env *env);

int
vinyl_service_do(struct vinyl_service *srv);

void
vinyl_service_delete(struct vinyl_service *srv);

/*
 * Transaction
 */

struct vinyl_tx *
vinyl_begin(struct vinyl_env *e);

int
vinyl_coget(struct vinyl_tx *tx, struct vinyl_index *index,
	    struct vinyl_tuple *key, struct vinyl_tuple **result);

int
vinyl_replace(struct vinyl_tx *tx, struct vinyl_index *index,
	     struct vinyl_tuple *tuple);

int
vinyl_upsert(struct vinyl_tx *tx, struct vinyl_index *index,
	    const char *tuple, const char *tuple_end,
	    const char *ops, const char *ops_end, int index_base);

int
vinyl_delete(struct vinyl_tx *tx, struct vinyl_index *index,
	    struct vinyl_tuple *tuple);

int
vinyl_prepare(struct vinyl_tx *tx);

int
vinyl_commit(struct vinyl_tx *tx, int64_t lsn);

int
vinyl_rollback(struct vinyl_tx *tx);

/*
 * Index
 */

struct vinyl_index *
vinyl_index_by_name(struct vinyl_env *env, const char *name);

struct vinyl_index *
vinyl_index_new(struct vinyl_env *e, struct key_def *);

void
vinyl_index_ref(struct vinyl_index *index);

int
vinyl_index_unref(struct vinyl_index *index);

int
vinyl_index_open(struct vinyl_index *index);

/**
 * Close index during database shutdown
 */
int
vinyl_index_close(struct vinyl_index *index);

/**
 * Close index and drop all data
 */
int
vinyl_index_drop(struct vinyl_index *index);

size_t
vinyl_index_bsize(struct vinyl_index *db);

uint64_t
vinyl_index_size(struct vinyl_index *db);

/*
 * Index Cursor
 */

enum vinyl_order {
	VINYL_LT,
	VINYL_LE,
	VINYL_GT,
	VINYL_GE,
	VINYL_EQ
};

struct vinyl_cursor *
vinyl_cursor_new(struct vinyl_index *index, struct vinyl_tuple *key,
		enum vinyl_order order);

void
vinyl_cursor_delete(struct vinyl_cursor *cursor);

void
vinyl_cursor_set_read_commited(struct vinyl_cursor *cursor, bool read_commited);

int
vinyl_cursor_next(struct vinyl_cursor *c, struct vinyl_tuple **result,
		 bool cache_only); /* used from relay cord */
int
vinyl_cursor_conext(struct vinyl_cursor *cursor, struct vinyl_tuple **result);

/*
 * Tuple
 */

struct vinyl_tuple *
vinyl_tuple_from_data(struct vinyl_index *index, const char *data,
		     const char *data_end);

struct vinyl_tuple *
vinyl_tuple_from_key_data(struct vinyl_index *index, const char *key,
			 uint32_t part_count, int order);

struct tuple *
vinyl_convert_tuple(struct vinyl_index *index, struct vinyl_tuple *vinyl_tuple,
		    struct tuple_format *format);

char *
vinyl_convert_tuple_data(struct vinyl_index *index,
			 struct vinyl_tuple *vinyl_tuple, uint32_t *bsize);

void
vinyl_tuple_ref(struct vinyl_tuple *tuple);

void
vinyl_tuple_unref(struct vinyl_index *index, struct vinyl_tuple *tuple);

int64_t
vinyl_tuple_lsn(struct vinyl_tuple *doc);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_TARANTOOL_BOX_VINYL_H */
