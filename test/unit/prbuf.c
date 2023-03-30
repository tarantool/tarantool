#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>

#define UNIT_TAP_COMPATIBLE 1
#include "unit.h"
#include "core/prbuf.h"
#include "trivia/util.h"
#include "fiber.h"
#include "memory.h"

/**
 * The below buffer size/payload size variants give next unused space at
 * the end of buffer sizes:
 *
 * payload_sizes:   4, 16, 39
 * --------------------------
 * buffer_size=128: 0, 12, 26
 * buffer_size=256: 0,  0, 25
 * buffer_size=507: 3, 11,  7
 *
 * So we test next end of the buffer scenarios:
 *  - no unused space
 *  - not enough space to put end mark
 *  - enough space to put end mark
 *
 * Additionally payload size of 39 helps to test unaligned record header.
 */
const size_t buffer_size_arr[] = { 128, 256, 507 };
const size_t copy_number_arr[] = { 16, 32, 64 };
const char payload_small[] = { 0xab, 0xdb, 0xee, 0xcc };
const char payload_avg[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
#define PAYLOAD_LARGE_SIZE 39
static char payload_large[PAYLOAD_LARGE_SIZE];

enum test_buffer_status {
	OK = 0,
	WRONG_PAYLOAD_SIZE,
	WRONG_PAYLOAD_CONTENT,
	RECOVERY_ERROR,
	TEST_BUFFER_STATUS_COUNT
};

const char *info_msg = "prbuf(size=%lu, payload=%lu, iterations=%lu) %s";

const char *test_buffer_status_strs[TEST_BUFFER_STATUS_COUNT] = {
	"has been validated",
	"failed due to wrong size of payload after recovery",
	"failed due to wrong content of payload after recovery",
	"failed to recover",
};

static void
payload_large_init(void)
{
	for (size_t i = 0; i < PAYLOAD_LARGE_SIZE; ++i)
		payload_large[i] = (char)i;
}

/** Just some offset to test reader with. */
#define BUFFER_OFFSET 33

static inline const char *
diagmsg(void)
{
	return diag_last_error(diag_get())->errmsg;
}

static int
save_buffer(void *buf, int size)
{
	int fd = open("prbuf.bin", O_CREAT | O_TRUNC | O_RDWR, 0644);
	unlink("prbuf.bin");
	if (fd == -1)
		fail("open(prbuf.bin)", "== -1");

	int rc = write(fd, payload_large, BUFFER_OFFSET);
	if (rc != BUFFER_OFFSET)
		fail("write(prbuf.bin)", "incomplete write");
	rc = write(fd, buf, size);
	if (rc != size)
		fail("write(prbuf.bin)", "incomplete write");

	return fd;
}

void
fill_buffer(struct prbuf *buf, const char *payload, int payload_size,
	    int count)
{
	for (int i = 0; i < count; i++) {
		char *p = prbuf_prepare(buf, payload_size);
		if (p == NULL)
			fail("prbuf_prepare", "failure");
		memcpy(p, payload, payload_size);
		prbuf_commit(buf);
	}
}

static void
test_reader_bad_header(void *buf, size_t size, const char *errmsg)
{
	plan(2);
	header();

	int fd = save_buffer(buf, size);
	struct prbuf_reader reader;
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);

	struct prbuf_entry entry;
	int rc = prbuf_reader_next(&reader, &entry);
	ok(rc == -1, "rc is %d", rc);
	ok(strstr(diagmsg(), errmsg) != NULL,
	   "diag message is '%s'", diagmsg());
	close(fd);

	footer();
	check_plan();
}

static void
test_reader_good_header(void *buf, size_t size)
{
	int fd = save_buffer(buf, size);
	struct prbuf_reader reader;
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);
	struct prbuf_entry entry;
	int rc = prbuf_reader_next(&reader, &entry);
	ok(rc == 0, "rc is %d", rc);
	close(fd);
	prbuf_reader_destroy(&reader);
}

