#ifndef TNT_REQUEST_H_INCLUDED
#define TNT_REQUEST_H_INCLUDED

typedef ssize_t (*tnt_request_t)(void *ptr, char *dst, ssize_t size);

enum tnt_request_type {
	TNT_REQUEST_NONE,
	TNT_REQUEST_PING,
	TNT_REQUEST_INSERT,
	TNT_REQUEST_DELETE,
	TNT_REQUEST_UPDATE,
	TNT_REQUEST_CALL,
	TNT_REQUEST_SELECT
};

struct tnt_request_insert {
	struct tnt_header_insert h;
	struct tnt_tuple t;
};

struct tnt_request_delete {
	struct tnt_header_delete h;
	struct tnt_tuple t;
};

struct tnt_request_update_op {
	uint8_t op;
	uint32_t field;
	char size_enc[5];
	uint32_t size_enc_len;
	uint32_t size;
	char *data;
};

struct tnt_request_update {
	struct tnt_header_update h;
	struct tnt_tuple t;
	uint32_t opc;
	struct tnt_request_update_op *opv;
};

struct tnt_request_call {
	struct tnt_header_call h;
	char proc_enc[5];
	uint32_t proc_enc_len;
	char *proc;
	uint32_t proc_len;
	struct tnt_tuple t;
};

struct tnt_request_select {
	struct tnt_header_select h;
	struct tnt_list l;
};

struct tnt_request {
	enum tnt_request_type type;
	struct tnt_header h;
	union {
		struct tnt_request_insert insert;
		struct tnt_request_delete delete;
		struct tnt_request_call call;
		struct tnt_request_select select;
		struct tnt_request_update update;
	} r;
};

void tnt_request_init(struct tnt_request *r);
void tnt_request_free(struct tnt_request *r);

int tnt_request(struct tnt_request *r, char *buf, size_t size, size_t *off);
int tnt_request_from(struct tnt_request *r, tnt_request_t rcv, void *ptr);

#endif /* TNT_REQUEST_H_INCLUDED */
