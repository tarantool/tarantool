#ifndef INCLUDES_TARANTOOL_BOX_VY_STMT_STREAM_H
#define INCLUDES_TARANTOOL_BOX_VY_STMT_STREAM_H
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

#include <trivia/util.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vy_entry;

/**
 * The stream is a very simple iterator (generally over a mem or a run)
 * that output all the tuples on increasing order.
 */
struct vy_stmt_stream;

/**
 * Start streaming
 */
typedef NODISCARD int
(*vy_stream_start_f)(struct vy_stmt_stream *virt_stream);

/**
 * Get next tuple from a stream.
 */
typedef NODISCARD int
(*vy_stream_next_f)(struct vy_stmt_stream *virt_stream, struct vy_entry *ret);

/**
 * Close the stream.
 */
typedef void
(*vy_stream_close_f)(struct vy_stmt_stream *virt_stream);

/**
 * The interface description for streams over run and mem.
 */
struct vy_stmt_stream_iface {
	vy_stream_start_f start;
	vy_stream_next_f next;
	vy_stream_close_f stop;
	vy_stream_close_f close;
};

/**
 * Common interface for streams over run and mem.
 */
struct vy_stmt_stream {
	const struct vy_stmt_stream_iface *iface;
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_VY_STMT_STREAM_H */
