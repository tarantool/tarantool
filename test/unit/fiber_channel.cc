#include "memory.h"
#include "fiber.h"
#include "fiber_channel.h"
#include "unit.h"

int status;

void
fiber_channel_basic()
{
	header();
	plan(10);

	struct fiber_channel *channel = fiber_channel_new(1);
	ok(channel != NULL, "fiber_channel_new()");

	ok(fiber_channel_size(channel) == 1, "fiber_channel_size()");

	ok(fiber_channel_count(channel) == 0, "fiber_channel_count()");

	ok(fiber_channel_is_full(channel) == false, "fiber_channel_is_full()");

	ok(fiber_channel_is_empty(channel) == true, "fiber_channel_is_empty()");

	char dummy;

	fiber_channel_put(channel, &dummy);

	ok(fiber_channel_size(channel) == 1, "fiber_channel_size(1)");

	ok(fiber_channel_count(channel) == 1, "fiber_channel_count(1)");

	ok(fiber_channel_is_full(channel) == true, "fiber_channel_is_full(1)");

	ok(fiber_channel_is_empty(channel) == false, "fiber_channel_is_empty(1)");

	void *ptr = NULL;

	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get()");

	fiber_channel_delete(channel);

	footer();
	status = check_plan();
}

void
fiber_channel_get()
{
	header();
	plan(7);

	struct fiber_channel *channel = fiber_channel_new(1);

	char dummy;
	ok(fiber_channel_put_timeout(channel, &dummy, 0) == 0,
	   "fiber_channel_put(0)");
	ok(fiber_channel_put_timeout(channel, &dummy, 0) == -1,
	   "fiber_channel_put_timeout(0)");
	void *ptr = NULL;
	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get(0)");
	ok(fiber_channel_put_timeout(channel, &dummy, 0.01) == 0,
	   "fiber_channel_put_timeout(1)");
	fiber_channel_get(channel, &ptr);
	ok(ptr == &dummy, "fiber_channel_get(1)");

	fiber_channel_close(channel);

	ok(fiber_channel_put(channel, &dummy) == -1, "fiber_channel_put(closed)");

	ok(fiber_channel_get(channel, &ptr) == -1, "fiber_channel_get(closed)");

	fiber_channel_delete(channel);

	footer();
	status = check_plan();
}

static void
fiber_channel_close_basic(enum fiber_channel_close_mode mode)
{
	fiber_channel_set_close_mode(mode);

	bool graceful = mode == FIBER_CHANNEL_CLOSE_GRACEFUL;
	struct fiber_channel *channel = fiber_channel_new(10);

	char msg_1;
	char msg_2;
	char msg_3;
	void *ptr = NULL;

	ok(fiber_channel_put_timeout(channel, &msg_1, 0) == 0,
	   "fiber_channel_put(msg_1)");

	ok(fiber_channel_put_timeout(channel, &msg_2, 0) == 0,
	   "fiber_channel_put(msg_2)");

	fiber_channel_get(channel, &ptr);

	ok(ptr == &msg_1, "fiber_channel_get(1)");

	fiber_channel_close(channel);

	ok(channel->is_closed, "is_closed");
	ok(channel->is_destroyed == !graceful,
	   graceful ? "not is_destroyed" : "is_destroyed");

	ok(fiber_channel_put_timeout(channel, &msg_3, 0) != 0,
	   "not fiber_channel_put(msg_3)");

	ptr = NULL;
	fiber_channel_get(channel, &ptr);

	ok(ptr == (graceful ? &msg_2 : NULL),
	   graceful ? "fiber_channel_get(2)" : "not fiber_channel_get(2)");
	ok(channel->is_destroyed, "is_destroyed");

	fiber_channel_delete(channel);
}

