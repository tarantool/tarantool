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
#include <stdbool.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct ev_loop;
struct ifaddrs;

/**
 * Fakenet implements a 'fake' file descriptors table in user space in order to
 * get the full control over UDP sockets. Fake fds are used to provide means
 * to set necessary loss level, delay, reorders, blocks.
 */

/**
 * Emulator of sendto().
 * Save data to the socket output buffer. The buffer is flushed on the next loop
 * update.
 */
ssize_t
fakenet_sendto(int fd, const void *data, size_t size,
	       const struct sockaddr *addr, socklen_t addr_size);

/**
 * Emulator of recvfrom().
 * Try to read the input buffer of the socket.
 */
ssize_t
fakenet_recvfrom(int fd, void *buffer, size_t size, struct sockaddr *addr,
		 socklen_t *addr_size);

/**
 * Emulator of socket() + bind() + close().
 * Fake bind will close the old descriptor, create a new one, and bind it to the
 * given address atomically. So it either does all that successfully, or nothing
 * of that. The created socket is connection-less and packet-oriented.
 */
int
fakenet_bind(int *fd, const struct sockaddr *addr, socklen_t addr_len);

/**
 * Emulator of close().
 * Should be called only on what was previously created via fakenet bind().
 */
void
fakenet_close(int fd);

/** Emulator of getifaddrs(). */
int
fakenet_getifaddrs(struct ifaddrs **ifaddrs);

/** Emulator of freeifaddrs(). */
void
fakenet_freeifaddrs(struct ifaddrs *ifaddrs);

/**
 * Signature of a packet filter function. It takes packet data,
 * arbitrary user data, and should return true, if the packet
 * should be dropped. False otherwise. Direction is said via
 * @a dir parameter. 0 means incoming packet, 1 means outgoing
 * packet, just like standard IO descriptors. Via @a peer_fd
 * parameter a sender/receiver descriptor number is passed
 * depending on @a dir.
 */
typedef bool (*fakenet_filter_check_f)(const char *data, int size, void *udata,
				       int dir, int peer_fd);

/**
 * Until there are no new IO events, feed EV_WRITE event to all
 * opened descriptors; EV_READ to ones, who have not empty recv
 * queue; invoke callbacks to process the events. Move packets
 * from send to recv queues.
 */
void
fakenet_loop_update(struct ev_loop *loop);

/**
 * Block a file descriptor so as it can not receive nor send any
 * packets.
 */
void
fakenet_block(int fd);

/** Unblock a file descriptor. */
void
fakenet_unblock(int fd);

/**
 * Add a filter to the file descriptor @a fd. If a filter with
 * the same @a check function exists, then it is deleted and a
 * new one is created.
 * @param fd File descriptor to add filter to.
 * @param check Check function. It is called for each packet and
 *        should return true, when the packet should be dropped.
 * @param udata Arbitrary user data, passed to each @a check
 *        invocation.
 */
void
fakenet_add_filter(int fd, fakenet_filter_check_f check, void *udata);

/**
 * Remove a filter having @a check function. Works just like the
 * core triggers library. The found filter is deleted. If nothing
 * is found, then it is not an error.
 */
void
fakenet_remove_filter(int fd, fakenet_filter_check_f check);

/** Initialize fake network system. */
void
fakenet_init(void);

/** Destroy fake network system, free resources. */
void
fakenet_free(void);
