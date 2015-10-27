#ifndef TARANTOOL_PROCESS_TITLE_H_INCLUDED
#define TARANTOOL_PROCESS_TITLE_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
 *
 * script.lua/running (tarantool): my lovely pony
 *
 *            ^^^^^^^              ^^^^^^^^^^^^^^
 * ^^^^^^^^^^ status   ^^^^^^^^^    custom title
 * script name         interpretor name
 *
 *
 * Parts missing:
 *
 * 1) no custom title
 *
 * script.lua/running (tarantool)
 *
 * 2) script name missing
 *
 * tarantool/running: my lovely pony
 *
 * 3) scriptname.matches(tarantool.*)
 *
 * tarantoolctl/running: my lovely pony
 *
 * 4) no status
 *
 * script.lua (tarantool): my lovely pony
 */
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Prepares for customizing process title but doesn't change the
 * title yet.  Creates and returns a copy of argv if necessary, may
 * relocate environ as well.
 *
 * On Linux customized title is writen on top of argv/environ memory block.
 */
char **process_title_init(int argc, char **argv);

void process_title_free(int argc, char **argv);

/** generate and update process title */
void process_title_update();

/** query current title */
const char *process_title_get();

/* parts: invoke process_title_update() to propagate changes */

/* interpretor name */
void process_title_set_interpretor_name(const char *name);
const char *process_title_get_interpretor_name();
/* script name */
void process_title_set_script_name(const char *name);
const char *process_title_get_script_name();
/* custom */
void process_title_set_custom(const char *);
const char *process_title_get_custom();
/* status */
void process_title_set_status(const char *);
const char *process_title_get_status();

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* TARANTOOL_PROCESS_TITLE_H_INCLUDED */
