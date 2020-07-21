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
#include "vy_upsert.h"

#include <sys/uio.h>
#include <small/region.h>
#include <msgpuck/msgpuck.h>
#include "vy_stmt.h"
#include "xrow_update.h"
#include "fiber.h"
#include "column_mask.h"

/**
 * Check that key hasn't been changed after applying upsert operation.
 */
static bool
vy_apply_result_does_cross_pk(struct tuple *old_stmt, const char *result,
			      const char *result_end, struct key_def *cmp_def,
			      uint64_t col_mask)
{
	if (!key_update_can_be_skipped(cmp_def->column_mask, col_mask)) {
		struct tuple *tuple =
			vy_stmt_new_replace(tuple_format(old_stmt), result,
					    result_end);
		int cmp_res = vy_stmt_compare(old_stmt, HINT_NONE, tuple,
					      HINT_NONE, cmp_def);
		tuple_unref(tuple);
		return cmp_res != 0;
	}
	return false;
}

/**
 * Apply update operations from @a upsert on tuple @a stmt. If @a stmt is
 * void statement (i.e. it is NULL or delete statement) then operations are
 * applied on tuple stored in @a upsert. Update operations of @a upsert which
 * can't be applied are skipped along side with other operations from single
 * group (i.e. packed in one msgpack array); errors may be logged depending on
 * @a suppress_error flag.
 *
 * @param upsert Upsert statement to be applied on @a stmt.
 * @param stmt Statement to be used as base for upsert operations.
 * @param cmp_def Key definition required to provide check of primary key
 *                modification.
 * @param suppress_error If true, do not raise/log any errors.
 * @return Tuple containing result of upsert application; NULL in case OOM.
 */
static struct tuple *
vy_apply_upsert_on_terminal_stmt(struct tuple *upsert, struct tuple *stmt,
				 struct key_def *cmp_def, bool suppress_error)
{
	assert(vy_stmt_type(upsert) == IPROTO_UPSERT);
	assert(stmt == NULL || vy_stmt_type(stmt) != IPROTO_UPSERT);
	uint32_t mp_size;
	const char *new_ops = vy_stmt_upsert_ops(upsert, &mp_size);
	/* Msgpack containing result of upserts application. */
	const char *result_mp;
	bool stmt_is_void = stmt == NULL || vy_stmt_type(stmt) == IPROTO_DELETE;
	if (stmt_is_void)
		result_mp = vy_upsert_data_range(upsert, &mp_size);
	else
		result_mp = tuple_data_range(stmt, &mp_size);
	const char *result_mp_end = result_mp + mp_size;
	/*
	 * xrow_upsert_execute() allocates result using region,
	 * so save starting point to release it later.
	 */
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint64_t column_mask = COLUMN_MASK_FULL;
	struct tuple_format *format = tuple_format(upsert);

	uint32_t ups_cnt = mp_decode_array(&new_ops);
	const char *ups_ops = new_ops;
	/*
	 * In case upsert folds into insert, we must skip first
	 * update operations. Moreover, we should use upsert's tuple
	 * to provide PK modification check.
	 */
	if (stmt_is_void) {
		ups_cnt--;
		mp_next(&ups_ops);
		stmt = upsert;
	}
	for (uint32_t i = 0; i < ups_cnt; ++i) {
		assert(mp_typeof(*ups_ops) == MP_ARRAY);
		const char *ups_ops_end = ups_ops;
		mp_next(&ups_ops_end);
		const char *exec_res = result_mp;
		exec_res = xrow_upsert_execute(ups_ops, ups_ops_end, result_mp,
					       result_mp_end, format, &mp_size,
					       0, suppress_error, &column_mask);
		if (exec_res == NULL) {
			if (! suppress_error) {
				struct error *e = diag_last_error(diag_get());
				assert(e != NULL);
				/* Bail out immediately in case of OOM. */
				if (e->type != &type_ClientError) {
					region_truncate(region, region_svp);
					return NULL;
				}
				diag_log();
			}
			ups_ops = ups_ops_end;
			continue;
		}
		/*
		 * If it turns out that resulting tuple modifies primary
		 * key, then simply ignore this upsert.
		 */
		if (vy_apply_result_does_cross_pk(stmt, exec_res,
						  exec_res + mp_size, cmp_def,
						  column_mask)) {
			if (!suppress_error) {
				say_error("upsert operations %s are not applied"\
					  " due to primary key modification",
					  mp_str(ups_ops));
			}
			ups_ops = ups_ops_end;
			continue;
		}
		ups_ops = ups_ops_end;
		/*
		 * Result statement must satisfy space's format. Since upsert's
		 * tuple correctness is already checked in vy_upsert(), let's
		 * use its format to provide result verification.
		 */
		struct tuple_format *format = tuple_format(upsert);
		if (tuple_validate_raw(format, exec_res) != 0) {
			if (! suppress_error)
				diag_log();
			continue;
		}
		result_mp = exec_res;
		result_mp_end = exec_res + mp_size;
	}
	struct tuple *new_terminal_stmt = vy_stmt_new_replace(format, result_mp,
							      result_mp_end);
	region_truncate(region, region_svp);
	if (new_terminal_stmt == NULL)
		return NULL;
	vy_stmt_set_lsn(new_terminal_stmt, vy_stmt_lsn(upsert));
	return new_terminal_stmt;
}

