#ifndef TARANTOOL_IPROTO_H
#define TARANTOOL_IPROTO_H

#include <stdint.h>

#include <tbuf.h>
#include <util.h>

/*
 * struct iproto_header and struct iproto_header_retcode
 * share common prefix {msg_code, len, sync}
 */

struct iproto_header {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint8_t data[];
} __packed__;

struct iproto_header_retcode {
	uint32_t msg_code;
	uint32_t len;
	uint32_t sync;
	uint32_t ret_code;
	uint8_t data[];
} __packed__;

static inline struct iproto_header *iproto(const struct tbuf *t)
{
	return (struct iproto_header *)t->data;
}

struct iproto_interactor;

struct iproto_interactor
*iproto_interactor(uint32_t (*interact) (uint32_t msg, uint8_t *data, size_t len));

void iproto_interact(void *);

#endif
