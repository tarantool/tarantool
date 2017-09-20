#ifndef INCLUDES_TARANTOOL_BOX_SEQUENCE_H
#define INCLUDES_TARANTOOL_BOX_SEQUENCE_H
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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Sequence metadata. */
struct sequence_def {
	/** Sequence id. */
	uint32_t id;
	/** Owner of the sequence. */
	uint32_t uid;
	/**
	 * The value added to the sequence at each step.
	 * If it is positive, the sequence is ascending,
	 * otherwise it is descending.
	 */
	int64_t step;
	/** Min sequence value. */
	int64_t min;
	/** Max sequence value. */
	int64_t max;
	/** Initial sequence value. */
	int64_t start;
	/** Number of values to preallocate. Not implemented yet. */
	int64_t cache;
	/**
	 * If this flag is set, the sequence will wrap
	 * upon reaching min or max value by a descending
	 * or ascending sequence respectively.
	 */
	bool cycle;
	/** Sequence name. */
	char name[0];
};

/** Sequence object. */
struct sequence {
	/** Sequence definition. */
	struct sequence_def *def;
	/** Last value returned by the sequence. */
	int64_t value;
	/** True if the sequence was started. */
	bool is_started;
};

static inline size_t
sequence_def_sizeof(uint32_t name_len)
{
	return sizeof(struct sequence_def) + name_len + 1;
}

/** Reset a sequence. */
static inline void
sequence_reset(struct sequence *seq)
{
	seq->is_started = false;
}

/** Set a sequence value. */
static inline void
sequence_set(struct sequence *seq, int64_t value)
{
	seq->value = value;
	seq->is_started = true;
}

/**
 * Advance a sequence.
 *
 * On success, return 0 and assign the next sequence to
 * @result. If the sequence isn't cyclic and has reached
 * its limit, return -1 and set diag.
 */
int
sequence_next(struct sequence *seq, int64_t *result);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_SEQUENCE_H */