/**
 * Unpack upsert's update operations from msgpack array
 * into array of iovecs.
 */
static void
upsert_ops_to_iovec(const char *ops, uint32_t ops_cnt, struct iovec *iov_arr)
{
	for (uint32_t i = 0; i < ops_cnt; ++i) {
		assert(mp_typeof(*ops) == MP_ARRAY);
		iov_arr[i].iov_base = (char *) ops;
		mp_next(&ops);
		iov_arr[i].iov_len = ops - (char *) iov_arr[i].iov_base;
	}
}

struct tuple *
vy_apply_upsert(struct tuple *new_stmt, struct tuple *old_stmt,
		struct key_def *cmp_def, bool suppress_error)
{
	/*
	 * old_stmt - previous (old) version of stmt
	 * new_stmt - next (new) version of stmt
	 * result_stmt - the result of merging new and old
	 */
	assert(new_stmt != NULL);
	assert(new_stmt != old_stmt);
	assert(vy_stmt_type(new_stmt) == IPROTO_UPSERT);

	struct tuple *result_stmt = NULL;
	if (old_stmt == NULL || vy_stmt_type(old_stmt) != IPROTO_UPSERT) {
		return vy_apply_upsert_on_terminal_stmt(new_stmt, old_stmt,
						        cmp_def, suppress_error);
	}

	assert(old_stmt != NULL);
	assert(vy_stmt_type(old_stmt) == IPROTO_UPSERT);
	/*
	 * Unpack UPSERT operation from the old and new stmts.
	 */
	uint32_t mp_size;
	const char *old_ops = vy_stmt_upsert_ops(old_stmt, &mp_size);
	const char *old_stmt_mp = vy_upsert_data_range(old_stmt, &mp_size);
	const char *old_stmt_mp_end = old_stmt_mp + mp_size;
	const char *new_ops = vy_stmt_upsert_ops(new_stmt, &mp_size);
	/*
	 * UPSERT + UPSERT case: unpack operations to iovec array and merge
	 * them into one ops array.
	 */
	struct tuple_format *format = tuple_format(old_stmt);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint32_t old_ops_cnt = mp_decode_array(&old_ops);
	uint32_t new_ops_cnt = mp_decode_array(&new_ops);
	uint32_t total_ops_cnt = old_ops_cnt + new_ops_cnt;
	size_t ops_size;
	struct iovec *operations =
		region_alloc_array(region, typeof(operations[0]),
				   total_ops_cnt + 1, &ops_size);
	if (operations == NULL) {
		region_truncate(region, region_svp);
		diag_set(OutOfMemory, ops_size, "region_alloc_array",
			 "operations");
		return NULL;
	}
	char header[16];
	char *header_end = mp_encode_array(header, total_ops_cnt);
	operations[0].iov_base = header;
	operations[0].iov_len = header_end - header;
	upsert_ops_to_iovec(old_ops, old_ops_cnt, &operations[1]);
	upsert_ops_to_iovec(new_ops, new_ops_cnt, &operations[old_ops_cnt + 1]);
	/*
	 * Adding update operations. We keep order of update operations in
	 * the array the same. It is vital since first set of operations
	 * must be skipped in case upsert folds into insert. For instance:
	 * old_ops = {{{op1}, {op2}}, {{op3}}}
	 * new_ops = {{{op4}, {op5}}}
	 * res_ops = {{{op1}, {op2}}, {{op3}}, {{op4}, {op5}}}
	 * If upsert corresponding to old_ops becomes insert, then
	 * {{op1}, {op2}} update operations are not applied.
	 */
	result_stmt = vy_stmt_new_upsert(format, old_stmt_mp, old_stmt_mp_end,
					 operations, total_ops_cnt + 1);
	region_truncate(region, region_svp);
	if (result_stmt == NULL)
		return NULL;
	vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));
	return result_stmt;
}
