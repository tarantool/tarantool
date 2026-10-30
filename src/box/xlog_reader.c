/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include <stdint.h>
#include <small/ibuf.h>

#include "xlog_reader.h"

#include "cbus.h"
#include "fiber.h"
#include "fiber_cond.h"
#include "memory.h"
#include "tt_static.h"
#include "xlog.h"
#include "error.h"

/**
 * Stream message. It is used to pass batches with xlog entries from reader
 * thread to TX and back to be freed.
 */
struct xlog_reader_stream_msg {
	/** Base class. */
	struct cmsg base;
	/** Xlog reader instance. */
	struct xlog_reader *reader;
	/** Batch of xlog entries. */
	struct xlog_batch batch;
	/** Error reading xlog batch. */
	struct error *error;
	/** Set if file EOF is reached. */
	bool eof;
	/** Set if EOF marker is read. */
	bool eof_marker;
};

/** Control message. It is used to send stop or finished message. */
struct xlog_reader_control_msg {
	/** Base class. */
	struct cmsg base;

	/** Xlog reader instance. */
	struct xlog_reader *reader;
};

/**
 * Xlog reader, uses a designated thread to read xlogs batches
 * (see `struct xlog_entry_cursor`).
 *
 * Snapshot xlogs are written in batches of size about 128k
 * (`XLOG_TX_AUTOCOMMIT_THRESHOLD`). The reader thread reads and parses
 * the entire batch and send it to TX. There the xlog entries can be obtained
 * using `xlog_reader_next`. After the batch is read entirely in TX the
 * batch is sent back to reader thread to be freed.
 *
 * To organize flow control between reader and TX threads the number of batches
 * passing between threads is limited to 2.
 */
struct xlog_reader {
	/*********************************************************************/
	/*                Fields accessed by TX thread.                      */
	/*********************************************************************/

	/** Reader thread cord. */
	struct cord cord;
	/** Pipe to reader thread endpoint. */
	struct cpipe thread_pipe;
	/** Condition for new stream messages or that streaming is finished. */
	struct fiber_cond cond;
	/**
	 * Message holding current batch being read with reader API. May be
	 * NULL.
	 */
	struct xlog_reader_stream_msg *read_msg;
	/**
	 * Message holding next batch to be read with reader API. May be NULL.
	 */
	struct xlog_reader_stream_msg *next_msg;
	/** Next entry index to be read from current batch. */
	size_t batch_pos;
	/** EOF flag, set if xlog end is reached. */
	bool eof;
	/** Set if EOF marker is read. */
	bool eof_marker;
	/**
	 * Finished flag, set if there will be no more messages from reader
	 * thread.
	 */
	bool finished;

	/*********************************************************************/
	/*                 Fields accessed by reader thread.                 */
	/*********************************************************************/

	/**
	 * Filename of xlog to read. Actually set in TX before reader
	 * thread started.
	 */
	const char *filename;
	/** Message used to read the batch from xlog. May be NULL. */
	struct xlog_reader_stream_msg *write_msg;
	/** Next message to read the batch to. May be NULL. */
	struct xlog_reader_stream_msg *spare_msg;
	/** Pipe to "tx_prio" endpoint. */
	struct cpipe tx_pipe;
	/** Set if TX is requested to stop reading xlog. */
	bool stop;
};

/**
 * Next reader id. It is used to make reader thread endpoint unique among
 * readers.
 */
static unsigned int xlog_reader_id;

/**
 * Callback called when new batch is available.
 *
 * Called in TX thread.
 */
static void
xlog_reader_deliver_batch(struct cmsg *base)
{
	struct xlog_reader_stream_msg *msg =
				(struct xlog_reader_stream_msg *)base;
	struct xlog_reader *reader = msg->reader;

	if (reader->read_msg == NULL) {
		reader->read_msg = msg;
	} else {
		assert(reader->next_msg == NULL);
		reader->next_msg = msg;
	}
	fiber_cond_broadcast(&reader->cond);
}

/**
 * Callback called on batch completion.
 *
 * Called in reader thread.
 */
