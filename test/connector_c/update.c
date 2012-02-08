
/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include <connector/c/include/libtnt/tnt.h>
#include <connector/c/include/libtnt/tnt_net.h>
#include <connector/c/include/libtnt/tnt_io.h>

#include <util.h>
#include <errcode.h>

/*==========================================================================
 * test variables
 *==========================================================================*/

/** tarantool connector instance */
static struct tnt_stream *tnt;

static char *long_string = "A long time ago, in a galaxy far, far away...\n"
			   "It is a period of civil war. Rebel\n"
			   "spaceships, striking from a hidden\n"
			   "base, have won their first victory\n"
			   "against the evil Galactic Empire.\n"
			   "During the battle, Rebel spies managed\n"
			   "to steal secret plans to the Empire's\n"
			   "ultimate weapon, the Death Star, an\n"
			   "armored space station with enough\n"
			   "power to destroy an entire planet.\n"
			   "Pursued by the Empire's sinister agents,\n"
			   "Princess Leia races home aboard her\n"
			   "starship, custodian of the stolen plans\n"
			   "that can save her people and restore\n"
			   "freedom to the galaxy....";

/*==========================================================================
 * function declaration
 *==========================================================================*/

/*--------------------------------------------------------------------------
 * tarantool management functions
 *--------------------------------------------------------------------------*/

/** insert tuple */
void
insert_tuple(struct tnt_tuple *tuple);

/** select tuple by key */
void
select_tuple(i32 key);

/** update fields */
void
update(i32 key, struct tnt_stream *stream);

/** add update fields operation: set int32 */
void
update_set_i32(struct tnt_stream *stream, i32 field, i32 value);

/** add update fields operation: set string */
void
update_set_str(struct tnt_stream *stream, i32 field, char *str);

/** add update fields operation: splice string */
void
update_splice_str(struct tnt_stream *stream, i32 field, i32 offset, i32 length, char *list);

/** add update fields operation: delete field */
void
update_delete_field(struct tnt_stream *stream, i32 field);


/** receive reply from server */
void
recv_command(char *command);

/** print tuple */
void
print_tuple(struct tnt_tuple *tuple);

/*--------------------------------------------------------------------------
 * test suite functions
 *--------------------------------------------------------------------------*/

/** setup test suite */
void
test_suite_setup();

/** clean-up test suite */
void
test_suite_tear_down();

/** print error message and exit */
void
fail(char *msg);

/** print tarantool error message and exit */
void
fail_tnt_error(char *msg, int error_code);

/** print tarantool error message and exit */
void
fail_tnt_perror(char *msg);


/*--------------------------------------------------------------------------
 * test cases functions
 *--------------------------------------------------------------------------*/

/** update fields test case: simple set operation test */
void
test_simple_set();

/** update fields test case: long set operation test */
void
test_long_set();

/** update fields test case: append(set) operation test */
void
test_append();

/** update fields test case: simple arithmetics operations test */
void
test_simple_arith();

/** update fields test case: multi arithmetics operations test */
void
test_multi_arith();

/** update fields test case: splice operations test */
void
test_splice();

/** update fields test case: set and spice operations test */
void
test_set_and_splice();

/** update fields test case: delete field operations test */
void
test_delete_field();


/*==========================================================================
 * function definition
 *==========================================================================*/

int
main(void)
{
	/* initialize suite */
	test_suite_setup();
	/* run tests */
	test_simple_set();
	test_long_set();
	test_append();
	test_simple_arith();
	test_multi_arith();
	test_splice();
	test_set_and_splice();
	test_delete_field();
	/* clean-up suite */
	test_suite_tear_down();
	return EXIT_SUCCESS;
}


/*--------------------------------------------------------------------------
 * tarantool management functions
 *--------------------------------------------------------------------------*/

void
insert_tuple(struct tnt_tuple *tuple)
{
	if (tnt_insert(tnt, 0, TNT_FLAG_RETURN, tuple) < 0)
		fail_tnt_perror("tnt_insert");
	if (tnt_flush(tnt) < 0)
		fail_tnt_perror("tnt_flush");
	recv_command("insert");
}

