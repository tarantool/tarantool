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

#include "coll_id_def.h"

static int64_t
icu_on_off_from_str(const char *str, uint32_t len)
{
	return strnindex(coll_icu_on_off_strs + 1, str, len,
			 coll_icu_on_off_MAX - 1) + 1;
}

static int64_t
icu_alternate_handling_from_str(const char *str, uint32_t len)
{
	return strnindex(coll_icu_alternate_handling_strs + 1, str, len,
			 coll_icu_alternate_handling_MAX - 1) + 1;
}

static int64_t
icu_case_first_from_str(const char *str, uint32_t len)
{
	return strnindex(coll_icu_case_first_strs + 1, str, len,
			 coll_icu_case_first_MAX - 1) + 1;
}

static int64_t
icu_strength_from_str(const char *str, uint32_t len)
{
	return strnindex(coll_icu_strength_strs + 1, str, len,
			 coll_icu_strength_MAX - 1) + 1;
}

const struct opt_def coll_icu_opts_reg[] = {
	OPT_DEF_ENUM("french_collation", coll_icu_on_off, struct coll_icu_def,
		     french_collation, icu_on_off_from_str),
	OPT_DEF_ENUM("alternate_handling", coll_icu_alternate_handling, struct coll_icu_def,
		     alternate_handling, icu_alternate_handling_from_str),
	OPT_DEF_ENUM("case_first", coll_icu_case_first, struct coll_icu_def,
		     case_first, icu_case_first_from_str),
	OPT_DEF_ENUM("case_level", coll_icu_on_off, struct coll_icu_def,
		     case_level, icu_on_off_from_str),
	OPT_DEF_ENUM("normalization_mode", coll_icu_on_off, struct coll_icu_def,
		     normalization_mode, icu_on_off_from_str),
	OPT_DEF_ENUM("strength", coll_icu_strength, struct coll_icu_def,
		     strength, icu_strength_from_str),
	OPT_DEF_ENUM("numeric_collation", coll_icu_on_off, struct coll_icu_def,
		     numeric_collation, icu_on_off_from_str),
	OPT_END,
};
