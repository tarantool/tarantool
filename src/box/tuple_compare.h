#ifndef TARANTOOL_BOX_TUPLE_COMPARE_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_COMPARE_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct key_def;

/**
 * Hints are now used for two purposes - passing the index of the
 * key used in the case of multikey index and to speed up the
 * comparators.
 *
 * Scenario I. Pass the multikey index of the key to comparator.
 * In the case of multikey index arises an ambiguity: which key
 * should be used in the comparison. Hints act as an non-negative
 * numeric index of key to use.
 *
 * Scenario II. Speed up comparators.
 * Tuple comparison hint h(t) is such a function of tuple t that
 * the following conditions always hold for any pair of tuples
 * t1 and t2:
 *
 *   if h(t1) < h(t2) then t1 < t2;
 *   if h(t1) > h(t2) then t1 > t2;
 *   if h(t1) == h(t2) then t1 may or may not be equal to t2.
 *
 * These rules mean that instead of direct tuple vs tuple
 * (or tuple vs key) comparison one may compare their hints
 * first and only if theirs hints equal compare the tuples
 * themselves.
 */
typedef uint64_t hint_t;

/**
 * Reserved value to use when comparison hint is undefined.
 */
#define HINT_NONE ((hint_t)UINT64_MAX)

/**
 * Initialize comparator functions for the key_def.
 * @param key_def key definition
 */
void
key_def_set_compare_func(struct key_def *def);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_COMPARE_H_INCLUDED */