void
select_tuple(i32 key)
{
	struct tnt_list tuple_list;
	tnt_list_init(&tuple_list);
	struct tnt_tuple *tuple = tnt_list_at(&tuple_list, NULL);
	tnt_tuple(tuple, "%d", key);
	if (tnt_select(tnt, 0, 0, 0, 1, &tuple_list) < 0)
		fail_tnt_perror("tnt_select");
	if (tnt_flush(tnt) < 0)
		fail_tnt_perror("tnt_flush");
	recv_command("select");
	tnt_list_free(&tuple_list);
}

void
update(i32 key, struct tnt_stream *stream)
{
	struct tnt_tuple *k = tnt_tuple(NULL, "%d", key);
	if (tnt_update(tnt, 0, TNT_FLAG_RETURN, k, stream) < 0)
		fail_tnt_perror("tnt_update");
	if (tnt_flush(tnt) < 0)
		fail_tnt_perror("tnt_flush");
	tnt_tuple_free(k);
	recv_command("update fields");
}

void
update_set_i32(struct tnt_stream *stream, i32 field, i32 value)
{
	int result = tnt_update_assign(stream, field, (char *)&value, sizeof(value));
	if (result < 0)
		fail_tnt_error("tnt_update_assign", result);
}

void
update_set_str(struct tnt_stream *stream, i32 field, char *str)
{
	int result = tnt_update_assign(stream, field, str, strlen(str));
	if (result < 0)
		fail_tnt_error("tnt_update_delete_field", result);
}

void
update_splice_str(struct tnt_stream *stream, i32 field, i32 offset, i32 length, char *list)
{
	int result = tnt_update_splice(stream, field, offset, length, list, strlen(list));
	if (result < 0)
		fail_tnt_error("tnt_update_splice", result);
}

void
update_delete_field(struct tnt_stream *stream, i32 field)
{
	int result = tnt_update_delete(stream, field);
	if (result < 0)
		fail_tnt_error("tnt_update_delete", result);
}

void
recv_command(char *command)
{
	struct tnt_iter i;
	tnt_iter_stream(&i, tnt);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_ISTREAM_REPLY(&i);
		printf("%s: respond %s (op: %"PRIu32", reqid: %"PRIu32", code: %"PRIu32", count: %"PRIu32")\n",
			command, tnt_strerror(tnt),
			r->op,
			r->reqid,
			r->code,
			r->count);
		struct tnt_iter it;
		tnt_iter_list(&it, TNT_REPLY_LIST(r));
		while (tnt_next(&it)) {
			struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
			print_tuple(tu);
		}
		tnt_iter_free(&it);
	}
	if (i.status == TNT_ITER_FAIL)
		fail_tnt_perror("tnt_next");
	tnt_iter_free(&i);
}

void
print_tuple(struct tnt_tuple *tuple)
{
	bool is_first = true;
	printf("(");

	struct tnt_iter ifl;
	tnt_iter(&ifl, tuple);
	while (tnt_next(&ifl)) {
		char *data = TNT_IFIELD_DATA(&ifl);
		uint32_t size = TNT_IFIELD_SIZE(&ifl);
		if (!is_first) {
			printf(", ");
		}
		is_first = false;

		switch(size) {
		case 1:
			printf("%"PRIi8" (0x%02"PRIx8")", *(i8 *)data, *(i8 *)data);
			break;
		case 2:
			printf("%"PRIi16" (0x%04"PRIx16")", *(i16 *)data, *(i16 *)data);
			break;
		case 4:
			printf("%"PRIi32" (0x%08"PRIx32")", *(i32 *)data, *(i32 *)data);
			break;
		case 8:
			printf("%"PRIi64" (0x%016"PRIx64")", *(i64 *)data, *(i64 *)data);
			break;
		default:
			printf("'%.*s'", size, data);
			break;
		}
	}
	if (ifl.status == TNT_ITER_FAIL)
		fail("tuple parsing error");
	tnt_iter_free(&ifl);
	printf(")\n");
}


