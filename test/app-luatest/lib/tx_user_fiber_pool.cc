/**
 * Try not include anything besides the public header when possible. To test
 * that the target API is actually exported.
 */
#include "module.h"
#include "msgpuck.h"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

enum test_msg_type {
	TEST_MSG_ECHO,
	TEST_MSG_INSERT,
	TEST_MSG_FLUSH,
	TEST_MSG_TERMINATE,
};

struct insert_payload {
	insert_payload(uint32_t _space_id, const std::vector<char> &_tuple)
		: space_id(_space_id)
		, tuple(_tuple)
	{
	}

	uint32_t space_id;
	std::vector<char> tuple;
};

struct test_msg {
	test_msg() {
		this->type = TEST_MSG_ECHO;
		this->key = 0;
	}

	test_msg(uint32_t space_id, const std::vector<char> &tuple)
		: type(TEST_MSG_INSERT)
		, insert(space_id, tuple)
	{
	}

	test_msg(const test_msg &msg) {
		this->type = msg.type;
		switch (this->type) {
		case TEST_MSG_ECHO:
			this->key = msg.key;
			break;
		case TEST_MSG_INSERT:
			new(&this->insert) insert_payload(msg.insert);
			break;
		default:
			break;
		}
	}

	~test_msg() {
		switch (type) {
		case TEST_MSG_INSERT:
			insert. ~insert_payload();
			break;
		default:
			break;
		}
	}

	test_msg_type type;
	union {
		uint64_t key;
		insert_payload insert;
	};

};

struct tx_module {
public:
	tx_module()
		: wait_push_cond(fiber_cond_new())
		, on_push_cond(fiber_cond_new())
		, max_size(1000)
		, pending_count(0)
	{
	}

	~tx_module()
	{
		fiber_cond_delete(on_push_cond);
		fiber_cond_delete(wait_push_cond);
	}

	void
	push(uint64_t key)
	{
		++pending_count;
		while (queue.size() >= max_size) {
			int rc = fiber_cond_wait(wait_push_cond);
			assert(rc == 0);
			(void)rc;
		}
		queue.push_back(key);
		fiber_cond_broadcast(on_push_cond);
		/* Wake the next fiber up if there is still size. */
		if (queue.size() < max_size)
			fiber_cond_signal(wait_push_cond);
		--pending_count;
		/*
		 * Yield to give time to the requests which came earlier.
		 * Otherwise this fiber might without yields start serving next
		 * requests from the pipe while there are already some other
		 * pending fibers waiting on the cond.
		 */
		fiber_sleep(0);
	}

	void
	wait_key(uint64_t key)
	{
		while (std::find(queue.begin(), queue.end(), key) ==
		       queue.end()) {
			int rc = fiber_cond_wait(on_push_cond);
			assert(rc == 0);
			(void)rc;
		}
	}

	void
	set_max_size(size_t size)
	{
		if (size > max_size)
			fiber_cond_signal(wait_push_cond);
		max_size = size;
	}

	size_t
	get_pending_count() const
	{
		return pending_count;
	}

	std::vector<uint64_t>
	pop_all()
	{
		fiber_cond_signal(wait_push_cond);
		return std::move(queue);
	}

	std::vector<uint64_t>
	get_all() const
	{
		return queue;
	}

private:
	std::vector<uint64_t> queue;
	fiber_cond *wait_push_cond;
	fiber_cond *on_push_cond;
	size_t max_size;
	size_t pending_count;
};

static tx_module glob_tx_module;

static void
tx_module_push_f(void *arg)
{
	uint64_t *key = (uint64_t *)arg;
	glob_tx_module.push(*key);
	delete key;
}

static void
tx_module_insert_f(void *arg)
{
	insert_payload *payload = (insert_payload *)arg;
	box_insert(payload->space_id, payload->tuple.data(),
		   payload->tuple.data() + payload->tuple.size(), NULL);
	delete payload;
}

struct worker {
public:
	worker()
		: is_running(false)
		, thread(std::bind(&worker::run, this))
	{
	}

	~worker()
	{
		test_msg msg;
		msg.type = TEST_MSG_TERMINATE;
		push(msg);
		thread.join();
	}

