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
#include "tarantool/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tarantool_cfg;
struct tbuf;

extern int snapshot_pid;
extern struct tarantool_cfg cfg;
extern const char *cfg_filename;
extern char *cfg_filename_fullpath;
extern char *custom_proc_title;
int reload_cfg();
extern char status[];
void show_cfg(struct tbuf *out);
int snapshot(void);
const char *tarantool_version(void);
/**
 * Get version (defined in PACKAGE_VERSION), packed into uint32_t
 * The highest byte or result means major version, next - minor,
 * middle - patch, last - revision.
 */
uint32_t tarantool_version_id(void);

double tarantool_uptime(void);
void tarantool_free(void);

void __attribute__((format (printf, 2, 3)))
title(const char *role, const char *fmt, ...);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_H_INCLUDED */
