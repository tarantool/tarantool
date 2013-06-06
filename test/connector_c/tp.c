
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

static inline int
test_check_read(void)
{
    int sock;
    struct sockaddr_in tt;
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		printf("Failed to create socket\n");
		return 1;
    }

    memset(&tt, 0, sizeof(tt));
    tt.sin_family = AF_INET;
    tt.sin_addr.s_addr = inet_addr("127.0.0.1");
    tt.sin_port = htons(33013);
    if (connect(sock, (struct sockaddr *) &tt, sizeof(tt)) < 0) {
		printf("Failed to connect\n");
		return 1;
    }

    {
		struct tp req;
		tp_init(&req, NULL, 0, tp_realloc, NULL);
		tp_insert(&req, 0, 0);
		tp_tuple(&req);
		tp_sz(&req, "_i32");
		tp_sz(&req, "0e72ae1a-d0be-4e49-aeb9-aebea074363c");
		write(sock, tp_buf(&req), tp_used(&req));
		tp_free(&req);
    }

    {
		struct tp req;
		tp_init(&req, NULL, 0, tp_realloc, NULL);
		tp_select(&req, 0, 0, 0, 1);
		tp_tuple(&req);
		tp_sz(&req, "_i32");
		write(sock, tp_buf(&req), tp_used(&req));
		tp_free(&req);
    }

    {
		struct tp rep;
		tp_init(&rep, NULL, 0, tp_realloc, NULL);
		while (1) {
			ssize_t to_read = tp_req(&rep);
			if (to_read <= 0)
				break;
			ssize_t new_size = tp_ensure(&rep, to_read);
			if (new_size == -1) {
				// no memory (?)
				return 1;
			}
			ssize_t res = read(sock, rep.p, to_read);
			if (res == 0) {
				// eof
				return 1;
			} else if (res < 0) {
				// error
				return 1;
			}
			tp_use(&rep, res);
		}

		ssize_t server_code = tp_reply(&rep);

		printf("op:    %d\n", tp_replyop(&rep));
		printf("count: %d\n", tp_replycount(&rep));
		printf("code:  %zu\n", server_code);

		if (server_code != 0) {
			printf("error: %-.*s\n", tp_replyerrorlen(&rep),
			   tp_replyerror(&rep));
			tp_free(&rep);
			return 1;
		}
		reply_print(&rep);
		tp_free(&rep);
    }

    return 0;
}

static inline void
test_check_buffer_initialized(void) {
	struct tp req;
	tp_init(&req, NULL, 0, tp_realloc, NULL);
	tp_select(&req, 0, 0, 0, 0); /* could fail on assert */
	tp_tuple(&req);
	tp_sz(&req, "key");
	tp_free(&req);
}

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	test_check_buffer_initialized();
	assert(test_check_read() == 0);

#if 0
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
#endif
	return 0;
}
