/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef TARANUL_H
#define TARANUL_H

#include <tbuf.h>
#include <util.h>
#include <log_io.h>
#include TARANTOOL_CONFIG

struct recovery_state *recovery_state;
void mod_init(void);
i32 mod_chkconfig(struct tarantool_cfg *conf);
void mod_reloadconfig(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf);
int mod_cat(const char *filename);
void mod_snapshot(struct log_io_iter *);
void mod_info(struct tbuf *out);
void mod_exec(char *str, int len, struct tbuf *out);

extern struct tarantool_module module;
extern struct tarantool_cfg cfg;
extern struct tbuf *cfg_out;
extern char *cfg_filename;
extern bool init_storage;
i32 reload_cfg(struct tbuf *out);
void snapshot(void *ev __unused__, int events __unused__);
const char *tarantool_version(void);
void tarantool_info(struct tbuf *out);
double tarantool_uptime(void);

char **init_set_proc_title(int argc, char **argv);
void set_proc_title(const char *format, ...);

enum tarantool_role { usage, cat, def, chkconfig };
extern enum tarantool_role role;

#endif
