
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
test_check_read_reply(int fd) {
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
		ssize_t res = read(fd, rep.p, to_read);
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

	if (server_code != 0) {
		printf("error: %-.*s\n", tp_replyerrorlen(&rep),
		   tp_replyerror(&rep));
		tp_free(&rep);
		return 1;
	}
	if (tp_replyop(&rep) == 17) { /* select */
		reply_print(&rep);
	} else
	if (tp_replyop(&rep) == 13) { /* insert */
	} else {
		return 1;
	}
	tp_free(&rep);
	return 0;
}

static inline int
test_check_read(void)
{
	int fd;
	struct sockaddr_in tt;
	if ((fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		printf("Failed to create socket\n");
		return 1;
	}

	memset(&tt, 0, sizeof(tt));
	tt.sin_family = AF_INET;
	tt.sin_addr.s_addr = inet_addr("127.0.0.1");
	tt.sin_port = htons(33013);
	if (connect(fd, (struct sockaddr *) &tt, sizeof(tt)) < 0) {
		printf("Failed to connect\n");
		return 1;
	}

	struct tp req;
	tp_init(&req, NULL, 0, tp_realloc, NULL);
	tp_insert(&req, 0, 0);
	tp_tuple(&req);
	tp_sz(&req, "_i32");
	tp_sz(&req, "0e72ae1a-d0be-4e49-aeb9-aebea074363c");
	tp_select(&req, 0, 0, 0, 1);
	tp_tuple(&req);
	tp_sz(&req, "_i32");
	int rc = write(fd, tp_buf(&req), tp_used(&req));
	if (rc != tp_used(&req))
		return 1;

	tp_free(&req);

	rc = test_check_read_reply(fd);
	if (rc != 0)
		return 1;
	rc = test_check_read_reply(fd);
	if (rc != 0)
		return 1;

	close(fd);
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

static void
test_gh331(void)
{
	struct tp request;
	tp_init(&request, NULL, 0, tp_realloc, NULL);
	tp_call(&request, 0, "test", 4);
	tp_tuple(&request);
	tp_field(&request, "", 2*tp_size(&request)-1);
	assert(tp_used(&request) <= tp_size(&request));
	tp_free(&request);
}

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	test_gh331();
	test_check_buffer_initialized();
	test_check_read();
	return 0;
}