static void
xlog_reader_batch_completed(struct cmsg *base)
{
	struct xlog_reader_stream_msg *msg =
				(struct xlog_reader_stream_msg *)base;
	struct xlog_reader *reader = msg->reader;

	if (msg->error == NULL && !msg->eof)
		xlog_batch_destroy(&msg->batch);
	if (msg->error != NULL) {
		error_unref(msg->error);
		msg->error = NULL;
	}
	if (reader->write_msg == NULL) {
		reader->write_msg = msg;
	} else {
		assert(reader->spare_msg == NULL);
		reader->spare_msg = msg;
	}
	fiber_wakeup(reader->cord.main_fiber);
}

/**
 * Callback called when reader is finished to send stream messages.
 *
 * Called in TX thread.
 */
static void
xlog_reader_finished(struct cmsg *base)
{
	struct xlog_reader_control_msg *msg =
				(struct xlog_reader_control_msg *)base;
	struct xlog_reader *reader = msg->reader;
	reader->finished = true;
	fiber_cond_broadcast(&reader->cond);
}

/**
 * Callback called when reader is requested to stop.
 *
 * Called in reader thread.
 */
static void
xlog_reader_stop(struct cmsg *base)
{
	struct xlog_reader_control_msg *msg =
				(struct xlog_reader_control_msg *)base;
	struct xlog_reader *reader = msg->reader;
	reader->stop = true;
	fiber_wakeup(reader->cord.main_fiber);

	static struct cmsg_hop route[] = {
		{xlog_reader_finished, /*pipe=*/NULL},
	};
	cmsg_init(&msg->base, route);
	cpipe_push(&reader->tx_pipe, &msg->base);
	cpipe_flush(&reader->tx_pipe);
}

/**
 * Send batch read from xlog to TX.
 *
 * Called in reader thread.
 */
static void
xlog_reader_send_batch(struct xlog_reader *reader)
{
	static struct cmsg_hop route[] = {
		{xlog_reader_deliver_batch, /*pipe=*/NULL},
	};
	cmsg_init(&reader->write_msg->base, route);
	cpipe_push(&reader->tx_pipe, &reader->write_msg->base);
	cpipe_flush(&reader->tx_pipe);
	reader->write_msg = reader->spare_msg;
	reader->spare_msg = NULL;
}

/**
 * Notify reader threat that processing batch at is completed.
 *
 * Called in TX thread.
 */
static void
xlog_reader_complete_batch(struct xlog_reader *reader)
{
	static struct cmsg_hop route[] = {
		{xlog_reader_batch_completed, /*pipe=*/NULL},
	};
	cmsg_init(&reader->read_msg->base, route);
	cpipe_push(&reader->thread_pipe, &reader->read_msg->base);
	cpipe_flush(&reader->thread_pipe);
	reader->read_msg = reader->next_msg;
	reader->next_msg = NULL;
}

static void
xlog_reader_cb(struct ev_loop *loop, ev_watcher *watcher, int events)
{
	(void)loop;
	(void)events;
	struct cbus_endpoint *endpoint = (struct cbus_endpoint *)watcher->data;
	cbus_process(endpoint);
}