static void
test_reader_iterate_failure(void *buf, size_t size, const char *errmsg)
{
	plan(2);
	header();

	int fd = save_buffer(buf, size);
	struct prbuf_reader reader;
	struct prbuf_entry entry;
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);

	int rc;
	while ((rc = prbuf_reader_next(&reader, &entry)) == 0 && entry.size != 0)
		;

	ok(rc == -1, "rc is %d", rc);
	ok(strstr(diagmsg(), errmsg) != NULL,
	   "diag message is '%s'", diagmsg());
	close(fd);
	prbuf_reader_destroy(&reader);

	footer();
	check_plan();
}

static int
test_buffer(uint32_t buffer_size, const char *payload, uint32_t payload_size,
	    size_t copy_number)
{
	int rc = OK;
	char *mem = xmalloc(buffer_size);
	struct prbuf buf;
	struct prbuf_reader reader;
	int fd = -1;
	bool created = false;

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload, payload_size, copy_number);

	struct prbuf_entry entry;
	fd = save_buffer(mem, buffer_size);
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);
	created = true;
	int res;
	int count = 0;
	while ((res = prbuf_reader_next(&reader, &entry)) == 0 &&
	       entry.size != 0) {
		if (entry.size != payload_size) {
			rc = WRONG_PAYLOAD_SIZE;
			goto finish;
		}
		if (memcmp(payload, entry.ptr, payload_size) != 0) {
			rc = WRONG_PAYLOAD_CONTENT;
			goto finish;
		}
		count++;
	};
	if (res < 0)
		diag_log();

	/*
	 * 16 is size of the header. 4 is size of record overhead.
	 */
	int expected_capacity = MIN((buffer_size - 16) / (payload_size + 4),
				    copy_number);
	if (res < 0 || entry.size != 0 || entry.ptr != NULL ||
	    count != expected_capacity) {
		rc = RECOVERY_ERROR;
		goto finish;
	}

	/* Check we continue to read EOF. */
	if (prbuf_reader_next(&reader, &entry) != 0 ||
	    entry.size != 0 || entry.ptr != NULL) {
		rc = RECOVERY_ERROR;
		goto finish;
	}

finish:
	if (fd != -1) {
		if (created)
			prbuf_reader_destroy(&reader);
		close(fd);
	}
	free(mem);
	return rc;
}

static void
test_buffer_foreach_copy_number(uint32_t buffer_size,
				const char *payload, uint32_t payload_size)
{
	int rc = 0;
	for (size_t i = 0; i < lengthof(copy_number_arr); ++i) {
		rc = test_buffer(buffer_size, payload, payload_size,
				 copy_number_arr[i]);
		is(rc, 0, info_msg, buffer_size, payload_size,
		   copy_number_arr[i], test_buffer_status_strs[rc]);
	}
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
	plan(27);
	header();

	for (size_t i = 0; i < lengthof(buffer_size_arr); ++i)
		test_buffer_foreach_payload(buffer_size_arr[i]);

	footer();
	check_plan();
}

static void
test_buffer_bad_header(void)
{
	plan(4);
	header();

	size_t buffer_size = buffer_size_arr[0];
	size_t copy_number = copy_number_arr[0];
	size_t payload_size = lengthof(payload_small);
	char mem[buffer_size];

	struct prbuf buf;

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	uint32_t bad_version = 666;
	memcpy(buf.header, &bad_version, sizeof(uint32_t));
	test_reader_bad_header(mem, buffer_size, "unknown format version");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	/* Change begin to wrong value. */
	mem[10] = 0xDD;
	test_reader_bad_header(mem, buffer_size, "inconsistent header");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	/* Change end to wrong value. */
	mem[15] = 0xDD;
	test_reader_bad_header(mem, buffer_size, "inconsistent header");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	/* Truncate buffer so that header is incomplete. */
	test_reader_bad_header(mem, 10, "truncated header");

	footer();
	check_plan();
}

