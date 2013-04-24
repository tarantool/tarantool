
#include <stdio.h>
#include <tp.h>

static void reply_print(struct tp *rep) {
	while (tp_next(rep)) {
		printf("tuple fields: %d\n", tp_tuplecount(rep));
		printf("tuple size: %d\n", tp_tuplesize(rep));
		printf("[");
		while (tp_nextfield(rep)) {
			printf("%-.*s", tp_getfieldsize(rep), tp_getfield(rep));
			if (tp_hasnextfield(rep))
				printf(", ");
		}
		printf("]\n");
	}
}

static int reply(void) {
	struct tp rep;
	tp_init(&rep, NULL, 0, tp_reallocator, NULL);

	while (1) {
		ssize_t to_read = tp_req(&rep);
		printf("to_read: %zu\n", to_read);
		if (to_read <= 0)
			break;
		ssize_t new_size = tp_ensure(&rep, to_read);
		printf("new_size: %zu\n", new_size);
		if (new_size == -1)
			return -1;
		int rc = fread(rep.p, to_read, 1, stdin);
		if (rc != 1)
			return 1;
		tp_use(&rep, to_read);
	}

	ssize_t server_code = tp_reply(&rep);

	printf("op:    %d\n", tp_replyop(&rep));
	printf("count: %d\n", tp_replycount(&rep));
	printf("code:  %zu\n", server_code);

	if (server_code != 0) {
		printf("error: %-.*s\n", tp_replyerrorlen(&rep),
		       tp_replyerror(&rep));
		return 1;
	}

	reply_print(&rep);

	/*
	tp_rewind(&rep);
	reply_print(&rep);
	*/

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp(argv[1], "--reply"))
		return reply();

	char buf[128];
	struct tp req;
	tp_init(&req, buf, sizeof(buf), NULL, NULL);

	/*
	tp_insert(&req, 0, TP_FRET);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_sz(&req, "value");
	*/

	/*
	tp_ping(&req);
	*/

	/*
	tp_select(&req, 0, 1, 0, 10);
	tp_tuple(&req);
	tp_sz(&req, "key");
	*/

	/*
	tp_update(&req, 0, 0);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_updatebegin(&req);
	tp_op(&req, 1, TP_OPSET, "VALUE", 5);
	*/

	/*
	char proc[] = "hello_proc";
	tp_call(&req, 0, proc, sizeof(proc) - 1);
	tp_tuple(&req);
	tp_sz(&req, "arg1");
	tp_sz(&req, "arg2");
	*/

	/*
	tp_update(&req, 0, 0);
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_updatebegin(&req);
	tp_opsplice(&req, 1, 0, 2, "VAL", 3);
	*/

	fwrite(buf, tp_used(&req), 1, stdout);
	return 0;
}
