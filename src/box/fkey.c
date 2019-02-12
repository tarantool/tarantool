/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include "fkey.h"
#include "sql.h"
#include "sql/sqlInt.h"

const char *fkey_action_strs[] = {
	/* [FKEY_ACTION_RESTRICT]    = */ "no_action",
	/* [FKEY_ACTION_SET_NULL]    = */ "set_null",
	/* [FKEY_ACTION_SET_DEFAULT] = */ "set_default",
	/* [FKEY_ACTION_CASCADE]     = */ "cascade",
	/* [FKEY_ACTION_NO_ACTION]   = */ "restrict"
};

const char *fkey_match_strs[] = {
	/* [FKEY_MATCH_SIMPLE]  = */ "simple",
	/* [FKEY_MATCH_PARTIAL] = */ "partial",
	/* [FKEY_MATCH_FULL]    = */ "full"
};

void
fkey_delete(struct fkey *fkey)
{
	sql_trigger_delete(sql_get(), fkey->on_delete_trigger);
	sql_trigger_delete(sql_get(), fkey->on_update_trigger);
	free(fkey->def);
	free(fkey);
}
