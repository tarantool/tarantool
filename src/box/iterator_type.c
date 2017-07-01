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
#include "iterator_type.h"
#include "trivia/util.h"

const char *iterator_type_strs[] = {
	/* [ITER_EQ]  = */ "EQ",
	/* [ITER_REQ]  = */ "REQ",
	/* [ITER_ALL] = */ "ALL",
	/* [ITER_LT]  = */ "LT",
	/* [ITER_LE]  = */ "LE",
	/* [ITER_GE]  = */ "GE",
	/* [ITER_GT]  = */ "GT",
	/* [ITER_BITS_ALL_SET] = */ "BITS_ALL_SET",
	/* [ITER_BITS_ANY_SET] = */ "BITS_ANY_SET",
	/* [ITER_BITS_ALL_NOT_SET] = */ "BITS_ALL_NOT_SET",
	/* [ITER_OVERLAPS] = */ "OVERLAPS",
	/* [ITER_NEIGHBOR] = */ "NEIGHBOR",
};

static_assert(sizeof(iterator_type_strs) / sizeof(const char *) ==
	iterator_type_MAX, "iterator_type_str constants");