/*--------------------------------------------------------------------------
 * test suite functions
 *--------------------------------------------------------------------------*/

void
test_suite_setup()
{
	tnt = tnt_net(NULL);
	if (tnt == NULL) {
		fail("tnt_alloc");
	}

	tnt_set(tnt, TNT_OPT_HOSTNAME, "localhost");
	tnt_set(tnt, TNT_OPT_PORT, 33013);

	if (tnt_init(tnt) == -1)
		fail_tnt_perror("tnt_init");
	if (tnt_connect(tnt) == -1)
		fail_tnt_perror("tnt_connect");
}

void
test_suite_tear_down()
{
	tnt_stream_free(tnt);
}

void
fail(char *msg)
{
	printf("fail: %s\n", msg);
	exit(EXIT_FAILURE);
}

void
fail_tnt_error(char *msg, int error_code)
{
	printf("fail: %s: %i\n", msg, error_code);
	exit(EXIT_FAILURE);
}

void
fail_tnt_perror(char *msg)
{
	printf("fail: %s: %s\n", msg, tnt_strerror(tnt));
	exit(EXIT_FAILURE);
}


/*--------------------------------------------------------------------------
 * test cases functions
 *--------------------------------------------------------------------------*/

void
test_simple_set()
{
	printf(">>> test simple set\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%d%d%s", 1, 2, 0, "");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test simple set field */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test simple set field\n");
	update_set_str(stream, 1, "new field value");
	update_set_str(stream, 2, "");
	update_set_str(stream, 3, "fLaC");
	update(1, stream);
	tnt_stream_free(stream);

	/* test useless set operations */
	stream = tnt_buf(NULL);
	printf("# set field\n");
	update_set_str(stream, 1, "value?");
	update_set_str(stream, 1, "very very very very very long field value?");
	update_set_str(stream, 1, "field's new value");
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test simple set done\n");
}

void
test_long_set()
{
	printf(">>> test long set\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s%s%s", 1, "first", "", "third");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test set long value in empty field */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test set big value in empty field\n");
	update_set_str(stream, 2, long_string);
	update(1, stream);
	tnt_stream_free(stream);

	/* test replace long value to short */
	stream = tnt_buf(NULL);
	printf("# test replace long value to short\n");
	update_set_str(stream, 2, "short string");
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test long set done\n");
}

void
test_append()
{
	printf(">>> test append\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s", 1, "first");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test append field */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test append field\n");
	update_set_str(stream, 2, "second");
	update(1, stream);
	tnt_stream_free(stream);

	/* test multi append field */
	stream = tnt_buf(NULL);
	printf("# test multi append\n");
	update_set_str(stream, 3, "3");
	update_set_str(stream, 3, "new field value");
	update_set_str(stream, 3, "other new field value");
	update_set_str(stream, 3, "third");
	update(1, stream);
	tnt_stream_free(stream);

	/* test append many field */
	stream = tnt_buf(NULL);
	printf("# test append many fields\n");
	update_set_str(stream, 4, "fourth");
	update_set_str(stream, 5, "fifth");
	update_set_str(stream, 6, "sixth");
	update_set_str(stream, 7, "seventh");
	update_set_str(stream, 8, long_string);
	update(1, stream);
	tnt_stream_free(stream);

	/* test append and change field */
	stream = tnt_buf(NULL);
	printf("# test append and change field\n");
	update_set_str(stream, 9, long_string);
	update_splice_str(stream, 9, 1, 544, "ac");
	tnt_update_arith(stream, 9, TNT_UPDATE_XOR, 0x3ffffff);
	tnt_update_arith(stream, 9, TNT_UPDATE_ADD, 1024);
	update(1, stream);
	tnt_stream_free(stream);

	/* test set to not an exist field */
	stream = tnt_buf(NULL);
	printf("# test set to not an exist field\n");
	update_set_str(stream, 0xDEADBEEF, "invalid!");
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test append done\n");
}

