/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "tracer.h"
#include <string.h>
#include "clock.h"
#include "say.h"

void
start_span(struct span *span, const char *name, char span_id[SPAN_ID_LEN],
	   char traceparent[TRACEPARENT_LEN])
{
	span->name = xmalloc(strlen(name) + 1);
	memcpy(span->name, name, strlen(name) + 1);
	memcpy(span->span_id, span_id, sizeof(span->span_id));
	memcpy(span->traceparent, traceparent, sizeof(span->traceparent));
	span->start_time = clock_monotonic();
}

void
end_span(struct span *span)
{
	span->end_time = clock_monotonic();
}

int
write_span(struct span *span)
{
	say_info("TRACER: span_id=%.16s parent_id=%.16s name=%s "
		 "start_time=%.10f end_time=%.10f",
		 span->span_id, span->traceparent + 36, span->name,
		 span->start_time, span->end_time);
	return 0;
}
