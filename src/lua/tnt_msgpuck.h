#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

/**
 * This header contains a list of functions used to work with
 * additional data types (defined in tarantool) in the msgpack
 * format. Intended for use in tests to avoid copy-pasting
 * function declarations.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include "mp_decimal.h"
#include "box/mp_error.h"
#include "uuid/mp_uuid.h"

char *
tnt_mp_encode_decimal(char *data, const decimal_t *dec);

uint32_t
tnt_mp_sizeof_decimal(const decimal_t *dec);

char *
tnt_mp_encode_uuid(char *data, const struct tt_uuid *uuid);

uint32_t
tnt_mp_sizeof_uuid(void);

char *
tnt_mp_encode_error(char *data, const struct error *error);

uint32_t
tnt_mp_sizeof_error(const struct error *error);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
