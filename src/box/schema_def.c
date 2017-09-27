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
#include "schema_def.h"
#include <wchar.h>
#include <wctype.h>

static const char *object_type_strs[] = {
	/* [SC_UKNNOWN]         = */ "unknown",
	/* [SC_UNIVERSE]        = */ "universe",
	/* [SC_SPACE]           = */ "space",
	/* [SC_FUNCTION]        = */ "function",
	/* [SC_USER]            = */ "user",
	/* [SC_ROLE]            = */ "role",
	/* [SC_SEQUENCE]        = */ "sequence",
};

enum schema_object_type
schema_object_type(const char *name)
{
	/**
	 * There may be other places in which we look object type by
	 * name, and they are case-sensitive, so be case-sensitive
	 * here too.
	 */
	int n_strs = sizeof(object_type_strs)/sizeof(*object_type_strs);
	int index = strindex(object_type_strs, name, n_strs);
	return (enum schema_object_type) (index == n_strs ? 0 : index);
}

const char *
schema_object_name(enum schema_object_type type)
{
	return object_type_strs[type];
}

bool
identifier_is_valid(const char *str, uint32_t str_len)
{
	mbstate_t state;
	memset(&state, 0, sizeof(state));
	wchar_t w;
	ssize_t len = mbrtowc(&w, str, str_len, &state);
	if (len <= 0)
		return false; /* invalid character or zero-length string */
	if (!iswalpha(w) && w != L'_')
		return false; /* fail to match [a-zA-Z_] */

	while (str_len > 0 && (len = mbrtowc(&w, str, str_len, &state)) > 0) {
		if (!iswalnum(w) && w != L'_')
			return false; /* fail to match [a-zA-Z0-9_]* */
		str_len -= len;
		str += len;
	}

	if (len < 0)
		return false; /* invalid character  */

	return true;
}
