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

#ifndef TARANTOOL_IFC_H_INCLUDED
#define TARANTOOL_IFC_H_INCLUDED

#include <tarantool_ev.h>


/**
@brief CHANNELS
*/

struct ipc_channel;

/**
@brief allocator
@param size
@return malloced channel (or NULL)
@code
	struct ipc_channel *ch = ipc_channel_alloc(10);
@endcode
*/
struct ipc_channel *ipc_channel_alloc(unsigned size);

/**
@brief init channel
@param channel
@code
	struct ipc_channel *ch = ipc_channel_alloc(10);
	ipc_channel_init(ch);
@endcode
*/
void ipc_channel_init(struct ipc_channel *ch);

/**
@brief put data into channel
@detail lock current fiber if channel is full
@param channel
@param data
@code
	ipc_channel_put(ch, "message");
@endcode
*/
void ipc_channel_put(struct ipc_channel *ch, void *data);

/**
@brief get data from channel
@detail lock current fiber if channel is empty
@param channel
@return data that was put into channel by ipc_channel_put
@code
	char *msg = ipc_channel_get(ch);
@endcode
*/
void *ipc_channel_get(struct ipc_channel *ch);

/**
@brief wake up all fibers that sleep by ipc_channel_get and send message to them
@param channel
@param data
@return count of fibers received the message
*/
int ipc_channel_broadcast(struct ipc_channel *ch, void *data);

/**
@brief check if channel is empty
@param channel
@return 1 (TRUE) if channel is empty
@return 0 otherwise
@code
	if (!ipc_channel_isempty(ch))
		char *msg = ipc_channel_get(ch);
@endcode
*/
int ipc_channel_isempty(struct ipc_channel *ch);

/**
@brief check if channel is full
@param channel
@return 1 (TRUE) if channel is full
@return 0 otherwise
@code
	if (!ipc_channel_isfull(ch))
		ipc_channel_put(ch, "message");
@endcode
*/

int ipc_channel_isfull(struct ipc_channel *ch);

/**
@brief put data into channel in timeout
@param channel
@param data
@param timeout
@return 0 if success
@return ETIMEDOUT if timeout exceeded
@code
	if (ipc_channel_put_timeout(ch, "message", 0.25) == 0)
		return "ok";
	else
		return "timeout exceeded";
@endcode
*/
int
ipc_channel_put_timeout(struct ipc_channel *ch,	void *data, ev_tstamp timeout);

/**
@brief get data into channel in timeout
@param channel
@param timeout
@return data if success
@return NULL if timeout exceeded
@code
	do {
		char *msg = ipc_channel_get_timeout(ch, 0.5);
		printf("message: %p\n", msg);
	} until(msg);
	return msg;
@endcode
*/
void *ipc_channel_get_timeout(struct ipc_channel *ch, ev_tstamp timeout);


/**
@brief return true if channel has reader fibers that wait data
@param channel
*/
int ipc_channel_has_readers(struct ipc_channel *ch);

/**
@brief return true if channel has writer fibers that wait data
@param channel
*/
int ipc_channel_has_writers(struct ipc_channel *ch);

#endif /* TARANTOOL_IFC_H_INCLUDED */

