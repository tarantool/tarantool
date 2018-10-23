#ifndef TARANTOOL_SWIM_TEST_TRANSPORT_H_INCLUDED
#define TARANTOOL_SWIM_TEST_TRANSPORT_H_INCLUDED
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

/**
 * SWIM test_transport implements a 'fake' file descriptors table
 * in user space in orider to get the full control over UDP
 * sockets, used in core SWIM code. Fake fds are used to provide a
 * capability to set necessary loss level, delay, reorders.
 */

/**
 * Feed EV_WRITE event to all opened descriptors, and EV_READ to
 * ones, who have not empty recv queue. Move packets from send to
 * recv queues. No callbacks is invoked. Only events are fed.
 */
void
swim_transport_do_loop_step(struct ev_loop *loop);

/**
 * Block a file descriptor so as it can not receive nor send any
 * packets.
 */
void
swim_test_transport_block_fd(int fd);

/** Unblock a file descriptor. */
void
swim_test_transport_unblock_fd(int fd);

/** Initialize test transport system. */
void
swim_test_transport_init(void);

/** Destroy test transport system, free resources. */
void
swim_test_transport_free(void);

#endif /* TARANTOOL_SWIM_TEST_TRANSPORT_H_INCLUDED */
