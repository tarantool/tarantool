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

#include "tracer_opentelemetry.h"
#include <string.h>
#include "clock.h"
#include <stdbool.h>

bool trace_opentelemetry_enabled = false;

void
trace_opentelemetry_set_enabled(bool enabled)
{
	trace_opentelemetry_enabled = enabled;
}

bool
trace_opentelemetry_get_enabled(void)
{
	return trace_opentelemetry_enabled;
}

void
span_start(struct span_opentelemetry *span, const char *name,
	   char span_id[16], char traceparent[55], enum span_kind kind)
{
	if (trace_opentelemetry_enabled) {
		span->name = name;
		memcpy(span->traceparent, traceparent, 55);
		memcpy(span->span_id, span_id, 16);
		span->kind = kind;
		span->start_time = clock_realtime64();
		span->trace_flag = 1;
	}
}

void
span_end(struct span_opentelemetry *span)
{
	span->end_time = clock_realtime64();
}
