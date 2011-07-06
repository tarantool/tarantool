#if !defined(REPLICATOR_H_INCLUDED)
#define REPLICATOR_H_INCLUDED
/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
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
#include <tarantool.h>
#include <util.h>

/**
 * Check replicator module configuration.
 *
 * @param config is tarantool checking configuration.
 *
 * @return On success, a zero is returned. On error, -1 is returned.
 */
u32
replicator_check_config(struct tarantool_cfg *config);

/**
 * Reload replicator module configuration.
 *
 * @param config is tarantool checking configuration.
 */
void
replicator_reload_config(struct tarantool_cfg *config);

/**
 * Pre-fork replicator spawner process.
 */
void
replicator_prefork();

/**
 * Intialize tarantool's replicator module.
 *
 * @return On success, a zero is returned. On error, -1 is returned.
 */
void
replicator_init();

#endif // !defined(REPLICATOR_H_INCLUDED)