void
test_simple_arith()
{
	printf(">>> test simple arith\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%d%d%d", 1, 2, 0, 0);
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test simple add */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test simple add\n");
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 16);
	update(1, stream);
	tnt_stream_free(stream);

	/* test overflow add */
	stream = tnt_buf(NULL);
	printf("# test overflow add\n");
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, INT_MAX);
	update(1, stream);
	tnt_stream_free(stream);

	/* test overflow add */
	stream = tnt_buf(NULL);
	printf("# test underflow add\n");
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, INT_MIN);
	update(1, stream);
	tnt_stream_free(stream);

	/* test or */
	stream = tnt_buf(NULL);
	printf("# test simple or\n");
	tnt_update_arith(stream, 2, TNT_UPDATE_OR, 0xbacf);
	tnt_update_arith(stream, 3, TNT_UPDATE_OR, 0xfabc);
	update(1, stream);
	tnt_stream_free(stream);

	/* test xor */
	stream = tnt_buf(NULL);
	printf("# test simple xor\n");
	tnt_update_arith(stream, 2, TNT_UPDATE_XOR, 0xffff);
	tnt_update_arith(stream, 3, TNT_UPDATE_XOR, 0xffff);
	update(1, stream);
	tnt_stream_free(stream);

	/* test and */
	stream = tnt_buf(NULL);
	printf("# test simple and\n");
	tnt_update_arith(stream, 2, TNT_UPDATE_AND, 0xf0f0);
	tnt_update_arith(stream, 3, TNT_UPDATE_AND, 0x0f0f);
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test simple arith done\n");
}

void
test_multi_arith()
{
	printf(">>> test multi splice\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s%d%s", 1, "first", 128, "third");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test and */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test simple and\n");
	update_set_i32(stream, 2, 0);
	update_set_str(stream, 1, "first field new value");
	tnt_update_arith(stream, 2, TNT_UPDATE_XOR, 0xF00F);
	update_set_str(stream, 3, "third field new value");
	tnt_update_arith(stream, 2, TNT_UPDATE_OR, 0xF00F);
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test multi arith done\n");
}

void
test_splice()
{
	printf(">>> test simple splice\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s%s%s", 1, "first", "hi, this is a test string!", "third");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test cut from begin */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test cut from begin\n");
	update_splice_str(stream, 2, 0, 4, "");
	update(1, stream);
	tnt_stream_free(stream);

	/* test cut from middle */
	stream = tnt_buf(NULL);
	printf("# test cut from middle\n");
	update_splice_str(stream, 2, 9, -8, "");
	update(1, stream);
	tnt_stream_free(stream);

	/* test cut from end */
	stream = tnt_buf(NULL);
	printf("# test cut from end\n");
	update_splice_str(stream, 2, -1, 1, "");
	update(1, stream);
	tnt_stream_free(stream);

	/* test insert before begin */
	stream = tnt_buf(NULL);
	printf("# test insert before begin\n");
	update_splice_str(stream, 2, 0, 0, "Bonjour, ");
	update(1, stream);
	tnt_stream_free(stream);

	/* test insert  */
	stream = tnt_buf(NULL);
	printf("# test insert after end\n");
	update_splice_str(stream, 2, 10000, 0, " o_O!?");
	update(1, stream);
	tnt_stream_free(stream);

	/* test replace in begin */
	stream = tnt_buf(NULL);
	printf("# test replace in begin\n");
	update_splice_str(stream, 2, 0, 7, "Hello");
	update(1, stream);
	tnt_stream_free(stream);

	/* test replace in middle */
	stream = tnt_buf(NULL);
	printf("# test replace in middle\n");
	update_splice_str(stream, 2, 17, -6, "field");
	update(1, stream);
	tnt_stream_free(stream);

	/* test replace in end */
	stream = tnt_buf(NULL);
	printf("# test replace in end\n");
	update_splice_str(stream, 2, -6, 4, "! Is this Sparta");
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test simple splice done\n");
}

