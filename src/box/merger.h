#ifndef TARANTOOL_BOX_MERGER_H_INCLUDED
#define TARANTOOL_BOX_MERGER_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/* {{{ Structures */

struct tuple;
struct key_def;
struct tuple_format;

struct merge_source;

struct merge_source_vtab {
	/**
	 * Free a merge source.
	 *
	 * Don't call it directly, use merge_source_unref()
	 * instead.
	 */
	void (*destroy)(struct merge_source *base);
	/**
	 * Get a next tuple (refcounted) from a source.
	 *
	 * When format is not NULL the resulting tuple will be in
	 * a compatible format.
	 *
	 * When format is NULL it means that it does not matter
	 * for a caller in which format a tuple will be.
	 *
	 * Return 0 when successfully fetched a tuple or NULL. In
	 * case of an error set a diag and return -1.
	 */
	int (*next)(struct merge_source *base, struct tuple_format *format,
		    struct tuple **out);
};

/**
 * Base (abstract) structure to represent a merge source.
 *
 * The structure does not hold any resources.
 */
struct merge_source {
	/* Source-specific methods. */
	const struct merge_source_vtab *vtab;
	/* Reference counter. */
	int refs;
};

/* }}} */

/* {{{ Base merge source functions */

/**
 * Increment a merge source reference counter.
 */
static inline void
merge_source_ref(struct merge_source *source)
{
	++source->refs;
}

/**
 * Decrement a merge source reference counter. When it has
 * reached zero, free the source (call destroy() virtual method).
 */
static inline void
merge_source_unref(struct merge_source *source)
{
	assert(source->refs - 1 >= 0);
	if (--source->refs == 0)
		source->vtab->destroy(source);
}

/**
 * @see merge_source_vtab
 */
static inline int
merge_source_next(struct merge_source *source, struct tuple_format *format,
		  struct tuple **out)
{
	return source->vtab->next(source, format, out);
}

/**
 * Initialize a base merge source structure.
 */
static inline void
merge_source_create(struct merge_source *source, struct merge_source_vtab *vtab)
{
	source->vtab = vtab;
	source->refs = 1;
}

/* }}} */

/* {{{ Merger */

/**
 * Create a new merger.
 *
 * Return NULL and set a diag in case of an error.
 */
struct merge_source *
merger_new(struct key_def *key_def, struct merge_source **sources,
	   uint32_t source_count, bool reverse);

/* }}} */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_MERGER_H_INCLUDED */
