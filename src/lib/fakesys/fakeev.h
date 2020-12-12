#pragma once
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

/**
 * Fakeev implements a 'fake' event loop with bogus clock to speed up events
 * processing while keeping the real code unaware that it works in a simulation.
 * Libev is used a little, just to store some IO events.
 *
 * The test event loop works as follows. It has a global watch and
 * a heap of events sorted by deadlines. An event is either a
 * libev event like EV_TIMER, or an internal test event.
 *
 * On each iteration it takes all the next events with the nearest
 * and equal deadline, and sets the global watch with the deadline
 * value. It simulates time flow. All the events with that
 * deadline are processed. An event processing usually means
 * calling a libev callback set by real code beforehand.
 *
 * For example, if event deadlines and the watch are:
 *
 *     watch = 0
 *     queue = [1, 1, 1, 5, 5, 6, 7, 7, 7]
 *
 * Then the queue is dispatched as follows:
 *
 *     1) watch = 1
 *        process first 3 events
 *        queue = [5, 5, 6, 7, 7, 7]
 *
 *     2) watch = 5
 *        process next 2 events
 *        queue = [6, 7, 7, 7]
 *
 *     3) watch = 6
 *        process a next event
 *        queue = [7, 7, 7]
 *
 *     4) watch = 7
 *        process next 3 events
 *        queue = []
 *
 * The loop provides an API to make one iteration, do one loop
 * step. For example, the sequence above is played in 4 loop
 * steps. The unit tests can either do explicitly step by step,
 * calling that API method. Or use wrappers with 'timeouts', which
 * in fact do the same, but until the global watch equals a
 * certain value. Usually after each loop step a test checks some
 * conditions.
 */

/** Initialize test event processing system. */
void
fakeev_init(void);

/** Destroy test event processing system, free resources. */
void
fakeev_free(void);

/** Implementation of global time visible as real time in the tested code. */
double
fakeev_time(void);

/** Global loop used by the fake events. */
struct ev_loop *
fakeev_loop(void);

/** Emulator of ev_timer_start(). */
void
fakeev_timer_start(struct ev_loop *loop, struct ev_timer *base);

/** Emulator of raft_ev_timer_remaining(). */
double
fakeev_timer_remaining(struct ev_loop *loop, struct ev_timer *base);

/** Emulator of ev_timer_again(). */
void
fakeev_timer_again(struct ev_loop *loop, struct ev_timer *base);

/** Emulator of ev_timer_stop(). */
void
fakeev_timer_stop(struct ev_loop *loop, struct ev_timer *base);

/**
 * Stop the event loop after @a delay fake seconds. It does not
 * affect other events, so the loop can stop earlier multiple
 * times.
 */
void
fakeev_set_brk(double delay);

/** Play one step of event loop, process generated events. */
void
fakeev_loop_update(struct ev_loop *loop);

/** Destroy pending events, reset global watch. */
void
fakeev_reset(void);
