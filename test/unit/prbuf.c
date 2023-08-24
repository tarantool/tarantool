#include <assert.h>
#include <stdint.h>
#include <string.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "core/prbuf.h"
#include "trivia/util.h"

const size_t buffer_size_arr[] = { 128, 256, 512 };
const size_t copy_number_arr[] = { 16, 32, 64 };
const char payload_small[] = { 0xab, 0xdb, 0xee, 0xcc };
const char payload_avg[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
#define PAYLOAD_LARGE_SIZE 40
static char payload_large[PAYLOAD_LARGE_SIZE];

enum test_buffer_status {
	OK = 0,
	WRONG_PAYLOAD_SIZE,
	WRONG_PAYLOAD_CONTENT,
	RECOVERY_ERROR,
	ALLOCATION_ERROR,
	TEST_BUFFER_STATUS_COUNT
};

const char *info_msg = "prbuf(size=%lu, payload=%lu, iterations=%lu) %s";

const char *test_buffer_status_strs[TEST_BUFFER_STATUS_COUNT] = {
	"has been validated",
	"failed due to wrong size of payload after recovery",
	"failed due to wrong content of payload after recovery",
	"failed to recover",
	"failed to allocate memory"
};

static void
payload_large_init(void)
{
	for (size_t i = 0; i < PAYLOAD_LARGE_SIZE; ++i)
		payload_large[i] = (char)i;
}

static int
test_buffer(uint32_t buffer_size, const char *payload, uint32_t payload_size,
	    size_t copy_number)
{
	int rc = OK;
	char *mem = xmalloc(buffer_size);
	assert(mem != NULL);

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	for (size_t i = 0; i < copy_number; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		if (p == NULL) {
			rc = ALLOCATION_ERROR;
			goto finish;
		}
		memcpy(p, payload, payload_size);
		prbuf_commit(&buf);
	}

	struct prbuf recovered_buf;
	if (prbuf_open(&recovered_buf, mem) != 0) {
		rc = RECOVERY_ERROR;
		goto finish;
	}

	struct prbuf_iterator iter;
	struct prbuf_entry entry;
	prbuf_iterator_create(&recovered_buf, &iter);
	while (prbuf_iterator_next(&iter, &entry) == 0) {
		if (entry.size != payload_size) {
			rc = WRONG_PAYLOAD_SIZE;
			goto finish;
		}
		if (memcmp(payload, entry.ptr, payload_size) != 0) {
			rc = WRONG_PAYLOAD_CONTENT;
			goto finish;
		}
	};
	for (size_t i = 0; i < copy_number; ++i) {
		char *p = prbuf_prepare(&recovered_buf, payload_size);
		if (p == NULL) {
			rc = ALLOCATION_ERROR;
			goto finish;
		}
		memcpy(p, payload, payload_size);
		prbuf_commit(&recovered_buf);
	}

finish:
	free(mem);
	return rc;
}

static void
test_buffer_foreach_copy_number(uint32_t buffer_size,
				const char *payload, uint32_t payload_size)
{
	header();
	int rc = 0;
	for (size_t i = 0; i < lengthof(copy_number_arr); ++i) {
		rc = test_buffer(buffer_size, payload, payload_size,
				 copy_number_arr[i]);
		is(rc, 0, info_msg, buffer_size, payload_size,
		   copy_number_arr[i], test_buffer_status_strs[rc]);
	}
	footer();
}

static void
test_buffer_foreach_payload(uint32_t buffer_size)
{
	test_buffer_foreach_copy_number(buffer_size, payload_small,
					lengthof(payload_small));
	test_buffer_foreach_copy_number(buffer_size, payload_avg,
					lengthof(payload_avg));
	test_buffer_foreach_copy_number(buffer_size, payload_large,
					PAYLOAD_LARGE_SIZE);
}

static void
test_buffer_foreach_size(void)
{
	for (size_t i = 0; i < lengthof(buffer_size_arr); ++i)
		test_buffer_foreach_payload(buffer_size_arr[i]);
}

static void
test_buffer_bad_version(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	size_t copy_number = copy_number_arr[0];
	size_t payload_size = lengthof(payload_small);
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	for (size_t i = 0; i < copy_number; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}
	uint32_t bad_version = 666;
	memcpy(buf.header, &bad_version, sizeof(uint32_t));

	struct prbuf recovered_buf;
	int rc = prbuf_open(&recovered_buf, mem);
	is(rc, -1, "Failed to open buffer with malformed version");
	footer();
}

static void
test_buffer_bad_header(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	size_t copy_number = copy_number_arr[0];
	size_t payload_size = lengthof(payload_small);
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	for (size_t i = 0; i < copy_number; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}

	/* Change begin and end to wrong values. */
	mem[15] = 0xDD;
	mem[10] = 0xDD;

	struct prbuf recovered_buf;
	int rc = prbuf_open(&recovered_buf, mem);
	is(rc, -1, "Failed to open buffer with malformed header");
	footer();
}

static void
test_buffer_corrupted_record(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	size_t copy_number = copy_number_arr[0];
	size_t payload_size = lengthof(payload_small);
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	for (size_t i = 0; i < copy_number; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}

	/* Corrupt first record. */
	mem[17] = 0xDD;

	struct prbuf recovered_buf;
	int rc = prbuf_open(&recovered_buf, mem);
	is(rc, -1, "Failed to open buffer with malformed record");
	footer();
}

static void
test_buffer_too_large_entry(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	char *p = prbuf_prepare(&buf, buffer_size);
	is(p, NULL, "Failed to allocate too large entry");
	footer();
}

static void
test_buffer_empty(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	struct prbuf_iterator iter;
	struct prbuf_entry entry;
	prbuf_iterator_create(&buf, &iter);
	int rc = prbuf_iterator_next(&iter, &entry);
	is(rc, -1, "Buffer is empty");
	rc = prbuf_open(&buf, mem);
	is(rc, 0, "Opened empty buffer");
	prbuf_iterator_create(&buf, &iter);
	rc = prbuf_iterator_next(&iter, &entry);
	is(rc, -1, "Buffer is empty");
	footer();
}

static void
test_buffer_prepared(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];
	size_t payload_size = lengthof(payload_small);

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	size_t copy_count = 32;
	for (size_t i = 0; i < copy_count; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}
	/* Count the actual number of entries stored in the buffer. */
	struct prbuf_iterator iter;
	struct prbuf_entry entry;
	prbuf_iterator_create(&buf, &iter);
	size_t entry_count = 0;
	while (prbuf_iterator_next(&iter, &entry) == 0)
		entry_count++;
	char *p = prbuf_prepare(&buf, payload_size);
	isnt(p, NULL, "Prepare has not failed");
	/*
	 * The number of entries after prepare should decrease since
	 * it must overwrite some of the old records.
	 */
	prbuf_iterator_create(&buf, &iter);
	size_t new_entry_count = 0;
	while (prbuf_iterator_next(&iter, &entry) == 0)
		new_entry_count++;
	is(new_entry_count < entry_count, true, "Entry count has decreased");
	footer();
}