static void
test_buffer_corrupted_record_metadata(void)
{
	header();
	plan(2);

	size_t buffer_size = buffer_size_arr[0];
	size_t copy_number = copy_number_arr[0];
	size_t payload_size = lengthof(payload_small);
	char mem[buffer_size];

	struct prbuf buf;

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	/*
	 * Corrupt the first record. First in sense it's position in memory
	 * is before all others yet it is not first in the read order.
	 */
	mem[17] = 0xDD;
	test_reader_iterate_failure(mem, buffer_size, "invalid record length");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, copy_number);
	/* Corrupt same record. Zero size records are invalid. */
	mem[16] = 0;
	test_reader_iterate_failure(mem, buffer_size, "invalid record length");

	footer();
	check_plan();
}

static void
test_buffer_too_large_entry(void)
{
	header();
	plan(1);

	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	char *p = prbuf_prepare(&buf, buffer_size);
	ok(p == NULL, "NULL is expected");

	footer();
	check_plan();
}

static void
test_buffer_empty(void)
{
	plan(6);
	header();

	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);

	struct prbuf_reader reader;
	struct prbuf_entry entry;

	int fd = save_buffer(mem, buffer_size);
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);

	int rc;
	rc = prbuf_reader_next(&reader, &entry);
	ok(rc == 0, "next rc is %d", rc);
	ok(entry.size == 0, "entry size is %zd", entry.size);
	ok(entry.ptr == NULL, "NULL is expected");

	/* Check we continue to read EOF. */
	rc = prbuf_reader_next(&reader, &entry);
	ok(rc == 0, "next rc is %d", rc);
	ok(entry.size == 0, "entry size is %zd", entry.size);
	ok(entry.ptr == NULL, "NULL is expected");

	close(fd);
	prbuf_reader_destroy(&reader);

	footer();
	check_plan();
}

/**
 * Count the number of records in the buffer when reading it.
 */
static int
count_records(void *buf, size_t size)
{
	struct prbuf_reader reader;
	struct prbuf_entry entry;

	int fd = save_buffer(buf, size);
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);
	int entry_count = 0;
	int rc;
	while ((rc = prbuf_reader_next(&reader, &entry)) == 0 && entry.size != 0)
		entry_count++;
	if (rc != 0) {
		diag_log();
		fail("prbuf_reader_next", "!= 0");
	}
	close(fd);
	prbuf_reader_destroy(&reader);
	return entry_count;
}

static void
test_buffer_prepared(void)
{
	plan(2);
	header();

	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];
	size_t payload_size = lengthof(payload_small);

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_small, payload_size, 32);

	int entry_count = count_records(mem, buffer_size);
	char *p = prbuf_prepare(&buf, payload_size);
	ok(p != NULL, "not NULL is expected");
	int new_entry_count = count_records(mem, buffer_size);
	/*
	 * The number of entries after prepare should decrease since
	 * it must overwrite some of the old records.
	 */
	ok(entry_count - new_entry_count == 1, "old is %d, new is %d",
	   entry_count, new_entry_count);

	footer();
	check_plan();
}

static void
test_buffer_prepared_large(void)
{
	plan(3);
	header();

	size_t buffer_size = buffer_size_arr[0];
	char mem[buffer_size];
	size_t payload_size = lengthof(payload_small);

	struct prbuf buf;
	prbuf_create(&buf, mem, buffer_size);
	/* Fill more than a half of the buffer. */
	int entry_count = 8;
	fill_buffer(&buf, payload_small, payload_size, entry_count);

	/*
	 * Prepare one single entry which is going to overwrite
	 * all other records so in fact buffer should be empty until commit.
	 */
	char *p = prbuf_prepare(&buf, 90);
	ok(p != NULL, "not NULL is expected");
	int entry_count_before = count_records(mem, buffer_size);
	ok(entry_count_before == 0, "entry count before is %d",
	   entry_count_before);

	fill_buffer(&buf, payload_small, payload_size, entry_count);
	int entry_count_after = count_records(mem, buffer_size);
	ok(entry_count_after == entry_count, "entry count after is %d",
	   entry_count_after)

	footer();
	check_plan();
}

