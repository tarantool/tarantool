#ifndef TARANTOOL_SWIM_EV_H_INCLUDED
#define TARANTOOL_SWIM_EV_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
struct ev_loop;
struct ev_timer;
struct ev_io;

/**
 * Similar to swim transport, the functions below are compile time
 * virtualized. Unit tests implement them in one way, and the
 * server in another.
 */

double
swim_time(void);

void
swim_ev_timer_start(struct ev_loop *loop, struct ev_timer *watcher);

void
swim_ev_timer_stop(struct ev_loop *loop, struct ev_timer *watcher);

#define swim_ev_init ev_init

#define swim_ev_timer_init ev_timer_init

#define swim_ev_timer_set ev_timer_set

#define swim_ev_io_start ev_io_start

#define swim_ev_io_stop ev_io_stop

#define swim_ev_io_set ev_io_set

#endif /* TARANTOOL_SWIM_EV_H_INCLUDED */
