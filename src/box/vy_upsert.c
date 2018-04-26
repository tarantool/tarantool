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
#include "tuple_update.h"
#include "fiber.h"
#include "column_mask.h"

static void *
vy_update_alloc(void *arg, size_t size)
{
	/* TODO: rewrite tuple_upsert_execute() without exceptions */
	struct region *region = (struct region *) arg;
	void *data = region_aligned_alloc(region, size, sizeof(uint64_t));
	if (data == NULL)
		diag_set(OutOfMemory, size, "region", "upsert");
	return data;
}

/**
 * Try to squash two upsert series (msgspacked index_base + ops)
 * Try to create a tuple with squahed operations
 *
 * @retval 0 && *result_stmt != NULL : successful squash
 * @retval 0 && *result_stmt == NULL : unsquashable sources
 * @retval -1 - memory error
 */
static int
vy_upsert_try_to_squash(struct tuple_format *format, struct region *region,
			const char *key_mp, const char *key_mp_end,
			const char *old_serie, const char *old_serie_end,
			const char *new_serie, const char *new_serie_end,
			struct tuple **result_stmt)
{
	*result_stmt = NULL;

	size_t squashed_size;
	const char *squashed =
		tuple_upsert_squash(vy_update_alloc, region,
				    old_serie, old_serie_end,
				    new_serie, new_serie_end,
				    &squashed_size, 0);
	if (squashed == NULL)
		return 0;
	/* Successful squash! */
	struct iovec operations[1];
	operations[0].iov_base = (void *)squashed;
	operations[0].iov_len = squashed_size;

	*result_stmt = vy_stmt_new_upsert(format, key_mp, key_mp_end,
					  operations, 1);
	if (*result_stmt == NULL)
		return -1;
	return 0;
}

struct tuple *
vy_apply_upsert(const struct tuple *new_stmt, const struct tuple *old_stmt,
		const struct key_def *cmp_def, struct tuple_format *format,
		bool suppress_error)
{
	/*
	 * old_stmt - previous (old) version of stmt
	 * new_stmt - next (new) version of stmt
	 * result_stmt - the result of merging new and old
	 */
	assert(new_stmt != NULL);
	assert(new_stmt != old_stmt);
	assert(vy_stmt_type(new_stmt) == IPROTO_UPSERT);

	if (old_stmt == NULL || vy_stmt_type(old_stmt) == IPROTO_DELETE) {
		/*
		 * INSERT case: return new stmt.
		 */
		return vy_stmt_replace_from_upsert(new_stmt);
	}

	/*
	 * Unpack UPSERT operation from the new stmt
	 */
	uint32_t mp_size;
	const char *new_ops;
	new_ops = vy_stmt_upsert_ops(new_stmt, &mp_size);
	const char *new_ops_end = new_ops + mp_size;

	/*
	 * Apply new operations to the old stmt
	 */
	const char *result_mp;
	if (vy_stmt_type(old_stmt) == IPROTO_UPSERT)
		result_mp = vy_upsert_data_range(old_stmt, &mp_size);
	else
		result_mp = tuple_data_range(old_stmt, &mp_size);
	const char *result_mp_end = result_mp + mp_size;
	struct tuple *result_stmt = NULL;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	uint8_t old_type = vy_stmt_type(old_stmt);
	uint64_t column_mask = COLUMN_MASK_FULL;
	result_mp = tuple_upsert_execute(vy_update_alloc, region, new_ops,
					 new_ops_end, result_mp, result_mp_end,
					 &mp_size, 0, suppress_error,
					 &column_mask);
	result_mp_end = result_mp + mp_size;
	if (old_type != IPROTO_UPSERT) {
		assert(old_type == IPROTO_INSERT ||
		       old_type == IPROTO_REPLACE);
		/*
		 * UPDATE case: return the updated old stmt.
		 */
		result_stmt = vy_stmt_new_replace(format, result_mp,
						  result_mp_end);
		region_truncate(region, region_svp);
		if (result_stmt == NULL)
			return NULL; /* OOM */
		vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));
		goto check_key;
	}

	/*
	 * Unpack UPSERT operation from the old stmt
	 */
	assert(old_stmt != NULL);
	const char *old_ops;
	old_ops = vy_stmt_upsert_ops(old_stmt, &mp_size);
	const char *old_ops_end = old_ops + mp_size;
	assert(old_ops_end > old_ops);

	/*
	 * UPSERT + UPSERT case: combine operations
	 */
	assert(old_ops_end - old_ops > 0);
	if (vy_upsert_try_to_squash(format, region,
				    result_mp, result_mp_end,
				    old_ops, old_ops_end,
				    new_ops, new_ops_end,
				    &result_stmt) != 0) {
		region_truncate(region, region_svp);
		return NULL;
	}
	if (result_stmt != NULL) {
		region_truncate(region, region_svp);
		vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));
		goto check_key;
	}

	/* Failed to squash, simply add one upsert to another */
	int old_ops_cnt, new_ops_cnt;
	struct iovec operations[3];

	old_ops_cnt = mp_decode_array(&old_ops);
	operations[1].iov_base = (void *)old_ops;
	operations[1].iov_len = old_ops_end - old_ops;

	new_ops_cnt = mp_decode_array(&new_ops);
	operations[2].iov_base = (void *)new_ops;
	operations[2].iov_len = new_ops_end - new_ops;

	char ops_buf[16];
	char *header = mp_encode_array(ops_buf, old_ops_cnt + new_ops_cnt);
	operations[0].iov_base = (void *)ops_buf;
	operations[0].iov_len = header - ops_buf;

	result_stmt = vy_stmt_new_upsert(format, result_mp, result_mp_end,
					 operations, 3);
	region_truncate(region, region_svp);
	if (result_stmt == NULL)
		return NULL;
	vy_stmt_set_lsn(result_stmt, vy_stmt_lsn(new_stmt));

check_key:
	/*
	 * Check that key hasn't been changed after applying operations.
	 */
	if (!key_update_can_be_skipped(cmp_def->column_mask, column_mask) &&
	    vy_tuple_compare(old_stmt, result_stmt, cmp_def) != 0) {
		/*
		 * Key has been changed: ignore this UPSERT and
		 * @retval the old stmt.
		 */
		tuple_unref(result_stmt);
		result_stmt = vy_stmt_dup(old_stmt);
	}
	return result_stmt;
}