void
test_set_and_splice()
{
	printf(">>> test set and splice\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s%s%s", 1, "first", "hi, this is a test string!", "third");
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test set long string and splice to short */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test set long string and splice to short\n");
	update_set_str(stream, 2, long_string);
	update_splice_str(stream, 2, 45, 500, " away away away");
	update(1, stream);
	tnt_stream_free(stream);

	/* test set short value and splice to long */
	stream = tnt_buf(NULL);
	printf("# test set short value and splice to long\n");
	update_set_str(stream, 2, "test");
	update_splice_str(stream, 2, -4, 4, long_string);
	update(1, stream);
	tnt_stream_free(stream);

	printf("<<< test set and splice done\n");
}

/** update fields test case: delete field operations test */
void
test_delete_field()
{
	printf(">>> test delete field\n");

	/* insert tuple */
	printf("# insert tuple\n");
	struct tnt_tuple *tuple = tnt_tuple(NULL, "%d%s%s%s%d%d%d%d%d%d%d%d%d%d", 1,
			                    "first", "hi, this is a test string!", "third",
					    1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
	insert_tuple(tuple);
	tnt_tuple_free(tuple);

	/* test simple delete fields */
	struct tnt_stream *stream = tnt_buf(NULL);
	printf("# test simple delete fields\n");
	update_delete_field(stream, 2);
	update(1, stream);
	tnt_stream_free(stream);

	/* test useless operations with delete fields*/
	stream = tnt_buf(NULL);
	printf("# test useless operations with delete fields\n");
	update_set_i32(stream, 1, 0);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	update_delete_field(stream, 1);
	update(1, stream);
	tnt_stream_free(stream);

	/* test multi delete fields */
	stream = tnt_buf(NULL);
	printf("# test multi delete fields\n");
	update_delete_field(stream, 2);
	update_delete_field(stream, 3);
	update_delete_field(stream, 4);
	update_delete_field(stream, 5);
	update_delete_field(stream, 6);
	update_delete_field(stream, 7);
	update_delete_field(stream, 8);
	update_delete_field(stream, 9);
	update_delete_field(stream, 10);
	update(1, stream);
	tnt_stream_free(stream);

	/* test delete and set */
	stream = tnt_buf(NULL);
	printf("# test multi delete fields\n");
	update_delete_field(stream, 1);
	update_set_i32(stream, 1, 3);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	tnt_update_arith(stream, 1, TNT_UPDATE_ADD, 1);
	update(1, stream);
	tnt_stream_free(stream);

	/* test append and delete */
	stream = tnt_buf(NULL);
	printf("# test append and delete\n");
	update_set_str(stream, 3, "second");
	update_delete_field(stream, 3);
	update_set_str(stream, 3, "third");
	update_set_str(stream, 4, "third");
	update_delete_field(stream, 4);
	update_set_str(stream, 4, "third");
	update_set_str(stream, 4, "fourth");
	update_set_str(stream, 5, "fifth");
	update_set_str(stream, 6, "sixth");
	update_set_str(stream, 7, "seventh");
	update_set_str(stream, 8, "eighth");
	update_set_str(stream, 9, "ninth");
	update_delete_field(stream, 7);
	update_delete_field(stream, 6);
	update(1, stream);
	tnt_stream_free(stream);

	/* test double delete */
	stream = tnt_buf(NULL);
	printf("# test double delete\n");
	update_delete_field(stream, 3);
	update_delete_field(stream, 3);
	update(1, stream);
	tnt_stream_free(stream);
	select_tuple(1);

	/* test delete not an exist field */
	stream = tnt_buf(NULL);
	printf("# test delete not an exist field\n");
	update_delete_field(stream, 0xDEADBEEF);
	update(1, stream);
	tnt_stream_free(stream);
	select_tuple(1);

	printf("<<< test delete field done\n");
}
