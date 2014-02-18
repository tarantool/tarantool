#ifndef TARANTOOL_IPC_H_INCLUDED
#define TARANTOOL_IPC_H_INCLUDED
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
#include <tarantool_ev.h>

/**
 * @brief CHANNELS
 */

struct ipc_channel;

/**
 * @brief Allocate and construct new IPC channel
 * @param size of channel
 * @return new channel
 * @code
 *	struct ipc_channel *ch = ipc_channel_new(10);
 * @endcode
 */
struct ipc_channel *
ipc_channel_new(unsigned size);

/**
 * @brief Destruct and free a IPC channel
 * @param ch channel
 */
void
ipc_channel_delete(struct ipc_channel *ch);

/**
 * @brief Put data into a channel.
 * @detail Yield current fiber if the channel is full.
 * @param channel
 * @param data
 * @code
 *	ipc_channel_put(ch, "message");
 * @endcode
 */
void
ipc_channel_put(struct ipc_channel *ch, void *data);

/**
 * @brief Get data from a channel.
 * @detail Yield current fiber if the channel is empty.
 * @param channel
 * @return data that was put into channel by ipc_channel_put
 * @code
 *	char *msg = ipc_channel_get(ch);
 * @endcode
 */
void *
ipc_channel_get(struct ipc_channel *ch);

/**
 * @brief Wake up all fibers that sleep in ipc_channel_get and
 * send a message to them.
 * @param channel
 * @param data
 * @return count of fibers to which the message was delivered
 */
int
ipc_channel_broadcast(struct ipc_channel *ch, void *data);

/**
 * @brief check if channel is empty
 * @param channel
 * @retval 1 (TRUE) if channel is empty
 * @retval 0 otherwise
 * @code
 *	if (!ipc_channel_is_empty(ch))
 *		char *msg = ipc_channel_get(ch);
 * @endcode
 */
bool
ipc_channel_is_empty(struct ipc_channel *ch);

/**
 * @brief check if channel is full
 * @param channel
 * @return 1 (TRUE) if channel is full
 * @return 0 otherwise
 * @code
 *	if (!ipc_channel_is_full(ch))
 *		ipc_channel_put(ch, "message");
 * @endcode
 */
bool
ipc_channel_is_full(struct ipc_channel *ch);

/**
 * @brief put data into channel in timeout
 * @param channel
 * @param data
 * @param timeout
 * @return 0 if success
 * @return -1, errno=ETIMEDOUT if timeout exceeded
 * @code
 *	if (ipc_channel_put_timeout(ch, "message", 0.25) == 0)
 *		return "ok";
 *	else
 *		return "timeout exceeded";
 * @endcode
 */
int
ipc_channel_put_timeout(struct ipc_channel *ch,	void *data,
			ev_tstamp timeout);

/**
 * @brief Get data from a channel with a timeout
 * @param channel
 * @param timeout
 * @return data if success
 * @return NULL if timeout exceeded
 * @code
 *	do {
 *		char *msg = ipc_channel_get_timeout(ch, 0.5);
 *		printf("message: %p\n", msg);
 *	} while (msg);
 *	return msg;
 * @endcode
 */
void *
ipc_channel_get_timeout(struct ipc_channel *ch, ev_tstamp timeout);

/**
 * @brief return true if channel has reader fibers that wait data
 * @param channel
 */
bool
ipc_channel_has_readers(struct ipc_channel *ch);

/**
 * @brief return true if channel has writer fibers that wait data
 * @param channel
 */
bool
ipc_channel_has_writers(struct ipc_channel *ch);

/**
 * @brief close the channel. Wake up readers and writers (if they exist)
 */
void
ipc_channel_close(struct ipc_channel *ch);

/**
 * @brief return true if the channel is closed
 */
bool
ipc_channel_is_closed(struct ipc_channel *ch);

#endif /* TARANTOOL_IPC_H_INCLUDED */
