#ifndef TARANTOOL_H_INCLUDED
#define TARANTOOL_H_INCLUDED
/*
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
#include <stdbool.h>
#include <util.h>

struct tarantool_cfg;
struct tbuf;
struct log_io;
struct fio_batch;

void mod_init(void);
void mod_free(void);

extern const char *mod_name;
i32 mod_check_config(struct tarantool_cfg *conf);
i32 mod_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf);
int mod_cat(const char *filename);
void mod_snapshot(struct log_io *, struct fio_batch *batch);
void mod_info(struct tbuf *out);
const char *mod_status(void);

extern struct tarantool_cfg cfg;
extern const char *cfg_filename;
extern char *cfg_filename_fullpath;
extern bool init_storage, booting;
extern char *binary_filename;
extern char *custom_proc_title;
i32 reload_cfg(struct tbuf *out);
int snapshot(void * /* ev */, int /* events */);
const char *tarantool_version(void);
double tarantool_uptime(void);
void tarantool_free(void);

char **init_set_proc_title(int argc, char **argv);
void free_proc_title(int argc, char **argv);
void set_proc_title(const char *format, ...);
#endif /* TARANTOOL_H_INCLUDED */
