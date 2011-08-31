#include <stdio.h>
#include <connector/c/include/tnt.h>
#include "util.h"
#include "errcode.h"

/** Client handler. Reused between tests. */
struct tnt *t;

/** Test the ping command. */
void test_ping()
{
	const char message[]= {
		0xd, 0x0, 0x0, 0x0,    0x11, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,    0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,    0x1, 0x0, 0x0, 0x0,
		0x4, 0x1, 0x0, 0x0, 0x0 };

	tnt_io_send_raw(t, (char*)message, sizeof(message));

	struct tnt_recv rcv;
	tnt_recv_init(&rcv);
	tnt_recv(t, &rcv);

	printf("return_code: %d\n", TNT_RECV_CODE(&rcv)); /* =0 */
	tnt_recv_free(&rcv);
}

/** A test case for Bug#702397
 * https://bugs.launchpad.net/tarantool/+bug/702397 "If SELECT
 * request specifies tuple count 0, no error"
 */

void test_bug702397()
{
	const char message[]= {
		0x11, 0x0, 0x0, 0x0,    0x14, 0x0, 0x0, 0x0,    0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0,     0x0, 0x0, 0x0, 0x0,     0x0, 0x0, 0x0, 0x0,
		0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0 };

	tnt_io_send_raw(t, (char*)message, sizeof(message));

	struct tnt_recv rcv;
	tnt_recv_init(&rcv);
	tnt_recv(t, &rcv);

	/*
	printf("return_code: %s, %s\n",
	       tnt_errcode_str(tnt_res.errcode >> 8), tnt_res.errmsg);
	       */
	printf("return_code: %s, %s\n",
	       tnt_errcode_str(TNT_RECV_CODE(&rcv) >> 8),
		       tnt_recv_error(&rcv));
	tnt_recv_free(&rcv);
}

/** A test case for Bug#702399
 * https://bugs.launchpad.net/tarantool/+bug/702399
 * ERR_CODE_ILLEGAL_PARAMS is returned when there is no such key
 */

void test_bug702399()
{
	const char message[]= {
		0x11, 0x0, 0x0, 0x0,    0x1d, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x1, 0x0, 0x0, 0x0,     0x0, 0x0, 0x0, 0x0,
		0xff, 0xff, 0xff, 0xff,
		0x1, 0x0, 0x0, 0x0,     0x1, 0x0, 0x0, 0x0,
		0x4,    0x1, 0x0, 0x0, 0x0 };

	tnt_io_send_raw(t, (char*)message, sizeof(message));

	/*
	int res = tnt_execute_raw(conn, message, sizeof message, &tnt_res);
	printf("return_code: %s, %s\n",
	       tnt_errcode_str(tnt_res.errcode >> 8), tnt_res.errmsg);
	       */
	struct tnt_recv rcv;
	tnt_recv_init(&rcv);
	tnt_recv(t, &rcv);

	printf("return_code: %s, %s\n",
	       tnt_errcode_str(TNT_RECV_CODE(&rcv) >> 8),
		       tnt_recv_error(&rcv));
	tnt_recv_free(&rcv);
}

int main()
{
	t = tnt_alloc();
	if (t == NULL)
		return 1;

	tnt_set(t, TNT_OPT_HOSTNAME, "localhost");
	tnt_set(t, TNT_OPT_PORT, 33013); 
	if (tnt_init(t) == -1)
		return 1;

	if (tnt_connect(t) == -1)
		return 1;

	test_ping();
	test_bug702397();
	test_bug702399();

	tnt_free(t);
	return 0;
}