static void
test_buffer_prepared_large(void)
{
	header();
	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];
	size_t payload_size = lengthof(payload_small);

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	/* Fill more than a half of the buffer. */
	size_t entry_count = 8;
	for (size_t i = 0; i < entry_count; ++i) {
		char *p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}
	/*
	 * Prepare one single entry which is going to overwrite
	 * all other records so in fact buffer should be empty until commit.
	 */
	char *p = prbuf_prepare(&buf, 90);
	isnt(p, NULL, "Prepare has not failed");
	struct prbuf_iterator iter;
	struct prbuf_entry entry;
	prbuf_iterator_create(&buf, &iter);
	int rc = prbuf_iterator_next(&iter, &entry);
	is(rc, -1, "Buffer is empty");
	for (size_t i = 0; i < entry_count; ++i) {
		p = prbuf_prepare(&buf, payload_size);
		memcpy(p, payload_small, payload_size);
		prbuf_commit(&buf);
	}
	prbuf_iterator_create(&buf, &iter);
	size_t entry_count_after = 0;
	while (prbuf_iterator_next(&iter, &entry) == 0)
		entry_count_after++;
	is(entry_count_after, entry_count, "Buffer is in correct state");
	footer();
}

/**
 * Check:
 * - we can alloc record of prbuf_max_record_size
 * - we can't alloc larger buffer
 * - record of max size is actually usable
 */
static void
test_max_record_size(void)
{
	header();

	struct prbuf buf;
	char mem[73];
	char payload[73];

	prbuf_create(&buf, mem, sizeof(mem));
	int max_size = prbuf_max_record_size(&buf);

	int i;
	for (i = 0; i < max_size; i++)
		payload[i] = (char)i;

	char *p = prbuf_prepare(&buf, max_size);
	ok(p != NULL, "not NULL is expected");
	memcpy(p, payload, max_size);
	prbuf_commit(&buf);

	struct prbuf rbuf;
	struct prbuf_iterator iter;
	struct prbuf_entry entry;

	if (prbuf_open(&rbuf, mem) != 0)
		fail("prbuf_open", "not 0");
	prbuf_iterator_create(&rbuf, &iter);

	int rc = prbuf_iterator_next(&iter, &entry);
	ok(rc == 0, "rc is %d", rc);
	ok(entry.size == (size_t)max_size,
	   "expected %d got %zd", max_size, entry.size);
	if (entry.size != (size_t)max_size)
		fail("entry size", "incorrect");
	ok(memcmp(entry.ptr, payload, max_size) == 0,
	   "0 is expected");

	rc = prbuf_iterator_next(&iter, &entry);
	ok(rc == -1, "rc is %d", rc);

	p = prbuf_prepare(&buf, max_size + 1);
	ok(p == NULL, "NULL is expected");

	footer();
}

/**
 * There are three possible configurations of test:
 * 1. The size of buffer;
 * 2. The size of payload;
 * 3. The number of saves to the buffer.
 */
int
main(void)
{
	plan(45);
	payload_large_init();
	test_buffer_foreach_size();
	test_buffer_bad_version();
	test_buffer_bad_header();
	test_buffer_corrupted_record();
	test_buffer_too_large_entry();
	test_buffer_empty();
	test_buffer_prepared();
	test_buffer_prepared_large();
	test_max_record_size();
	return check_plan();
}
