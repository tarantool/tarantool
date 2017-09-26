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
#include "sequence.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <msgpuck/msgpuck.h>

#include "error.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "trivia/util.h"

struct sequence_data_iterator {
	struct snapshot_iterator base;
	/** Last tuple returned by the iterator. */
	/** Iterator over the data index. */
	struct light_sequence_iterator iter;
	char tuple[0];
};

static const int BUF_SIZE = mp_sizeof_array(2) + 2 * mp_sizeof_uint(UINT64_MAX);

static const char *
sequence_data_iterator_next(struct snapshot_iterator *base, uint32_t *size)
{
	struct sequence_data_iterator *iter =
		(struct sequence_data_iterator *)base;

	struct sequence_data *data =
		light_sequence_iterator_get_and_next(&sequence_data_index,
						     &iter->iter);
	if (data == NULL)
		return NULL;

	char *buf_end = iter->tuple;
	buf_end = mp_encode_array(buf_end, 2);
	buf_end = mp_encode_uint(buf_end, data->id);
	buf_end = (data->value >= 0 ?
		   mp_encode_uint(buf_end, data->value) :
		   mp_encode_int(buf_end, data->value));
	assert(buf_end <= iter->tuple + BUF_SIZE);
	*size = buf_end - iter->tuple;
	return iter->tuple;
}

static void
sequence_data_iterator_free(struct snapshot_iterator *base)
{
	struct sequence_data_iterator *iter =
		(struct sequence_data_iterator *)base;
	light_sequence_iterator_destroy(&sequence_data_index, &iter->iter);
	TRASH(iter);
	free(iter);
}

struct snapshot_iterator *
sequence_data_iterator_create(void)
{
	struct sequence_data_iterator *iter =
		(struct sequence_data_iterator *)calloc(1, sizeof(*iter) + BUF_SIZE);
	if (iter == NULL)
		tnt_raise(OutOfMemory, sizeof(*iter) + BUF_SIZE,
			  "malloc", "sequence_data_iterator");

	iter->base.free = sequence_data_iterator_free;
	iter->base.next = sequence_data_iterator_next;

	light_sequence_iterator_begin(&sequence_data_index, &iter->iter);
	light_sequence_iterator_freeze(&sequence_data_index, &iter->iter);
	return &iter->base;
}