/** Read xlog loop. Executed in reader thread. */
static int
xlog_reader_f(va_list ap)
{
	struct xlog_reader *reader = va_arg(ap, struct xlog_reader *);
	struct cbus_endpoint endpoint;

	struct xlog_cursor cursor;
	bool opened;
	int rc = xlog_cursor_open(&cursor, reader->filename);
	opened = rc == 0;
	/*
	 * Create endpoint after opening xlog so that we don't need to store
	 * a copy of reader->filename which is constructor argument.
	 */
	cpipe_create(&reader->tx_pipe, "tx_prio");
	VERIFY(cbus_endpoint_create(
			&endpoint,
			tt_sprintf("xlog_reader_%u", xlog_reader_id),
			xlog_reader_cb, &endpoint) == 0);

	struct xlog_reader_stream_msg msgs[2];
	for (int i = 0; i < 2; i++) {
		struct xlog_reader_stream_msg *msg = &msgs[i];
		msg->reader = reader;
		msg->error = NULL;
		msg->eof = false;
	}

	reader->stop = false;
	reader->write_msg = &msgs[0];
	reader->spare_msg = &msgs[1];

	while (true) {
		while (reader->write_msg == NULL && !reader->stop)
			fiber_yield();
		if (reader->stop)
			break;
		struct xlog_reader_stream_msg *msg = reader->write_msg;
		if (rc == 0)
			rc = xlog_cursor_read_tx(&cursor, &msg->batch);
		if (rc == -1) {
			msg->error = diag_last_error(diag_get());
			error_ref(msg->error);
			diag_clear(diag_get());
		} else if (rc == 1) {
			msg->eof = true;
			msg->eof_marker = xlog_cursor_is_eof(&cursor);
		}
		bool stop = msg->eof ||
			    (msg->error != NULL &&
			     msg->error->type != &type_XlogError);
		xlog_reader_send_batch(reader);
		if (stop)
			break;
		rc = 0;
	}

	if (opened)
		xlog_cursor_close(&cursor, false);

	/*
	 * cbus_endpoint_destroy() below is not enough. This loop is required
	 * to avoid race on creation. The thread may exit before even TX
	 * is connected to its endpoint.
	 */
	while (reader->spare_msg == NULL)
		fiber_yield();

	cbus_endpoint_destroy(&endpoint, NULL);
	cpipe_destroy(&reader->tx_pipe);

	return 0;
}

struct xlog_reader *
xlog_reader_new(const char *filename)
{
	struct xlog_reader *reader = xmalloc(sizeof(*reader));
	reader->filename = filename;
	reader->read_msg = NULL;
	reader->next_msg = NULL;
	reader->batch_pos = 0;
	reader->eof = false;
	reader->finished = false;
	fiber_cond_create(&reader->cond);
	if (cord_costart(&reader->cord, "log_reader", xlog_reader_f,
			 reader) != 0) {
		fiber_cond_destroy(&reader->cond);
		free(reader);
		return NULL;
	}
	cpipe_create(&reader->thread_pipe,
		     tt_sprintf("xlog_reader_%u", xlog_reader_id));
	xlog_reader_id++;
	return reader;
}

void
xlog_reader_delete(struct xlog_reader *reader)
{
	struct xlog_reader_control_msg msg;
	static struct cmsg_hop route[] = {
		{xlog_reader_stop, /*pipe=*/NULL},
	};
	cmsg_init(&msg.base, route);
	msg.reader = reader;
	cpipe_push(&reader->thread_pipe, &msg.base);
	cpipe_flush(&reader->thread_pipe);

	while (!reader->finished)
		fiber_cond_wait(&reader->cond);
	while (reader->read_msg != NULL)
		xlog_reader_complete_batch(reader);
	cpipe_destroy(&reader->thread_pipe);

	if (cord_cojoin(&reader->cord) != 0)
		panic_syserror("xlog reader cord join failed");

	fiber_cond_destroy(&reader->cond);
	TRASH(reader);
	free(reader);
}

enum xlog_reader_result
xlog_reader_next(struct xlog_reader *reader, struct xlog_entry **entry)
{
	assert(entry != NULL);
retry:
	if (reader->eof)
		return reader->eof_marker ?
			XLOG_READER_EOF_MARKER : XLOG_READER_EOF;
	while (reader->read_msg == NULL) {
		if (fiber_cond_wait(&reader->cond) != 0)
			return XLOG_READER_READ_ERROR;
	}
	struct xlog_reader_stream_msg *msg = reader->read_msg;
	if (!msg->eof && msg->error == NULL &&
	    reader->batch_pos < msg->batch.entry_count) {
		*entry = xlog_batch_get(&msg->batch, reader->batch_pos++);
		if ((*entry)->error != NULL) {
			diag_set_error(diag_get(), (*entry)->error);
			return XLOG_READER_DECODE_ERROR;
		}
		return XLOG_READER_OK;
	}
	reader->eof = msg->eof;
	reader->eof_marker = msg->eof_marker;
	reader->batch_pos = 0;
	bool error = false;
	if (msg->error != NULL) {
		diag_set_error(diag_get(), msg->error);
		error = true;
	}
	xlog_reader_complete_batch(reader);
	if (error)
		return XLOG_READER_READ_ERROR;
	goto retry;
}
