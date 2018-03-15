#ifndef INCLUDES_TARANTOOL_BOX_ALTER_H
#define INCLUDES_TARANTOOL_BOX_ALTER_H
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
#include "trigger.h"

extern struct trigger alter_space_on_replace_space;
extern struct trigger alter_space_on_replace_index;
extern struct trigger on_replace_truncate;
extern struct trigger on_replace_schema;
extern struct trigger on_replace_user;
extern struct trigger on_replace_func;
extern struct trigger on_replace_collation;
extern struct trigger on_replace_priv;
extern struct trigger on_replace_cluster;
extern struct trigger on_replace_sequence;
extern struct trigger on_replace_sequence_data;
extern struct trigger on_replace_space_sequence;
extern struct trigger on_replace_trigger;
extern struct trigger on_stmt_begin_space;
extern struct trigger on_stmt_begin_index;
extern struct trigger on_stmt_begin_truncate;

#endif /* INCLUDES_TARANTOOL_BOX_ALTER_H */
