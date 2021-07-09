#pragma once
/*
 * Copyright 2021, Tarantool AUTHORS, please see AUTHORS file.
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

#include <c-dt/dt.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef SECS_PER_DAY
#define SECS_PER_DAY	86400
#define NANOS_PER_SEC	1000000000
#endif

/**
 * datetime structure consisting of:
 */
struct datetime_t {
	int secs;	///< seconds since epoch
	int nsec;	///< nanoseconds if any
	int offset;	///< offset in minutes from GMT
};

/**
 * Date/time delta structure
 */
struct t_datetime_duration {
	int secs;	///< relative seconds delta
	int nsec;	///< nanoseconds delta
};

int
datetime_compare(const struct datetime_t * lhs,
		 const struct datetime_t * rhs);


struct datetime_t *
datetime_unpack(const char **data, uint32_t len, struct datetime_t *date);

char *
datetime_pack(char *data, const struct datetime_t *date);

uint32_t
mp_sizeof_datetime(const struct datetime_t *date);

struct datetime_t *
mp_decode_datetime(const char **data, struct datetime_t *date);

char *
mp_encode_datetime(char *data, const struct datetime_t *date);

int
datetime_to_string(const struct datetime_t * date, char *buf, uint32_t len);

int
mp_snprint_datetime(char *buf, int size, const char **data, uint32_t len);

int
mp_fprint_datetime(FILE *file, const char **data, uint32_t len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

