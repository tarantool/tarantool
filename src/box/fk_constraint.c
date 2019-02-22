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
#include "fk_constraint.h"
#include "sql.h"
#include "sql/sqlInt.h"

const char *fk_constraint_action_strs[] = {
	/* [FKEY_ACTION_RESTRICT]    = */ "no_action",
	/* [FKEY_ACTION_SET_NULL]    = */ "set_null",
	/* [FKEY_ACTION_SET_DEFAULT] = */ "set_default",
	/* [FKEY_ACTION_CASCADE]     = */ "cascade",
	/* [FKEY_ACTION_NO_ACTION]   = */ "restrict"
};

const char *fk_constraint_match_strs[] = {
	/* [FKEY_MATCH_SIMPLE]  = */ "simple",
	/* [FKEY_MATCH_PARTIAL] = */ "partial",
	/* [FKEY_MATCH_FULL]    = */ "full"
};

void
fk_constraint_delete(struct fk_constraint *fk)
{
	sql_trigger_delete(sql_get(), fk->on_delete_trigger);
	sql_trigger_delete(sql_get(), fk->on_update_trigger);
	free(fk->def);
	free(fk);
}
