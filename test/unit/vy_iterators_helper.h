#ifndef INCLUDES_TARANTOOL_TEST_VY_ITERATORS_HELPER_H
#define INCLUDES_TARANTOOL_TEST_VY_ITERATORS_HELPER_H
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/uio.h>
#include "vy_stmt.h"
#include "small/rlist.h"
#include "small/lsregion.h"
#include "vy_mem.h"
#include "vy_cache.h"
#include "vy_history.h"
#include "vy_read_view.h"

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"

#define vyend 99999999
#define MAX_FIELDS_COUNT 100
#define STMT_TEMPLATE(lsn, type, ...) \
{ { __VA_ARGS__, vyend }, IPROTO_##type, lsn, 0, 0, 0 }

#define STMT_TEMPLATE_FLAGS(lsn, type, flags, ...) \
{ { __VA_ARGS__, vyend }, IPROTO_##type, lsn, flags, 0, 0 }

#define STMT_TEMPLATE_DEFERRED_DELETE(lsn, type, ...) \
STMT_TEMPLATE_FLAGS(lsn, type, VY_STMT_DEFERRED_DELETE, __VA_ARGS__)

#define STMT_TEMPLATE_SKIP_READ(lsn, type, ...) \
STMT_TEMPLATE_FLAGS(lsn, type, VY_STMT_SKIP_READ, __VA_ARGS__)

extern struct vy_stmt_env stmt_env;
extern struct vy_mem_env mem_env;
extern struct vy_cache_env cache_env;
extern struct mempool history_node_pool;
extern struct vy_stmt_counter dummy_count;

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialize subsystems neccessary for correct vinyl iterators
 * working.
 * @param cache_size Vinyl cache quota limit.
 */
void
vy_iterator_C_test_init(size_t cache_size);

/** Close subsystems, opened in vy_iterator_C_test_init(). */
void
vy_iterator_C_test_finish();

/** Template for creation a vinyl statement. */
struct vy_stmt_template {
	/** Array of statement fields, ended with 'vyend'. */
	const int fields[MAX_FIELDS_COUNT];
	/** Statement type: REPLACE/UPSERT/DELETE/UPSERT. */
	enum iproto_type type;
	/** Statement lsn. */
	int64_t lsn;
	/** Statement flags. */
	uint8_t flags;
	/*
	 * In case of upsert it is possible to use only one 'add' operation.
	 * This is the column number of the operation.
	 */
	uint32_t upsert_field;
	/** And that is the value to add. */
	int32_t upsert_value;
};

/**
 * Create a new vinyl statement using the specified template.
 *
 * @param format
 * @param key_def Key definition (for computing hint).
 * @param templ Statement template.
 *
 * @return Created statement.
 */
struct vy_entry
vy_new_simple_stmt(struct tuple_format *format, struct key_def *key_def,
		   const struct vy_stmt_template *templ);

/**
 * Insert into the mem the statement, created by the specified
 * template.
 *
 * @param vy_mem Mem to insert into.
 * @param templ Statement template to insert.
 *
 * @retval Lsregion allocated statement.
 */
struct vy_entry
vy_mem_insert_template(struct vy_mem *mem,
		       const struct vy_stmt_template *templ);

/**
 * Insert into the cache the statement template chain, got from
 * the read iterator.
 * @param cache Cache to insert into.
 * @param format Statements format.
 * @param chain Statement template array.
 * @param length Length of @a chain.
 * @param key_templ Key template.
 * @param order Iteration order.
 */
void
vy_cache_insert_templates_chain(struct vy_cache *cache,
				struct tuple_format *format,
				const struct vy_stmt_template *chain,
				uint length,
				const struct vy_stmt_template *key_templ,
				enum iterator_type order);

/**
 * Vy_cache_on_write wrapper for statement templates.
 * @param cache Cache to update to.
 * @param templ Written statement template.
 */
void
vy_cache_on_write_template(struct vy_cache *cache, struct tuple_format *format,
			   const struct vy_stmt_template *templ);

/**
 * Create a list of read views using the specified vlsns.
 *
 * @param rlist[out] Result list of read views.
 * @param rvs[out] Read views array.
 * @param vlsns Array of read view lsns, sorted in ascending
 *        order.
 * @param count Size of the @vlsns.
 */
void
init_read_views_list(struct rlist *rlist, struct vy_read_view *rvs,
		     const int *vlsns, int count);

/**
 * Create vy_mem with the specified key_def, using the @region as
 * allocator.
 *
 * @param def Key definition.
 *
 * @return New vy_mem.
 */
struct vy_mem *
create_test_mem(struct key_def *def);

/**
 * Create vy_cache, key_def and tuple_format, using a specified
 * array of key fields.
 * @param fields Array of key field numbers.
 * @param types Array of key field types.
 * @param key_cnt Length of @a fields and @a types.
 * @param[out] cache Cache to create.
 * @param[out] def Key def to create.
 * @param[out] format Tuple format to create.
 */
void
create_test_cache(uint32_t *fields, uint32_t *types,
		  int key_cnt, struct vy_cache *cache, struct key_def **def,
		  struct tuple_format **format);

/**
 * Destroy cache and its resources.
 * @param vy_cache Cache to destroy.
 * @param key_def Key def to delete.
 * @param format Tuple format to unref.
 */
void
destroy_test_cache(struct vy_cache *cache, struct key_def *def,
		   struct tuple_format *format);

/**
 * Check that the template specifies completely the same statement
 * as @stmt.
 *
 * @param actual Actual value.
 * @param templ Expected value.
 * @param format Template statement format.
 * @param key_def Key definition (for computing hint).
 *
 * @retval stmt === template.
 */
bool
vy_stmt_are_same(struct vy_entry actual,
		 const struct vy_stmt_template *expected,
		 struct tuple_format *format, struct key_def *key_def);

#if defined(__cplusplus)
}
#endif

#endif
