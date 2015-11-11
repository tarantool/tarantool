#ifndef TARANTOOL_IPC_H_INCLUDED
#define TARANTOOL_IPC_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdint.h>
#include <tarantool_ev.h>
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * @brief CHANNELS
 */

/**
 * A base structure for an IPC message.
 *
 * A message at any moment can be either:
 * - new
 * - in a channel, waiting to get delivered
 * - delivered
 *
 * When a channel is destroyed, all messages buffered by the
 * channel must be destroyed as well. The destroy callback is
 * therefore necessary to free any message-specific resources in
 * case of delivery failure.
 */
struct ipc_msg {
	void (*destroy)(struct ipc_msg *msg);
};

/**
 * A message implementation to pass simple value across
 * a channel.
 */
struct ipc_value {
	struct ipc_msg base;
	union {
		void *data;
		int i;
	};
};

void
ipc_value_delete(struct ipc_msg *msg);

struct ipc_value *
ipc_value_new();


/**
 * Channel - fiber communication media.
 *
 * A channel is a media to deliver messages between fibers.
 * Any fiber can read or write to/from a channel. Many
 * readers and writers can work with a channel concurrently.
 * A message sent to a channel is ready by the first fiber
 * reading from it. If a channel is empty, the reader blocks
 * and waits for a message. If a channel has no reader, the
 * writer waits for the reader to arrive. If a channel is
 * buffered, i.e. has an associated buffer for messages, it
 * is possible for a writer to "drop" the message in a channel
 * until a writer arrives. In case of multiple readers,
 * messages are delivered in FIFO order. In case of multiple
 * writers, the first writer to come is released of its message
 * first.
 *
 * If a channel has a buffer of size N, and the buffer
 * is full (contains N messages), and there is a queue of writers,
 * the moment the first reader arrives and reads the first message
 * from a buffer, the first fiber from the wait queue is awoken,
 * and puts its message to the end of the buffer.
 *
 * A channel, once created is "open". I.e. anyone can read or
 * write to/from a channel. A channel can be closed at any time,
 * in which case, all messages currently buffered in a channel
 * are destroyed, waiting readers or writers awoken with an error.
 *
 * Waiting for a message, a reader, or space in a buffer can also
 * return error in case of a wait timeout or cancellation (when the
 * waiting fiber is cancelled).
 *
 * Sending a message to a closed channel, as well as reading
 * a message from such channel, always fails.
 *
 * Channel memory layout
 * ---------------------
 * Channel structure has a fixed size. If a channel is created
 * with a buffer, the buffer must be allocated in a continuous
 * memory chunk, directly after the channel itself.
 * ipc_channel_memsize() can be used to find out the amount
 * of memory necessary to store a channel, given the desired
 * buffer size.
 */
struct ipc_channel {
	/** Channel buffer size, if the channel is buffered. */
	uint32_t size;
	/** The number of messages in the buffer. */
	uint32_t count;
	/**
	 * Readers blocked waiting for messages while the channel
	 * buffers is empty and/or there are no writers, or
	 * Writers blocked waiting for empty space while the
	 * channel buffer is full and/or there are no readers.
	 */
	struct rlist waiters;
	/** Ring buffer read position. */
	uint32_t beg;
	/* True if the channel is closed. */
	bool is_closed;
	/** Channel buffer, if any. */
	struct ipc_msg **buf;
};

/**
 * The amount of memory necessary to store a channel, given
 * buffer size.
 */
static inline size_t
ipc_channel_memsize(uint32_t size)
{
	return sizeof(struct ipc_channel) + sizeof(struct ipc_msg *) * size;
}

/**
 * Initialize a channel (the memory should have
 * been correctly allocated for the channel).
 */
void
ipc_channel_create(struct ipc_channel *ch, uint32_t size);

/** Destroy a channel. Does not free allocated memory. */
void
ipc_channel_destroy(struct ipc_channel *ch);

/**
 * Allocate and construct a channel.
 *
 * Uses malloc().
 *
 * @param	size of the channel buffer
 * @return	new channel
 * @code
 *	struct ipc_channel *ch = ipc_channel_new(10);
 * @endcode
 */
struct ipc_channel *
ipc_channel_new(uint32_t size);

/**
 * Destroy and free an IPC channel.
 *
 * @param ch	channel
 */
void
ipc_channel_delete(struct ipc_channel *ch);

/**
 * Check if the channel buffer is empty.
 *
 * @param channel
 *
 * @retval true		channel buffer is empty
 *			(always true for unbuffered
 *			channels)
 * @retval false	otherwise
 *
 * @code
 *	if (!ipc_channel_is_empty(ch))
 *		ipc_channel_get(ch, ...);
 * @endcode
 */