static int
reader_f(va_list ap)
{
	struct fiber_channel *channel =
		(struct fiber_channel *)fiber_get_ctx(fiber());
	void *msg;
	ok(!channel->is_closed, "reader tries to read from the open channel");
	/*
	 * Try to obtain the message from the zero-length channel.
	 * XXX: <reader> fiber hangs forever, until one of the
	 * following occurs:
	 * * <fiber_channel_put> is called from another fiber
	 * * <channel> is closed from another fiber
	 * For the latter case <fiber_channel_get> fails (i.e.
	 * yields non zero status).
	 */
	ok(fiber_channel_get(channel, &msg) != 0,
	   "reader fails to read a message from the zero-length channel");
	ok(channel->is_closed, "reader hangs until channel is closed");
	ok(fiber_channel_get_timeout(channel, &msg, 0) != 0,
	   "reader fails to read a message from the closed channel");
	return 0;
}

static void
fiber_channel_close_reader(enum fiber_channel_close_mode mode)
{
	fiber_channel_set_close_mode(mode);

	struct fiber_channel *channel = fiber_channel_new(0);
	struct fiber *reader = fiber_new("reader", reader_f);
	fail_if(reader == NULL);
	fiber_set_ctx(reader, channel);
	fiber_set_joinable(reader, true);
	fiber_wakeup(reader);
	/* Yield to start tests in <reader> payload. */
	fiber_sleep(0);

	fiber_channel_close(channel);
	ok(channel->is_closed, "is_closed");
	/* Wait until tests in <reader> payload are finished. */
	fiber_join(reader);
	ok(channel->is_destroyed, "is_destroyed");
	fiber_channel_delete(channel);
}

static int
writer_f(va_list ap)
{
	struct fiber_channel *channel =
		(struct fiber_channel *)fiber_get_ctx(fiber());
	char msg;
	ok(!channel->is_closed, "writer tries to write to the open channel");
	/*
	 * Try to push the message into the zero-length channel.
	 * XXX: <writer> fiber hangs forever, until one of the
	 * following occurs:
	 * * <fiber_channel_get> is called from another fiber
	 * * <channel> is closed from another fiber
	 * For the latter case <fiber_channel_put> fails (i.e.
	 * yields non zero status).
	 */
	ok(fiber_channel_put(channel, &msg) != 0,
	   "writer fails to write a message to the zero-length channel");
	ok(channel->is_closed, "writer hangs until channel is closed");
	ok(fiber_channel_put_timeout(channel, &msg, 0) != 0,
	   "writer fails to write a message to the closed channel");
	return 0;
}

static void
fiber_channel_close_writer(enum fiber_channel_close_mode mode)
{
	fiber_channel_set_close_mode(mode);

	struct fiber_channel *channel = fiber_channel_new(0);
	struct fiber *writer = fiber_new("writer", writer_f);
	fail_if(writer == NULL);
	fiber_set_ctx(writer, channel);
	fiber_set_joinable(writer, true);
	fiber_wakeup(writer);
	/* Yield to start tests in <reader> payload. */
	fiber_sleep(0);

	fiber_channel_close(channel);
	ok(channel->is_closed, "is_closed");
	/* Wait until tests in <writer> payload are finished. */
	fiber_join(writer);
	ok(channel->is_destroyed, "is_destroyed");
	fiber_channel_delete(channel);
}

static void
fiber_channel_test_close()
{
	header();
	plan(2 * (8 + 6 + 6));

	fiber_channel_close_basic(FIBER_CHANNEL_CLOSE_FORCEFUL);
	fiber_channel_close_basic(FIBER_CHANNEL_CLOSE_GRACEFUL);

	fiber_channel_close_reader(FIBER_CHANNEL_CLOSE_FORCEFUL);
	fiber_channel_close_reader(FIBER_CHANNEL_CLOSE_GRACEFUL);

	fiber_channel_close_writer(FIBER_CHANNEL_CLOSE_FORCEFUL);
	fiber_channel_close_writer(FIBER_CHANNEL_CLOSE_GRACEFUL);

	footer();
	status = status && check_plan();
}

int
main_f(va_list ap)
{
	(void) ap;
	fiber_channel_basic();
	fiber_channel_get();
	fiber_channel_test_close();
	ev_break(loop(), EVBREAK_ALL);
	return 0;
}

int main()
{
	memory_init();
	fiber_init(fiber_c_invoke);
	struct fiber *main= fiber_new_xc("main", main_f);
	fiber_wakeup(main);
	ev_run(loop(), 0);
	fiber_free();
	memory_free();
	return status;
}
