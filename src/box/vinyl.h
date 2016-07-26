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

#include <small/region.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vinyl_env;
struct vinyl_tx;
struct vinyl_cursor;
struct vinyl_index;
struct key_def;
struct tuple;
struct tuple_format;
struct region;

/*
 * Environment
 */

struct vinyl_env *
vinyl_env_new(void);

int
vinyl_env_delete(struct vinyl_env *e);

/*
 * Recovery
 */

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
 * Introspection
 */

enum vy_type {
	VINYL_NODE = 0,
	VINYL_STRING,
	VINYL_U32,
	VINYL_U64,
};

struct vy_info_node {
	const char *key;
	enum vy_type val_type;
	union {
		uint64_t u64;
		uint32_t u32;
		const char *str;
	} value;
	int childs_cap;
	int childs_n;
	struct vy_info_node *childs;
};

struct vy_info {
	struct vy_info_node root;
	struct region allocator;
	struct vinyl_env *env;
};

int
vy_info_create(struct vy_info *info, struct vinyl_env *e);

void
vy_info_destroy(struct vy_info *creator);

/*
 * Transaction
 */

struct vinyl_tx *
vinyl_begin(struct vinyl_env *e);

int
vinyl_coget(struct vinyl_tx *tx, struct vinyl_index *index,
	    const char *key, uint32_t part_count, struct tuple **result);

int
vinyl_replace(struct vinyl_tx *tx, struct vinyl_index *index,
	      const char *tuple, const char *tuple_end);

int
vinyl_upsert(struct vinyl_tx *tx, struct vinyl_index *index,
	    const char *tuple, const char *tuple_end,
	    const char *ops, const char *ops_end, int index_base);

int
vinyl_delete(struct vinyl_tx *tx, struct vinyl_index *index,
	     const char *key, uint32_t part_count);

int
vinyl_prepare(struct vinyl_env *e, struct vinyl_tx *tx);

int
vinyl_commit(struct vinyl_env *e, struct vinyl_tx *tx, int64_t lsn);

int
vinyl_rollback(struct vinyl_env *e, struct vinyl_tx *tx);

/*
 * Index
 */

struct key_def *
vy_index_key_def(struct vinyl_index *index);

struct vinyl_index *
vinyl_index_by_name(struct vinyl_env *env, const char *name);

struct vinyl_index *
vinyl_index_new(struct vinyl_env *env, struct key_def *key_def,
		struct tuple_format *tuple_format);

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
vinyl_cursor_new(struct vinyl_index *index, const char *key,
		 uint32_t part_count, enum vinyl_order order);

void
vinyl_cursor_delete(struct vinyl_cursor *cursor);

int
vinyl_cursor_conext(struct vinyl_cursor *cursor, struct tuple **result);

/*
 * Replication
 */

typedef int
(*vy_send_row_f)(void *, const char *tuple, uint32_t tuple_size, int64_t lsn);
int
vy_index_send(struct vinyl_index *index, vy_send_row_f sendrow, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_TARANTOOL_BOX_VINYL_H */
