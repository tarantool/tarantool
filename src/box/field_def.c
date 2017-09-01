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

#include "field_def.h"
#include "trivia/util.h"

const char *field_type_strs[] = {
	/* [FIELD_TYPE_ANY]      = */ "any",
	/* [FIELD_TYPE_UNSIGNED] = */ "unsigned",
	/* [FIELD_TYPE_STRING]   = */ "string",
	/* [FIELD_TYPE_ARRAY]    = */ "array",
	/* [FIELD_TYPE_NUMBER]   = */ "number",
	/* [FIELD_TYPE_INTEGER]  = */ "integer",
	/* [FIELD_TYPE_SCALAR]   = */ "scalar",
	/* [FIELD_TYPE_STR17]    = */ "str",
	/* [FIELD_TYPE_NUM17]    = */ "num",
};

enum field_type
field_type_by_name(const char *name)
{
	enum field_type field_type = STR2ENUM(field_type, name);
	/* 'num' and 'str' in _index are deprecated since Tarantool 1.7 */
	if (field_type == FIELD_TYPE_STR17)
		return FIELD_TYPE_STRING;
	if (field_type == FIELD_TYPE_NUM17)
		return FIELD_TYPE_UNSIGNED;
	/*
	 * FIELD_TYPE_ANY can't be used as type of indexed field,
	 * because it is internal type used only for filling
	 * struct tuple_format.fields array.
	 */
	if (field_type != field_type_MAX && field_type != FIELD_TYPE_ANY)
		return field_type;
	return field_type_MAX;
}
