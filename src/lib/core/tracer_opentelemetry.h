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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** box.cfg.enable_tracing. */
extern bool trace_opentelemetry_enabled;

/**
 * Set option which is responsible for Enable or disable
 * OpenTelemetry tracing.
 */
void
trace_opentelemetry_set_enabled(bool enabled);

/** Returns whether OpenTelemetry tracing is enabled or not. */
bool
trace_opentelemetry_get_enabled(void);

/** Kind of an OpenTelemetry span, as defined by the spec. */
enum span_kind {
	INTERNAL		= 1,
	SERVER			= 2,
	CLIENT			= 3,
};

/** A single OpenTelemetry span being tracked. */
struct span_opentelemetry {
	/** This span's own id, 16 hex characters. */
	char span_id[16];
	/** Span name. */
	const char *name;
	/**
	 * W3C traceparent of the parent context. All zero bytes
	 * when the span is root.
	 */
	char traceparent[55];
	/** Span kind. */
	enum span_kind kind;
	/** Whether the trace is sampled. */
	bool trace_flag;
	/** Span start time, unix time in nanoseconds. */
	int64_t start_time;
	/** Span end time, unix time in nanoseconds. */
	int64_t end_time;
};

/**
 * Initialize a span: store its name, own id, kind, and the parent
 * traceparent (all-zero traceparent means the span is root), then
 * record the start time.
 */
void
span_start(struct span_opentelemetry *span, const char *name,
	   char span_id[16], char traceparent[55], enum span_kind kind);

/** Record the span's end time. */
void
span_end(struct span_opentelemetry *span);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