	void
	push(const test_msg &msg)
	{
		std::unique_lock<std::mutex> lock(mutex);
		bool was_empty = queue.empty();
		queue.push_back(msg);
		if (was_empty)
			cond.notify_one();
	}

private:
	void
	run()
	{
		is_running = true;
		while (is_running) {
			std::vector<test_msg> msgs;
			{
				std::unique_lock<std::mutex> lock(mutex);
				while (queue.empty())
					cond.wait(lock);
				msgs = std::move(queue);
			}
			for (const test_msg &msg : msgs)
				handle_msg(msg);
		}
	}

	void
	handle_msg(const test_msg &msg)
	{
		switch (msg.type) {
		case TEST_MSG_ECHO:
			tnt_tx_push(tx_module_push_f, new uint64_t(msg.key));
			break;
		case TEST_MSG_INSERT:
			tnt_tx_push(tx_module_insert_f,
				    new insert_payload(msg.insert));
			break;
		case TEST_MSG_FLUSH:
			tnt_tx_flush();
			break;
		case TEST_MSG_TERMINATE:
			is_running = false;
			break;
		}
	}

	std::mutex mutex;
	std::condition_variable cond;
	std::vector<test_msg> queue;
	bool is_running;
	std::thread thread;
};

static void
return_keys(box_function_ctx_t *ctx, const std::vector<uint64_t> &keys)
{
	/* Size * max uint byte count in msgpack + array header. */
	size_t byte_count = keys.size() * 9 + 5;
	char *data = new char[byte_count];
	char *pos = mp_encode_array(data, keys.size());
	for (uint64_t key : keys)
		pos = mp_encode_uint(pos, key);
	assert(pos < data + byte_count);
	box_return_mp(ctx, data, pos);
	delete[] data;
}

static uint64_t
decode_arg_u64(const char *args)
{
	assert(mp_typeof(*args) == MP_ARRAY);
	uint32_t count = mp_decode_array(&args);
	assert(count == 1);
	(void)count;
	assert(mp_typeof(*args) == MP_UINT);
	return mp_decode_uint(&args);
}

static std::unique_ptr<worker> glob_worker;

} /* anon namespace */

extern "C" {
int
worker_start(box_function_ctx_t *, const char *, const char *)
{
	assert(glob_worker == nullptr);
	glob_worker.reset(new worker());
	return 0;
}

int
worker_stop(box_function_ctx_t *, const char *, const char *)
{
	glob_worker.reset();
	return 0;
}

int
worker_echo(box_function_ctx_t *, const char *args, const char *)
{
	assert(glob_worker != nullptr);
	test_msg msg;
	msg.type = TEST_MSG_ECHO;
	msg.key = decode_arg_u64(args);
	glob_worker->push(msg);
	return 0;
}

int
worker_insert(box_function_ctx_t *, const char *args, const char *args_end)
{
	assert(glob_worker != nullptr);
	assert(mp_typeof(*args) == MP_ARRAY);
	uint32_t count = mp_decode_array(&args);
	assert(count == 2);
	(void)count;
	assert(mp_typeof(*args) == MP_UINT);
	uint64_t space_id = mp_decode_uint(&args);
	assert(space_id <= UINT32_MAX);
	assert(mp_typeof(*args) == MP_ARRAY);
	test_msg insert(space_id, std::vector<char>(args, args_end));
	glob_worker->push(insert);
	return 0;
}

int
worker_flush(box_function_ctx_t *, const char *, const char *)
{
	assert(glob_worker != nullptr);
	test_msg msg;
	msg.type = TEST_MSG_FLUSH;
	glob_worker->push(msg);
	return 0;
}

int
tx_wait_key(box_function_ctx_t *, const char *args, const char *)
{
	glob_tx_module.wait_key(decode_arg_u64(args));
	return 0;
}

int
tx_set_max_size(box_function_ctx_t *, const char *args, const char *)
{
	glob_tx_module.set_max_size(decode_arg_u64(args));
	return 0;
}

int
tx_get_pending_count(box_function_ctx_t *ctx, const char *, const char *)
{
	char res[16];
	char *pos = mp_encode_uint(res, glob_tx_module.get_pending_count());
	box_return_mp(ctx, res, pos);
	return 0;
}

int
tx_pop_all(box_function_ctx_t *ctx, const char *, const char *)
{
	return_keys(ctx, glob_tx_module.pop_all());
	return 0;
}

int
tx_get_all(box_function_ctx_t *ctx, const char *, const char *)
{
	return_keys(ctx, glob_tx_module.get_all());
	return 0;
}

} /* extern "C" */
