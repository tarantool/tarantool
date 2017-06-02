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
#include "unit.h"
#include "vy_stmt.h"
#include "small/rlist.h"
#include "small/lsregion.h"
#include "vy_mem.h"
#include "vy_stmt_iterator.h"

#define vyend 99999999
#define MAX_FIELDS_COUNT 100
#define STMT_TEMPLATE(lsn, type, ...) \
{ { __VA_ARGS__, vyend }, IPROTO_##type, lsn, false }

#define STMT_TEMPLATE_OPTIMIZED(lsn, type, ...) \
{ { __VA_ARGS__, vyend }, IPROTO_##type, lsn, true }

extern struct tuple_format_vtab vy_tuple_format_vtab;

/** Template for creation a vinyl statement. */
struct vy_stmt_template {
	/** Array of statement fields, ended with 'vyend'. */
	const int fields[MAX_FIELDS_COUNT];
	/** Statement type: REPLACE/UPSERT/DELETE. */
	enum iproto_type type;
	/** Statement lsn. */
	int64_t lsn;
	/*
	 * True, if statement must have column mask, that allows
	 * to skip it in the write_iterator.
	 */
	bool optimize_update;
};

/**
 * Create a new vinyl statement using the specified template.
 *
 * @param format
 * @param upsert_format Format for upsert statements.
 * @param format_with_colmask Format for statements with a
 *        colmask.
 * @param templ Statement template.
 *
 * @return Created statement.
 */
struct tuple *
vy_new_simple_stmt(struct tuple_format *format,
		   struct tuple_format *upsert_format,
		   struct tuple_format *format_with_colmask,
		   const struct vy_stmt_template *templ);

/**
 * Insert into the mem the statement, created by the specified
 * template.
 *
 * @param vy_mem Mem to insert into.
 * @param templ Statement template to insert.
 */
void
vy_mem_insert_template(struct vy_mem *mem,
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
 * @param region Allocator for statements and bps.
 * @param def Key definition.
 *
 * @return New vy_mem.
 */
struct vy_mem *
create_test_mem(struct lsregion *region, struct key_def *def);

/**
 * Check that the template specifies completely the same statement
 * as @stmt.
 *
 * @param stmt Actual value.
 * @param templ Expected value.
 * @param format Template statement format.
 * @param upsert_format Template upsert statement format.
 * @param format_with_colmask Template statement format with colmask.
 *
 * @retval stmt === template.
 */
bool
vy_stmt_are_same(const struct tuple *actual,
		 const struct vy_stmt_template *expected,
		 struct tuple_format *format,
		 struct tuple_format *upsert_format,
		 struct tuple_format *format_with_colmask);

#endif