static void
test_buffer_truncated(void)
{
	plan(3);
	header();

	size_t buffer_size = 128;
	char mem[128];
	size_t payload_size = lengthof(payload_avg);

	struct prbuf buf;

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_avg, payload_size, 4);
	/* Truncate in the middle of length metadata of 2d record. */
	test_reader_iterate_failure(mem, 38, "truncated record");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_avg, payload_size, 6);
	/* Truncate in the middle of length metadata of record after wrap. */
	test_reader_iterate_failure(mem, 17, "truncated record");

	prbuf_create(&buf, mem, buffer_size);
	fill_buffer(&buf, payload_avg, payload_size, 4);
	/* Truncate in the middle of 3d record. */
	test_reader_iterate_failure(mem, 66, "truncated record");

	footer();
	check_plan();
}

static void
test_buffer_begin_border_values(void)
{
	plan(2);
	header();

	char mem[41];
	size_t size = sizeof(mem);

	struct prbuf buf;
	prbuf_create(&buf, mem, size);
	/*
	 * Fill the buffer in such a way that the `header.begin` is as close
	 * to the end of the buffer as possible.
	 */
	char c = '!';
	fill_buffer(&buf, &c, 1, 9);
	test_reader_good_header(mem, size);

	/* Increase begin offset by 1. */
	mem[8] += 1;
	test_reader_bad_header(mem, size, "inconsistent header");

	footer();
	check_plan();
}

static void
test_buffer_end_border_values(void)
{
	plan(2);
	header();

	char mem[40];
	size_t size = sizeof(mem);

	struct prbuf buf;
	prbuf_create(&buf, mem, size);
	/*
	 * Fill the buffer in such a way that the `header.end` is exactly
	 * at the end of the buffer.
	 */
	fill_buffer(&buf, payload_small, 4, 3);
	test_reader_good_header(mem, size);

	/* Increase end offset by 1. */
	mem[12] += 1;
	test_reader_bad_header(mem, size, "inconsistent header");

	footer();
	check_plan();
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
	plan(8);
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

	struct prbuf_reader reader;
	struct prbuf_entry entry;

	int fd = save_buffer(mem, sizeof(mem));
	prbuf_reader_create(&reader, fd, BUFFER_OFFSET);
	int rc = prbuf_reader_next(&reader, &entry);
	ok(rc == 0, "rc is %d", rc);
	ok(entry.size == (size_t)max_size,
	   "expected %d got %zd", max_size, entry.size);
	ok(memcmp(entry.ptr, payload, max_size) == 0,
	   "0 is expected");
	rc = prbuf_reader_next(&reader, &entry);
	ok(rc == 0, "rc is %d", rc);
	ok(entry.size == 0, "entry size is %zd", entry.size);
	ok(entry.ptr == NULL, "NULL is expected");
	close(fd);

	p = prbuf_prepare(&buf, max_size + 1);
	ok(p == NULL, "NULL is expected");

	footer();
	check_plan();
}

static void
test_buffer_wrap_on_prepare(void)
{
	plan(1);
	header();

	size_t payload_size = lengthof(payload_avg);
	char mem[128];
	size_t size = sizeof(mem);

	struct prbuf buf;
	prbuf_create(&buf, mem, size);
	fill_buffer(&buf, payload_avg, payload_size, 5);
	/* Preparing will wrap and allocate place at the beginning. */
	char *p = prbuf_prepare(&buf, payload_size);
	if (p == NULL)
		fail("prbuf_prepare", "failure");

	int num = count_records(mem, size);
	ok(num == 4, "num is %d", num);

	footer();
	check_plan();
}

int
main(void)
{
	plan(12);

	memory_init();
	fiber_init(fiber_c_invoke);
	payload_large_init();

	test_buffer_foreach_size();
	test_buffer_bad_header();
	test_buffer_corrupted_record_metadata();
	test_buffer_too_large_entry();
	test_buffer_empty();
	test_buffer_prepared();
	test_buffer_prepared_large();
	test_max_record_size();
	test_buffer_truncated();
	test_buffer_begin_border_values();
	test_buffer_end_border_values();
	test_buffer_wrap_on_prepare();

	fiber_free();
	memory_free();

	footer();
	return check_plan();
}