static inline bool
ipc_channel_is_empty(struct ipc_channel *ch)
{
	return ch->count == 0;
}

/**
 * Check if the channel buffer is full.
 *
 * @param	channel
 *
 * @return true		if the channel buffer is full
 *			(always true for  unbuffered channels)
 *
 * @return false	otherwise
 * @code
 *	if (!ipc_channel_is_full(ch))
 *		ipc_channel_put(ch, "message");
 * @endcode
 */
static inline bool
ipc_channel_is_full(struct ipc_channel *ch)
{
	return ch->count >= ch->size;
}

/**
 * Put a message into a channel.
 * This is for cases when messages need to have
 * a custom destructor.
 */
int
ipc_channel_put_msg_timeout(struct ipc_channel *ch,
			    struct ipc_msg *msg,
			    ev_tstamp timeout);

/**
 * Send a message over a channel within given time.
 *
 * @param	channel
 * @param	msg
 * @param	timeout
 * @return 0	success
 * @return -1, errno=ETIMEDOUT if timeout exceeded,
 *	       errno=ECANCEL if the fiber is cancelled
 *	       errno=EBADF if the channel is closed
 *	       while waiting on it.
 *
 */
int
ipc_channel_put_timeout(struct ipc_channel *ch,
			void *data,
			ev_tstamp timeout);

/**
 * Send a message over a channel.
 *
 * Yields current fiber if the channel is full.
 * The message does not require a custom
 * destructor.
 *
 * @param channel
 * @param data
 *
 * @code
 *	ipc_channel_put(ch, "message");
 * @endcode
 * @return  -1 if the channel is closed
 */
static inline int
ipc_channel_put(struct ipc_channel *ch, void *data)
{
	return ipc_channel_put_timeout(ch, data, TIMEOUT_INFINITY);
}

/**
 * Get a message from the channel, or time out.
 * The caller is responsible for message destruction.
 */
int
ipc_channel_get_msg_timeout(struct ipc_channel *ch,
			    struct ipc_msg **msg,
			    ev_tstamp timeout);
/**
 * Get data from a channel within given time.
 *
 * @param channel
 * @param timeout
 *
 * @return	0 on success, -1 on error (timeout, channel is
 *		closed)
 * @code
 *	do {
 *		struct ipc_msg *msg;
 *		int rc = ipc_channel_get_timeout(ch, 0.5, );
 *		printf("message: %p\n", msg);
 *	} while (msg);
 * @endcode
 */
int
ipc_channel_get_timeout(struct ipc_channel *ch,
			void **data,
			ev_tstamp timeout);

/**
 * Fetch a message from the channel. Yields current fiber if the
 * channel is empty.
 *
 * @param channel
 * @return 0 on success, -1 on error
 */
static inline int
ipc_channel_get(struct ipc_channel *ch, void **data)
{
	return ipc_channel_get_timeout(ch, data, TIMEOUT_INFINITY);
}

/**
 * Check if the channel has reader fibers that wait
 * for new messages.
 */
bool
ipc_channel_has_readers(struct ipc_channel *ch);

/**
 * Check if the channel has writer fibers that wait
 * for readers.
 */
bool
ipc_channel_has_writers(struct ipc_channel *ch);

/** Channel buffer size. */
static inline uint32_t
ipc_channel_size(struct ipc_channel *ch)
{
	return ch->size;
}

/**
 * The number of messages in the buffer.
 * There may be more messages outstanding
 * if the buffer is full.
 */
static inline uint32_t
ipc_channel_count(struct ipc_channel *ch)
{
	return ch->count;
}

/**
 * Close the channel. Discards all messages
 * and wakes up all readers and writers.
 */
void
ipc_channel_close(struct ipc_channel *ch);

/**
 * True if the channel is closed for both for reading
 * and writing.
 */
static inline bool
ipc_channel_is_closed(struct ipc_channel *ch)
{
	return ch->is_closed;
}

#if defined(__cplusplus)
} /* extern "C" */

#include "fiber.h"

struct IpcChannelGuard {
	struct ipc_channel *ch;

	IpcChannelGuard(uint32_t size) {
		ch = ipc_channel_new(size);
		if (ch == NULL)
			diag_raise();
	}

	~IpcChannelGuard() {
		ipc_channel_delete(ch);
	}
};

static inline void
ipc_channel_get_xc(struct ipc_channel *ch, void **data)
{
	if (ipc_channel_get(ch, data) != 0)
		diag_raise();
}

static inline void
ipc_channel_put_xc(struct ipc_channel *ch, void *data)
{
	if (ipc_channel_put(ch, data) != 0)
		diag_raise();
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_IPC_H_INCLUDED */
