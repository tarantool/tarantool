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

#include "msgpuck.h"
#include "trivia/util.h"
#include "mp_datetime.h"
#include "mp_decimal.h"
#include "box/mp_error.h"
#include "mp_uuid.h"

EXPORT_ALIAS(mp_encode_float, tnt_mp_encode_float);
EXPORT_ALIAS(mp_encode_double, tnt_mp_encode_double);
EXPORT_ALIAS(mp_decode_float, tnt_mp_decode_float); 
EXPORT_ALIAS(mp_decode_double, tnt_mp_decode_double);

EXPORT_ALIAS(mp_decode_extl, tnt_mp_decode_extl);
EXPORT_ALIAS(mp_encode_decimal, tnt_mp_encode_decimal);
EXPORT_ALIAS(mp_sizeof_decimal, tnt_mp_sizeof_decimal);
EXPORT_ALIAS(mp_encode_uuid, tnt_mp_encode_uuid);
EXPORT_ALIAS(mp_sizeof_uuid, tnt_mp_sizeof_uuid);
EXPORT_ALIAS(mp_encode_error, tnt_mp_encode_error);
EXPORT_ALIAS(mp_sizeof_error, tnt_mp_sizeof_error);
EXPORT_ALIAS(mp_encode_datetime, tnt_mp_encode_datetime);
EXPORT_ALIAS(mp_sizeof_datetime, tnt_mp_sizeof_datetime);
