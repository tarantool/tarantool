/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "evio.h"
#include "tt_static.h"
#include "lua/utils.h"
#include "diag.h"
#include "box/error.h"
#include "box/errcode.h"
#include "trivia/util.h"
#include "cfg_uri.h"
#include "fiber.h"

struct iproto_service_array {
	/** Iproto binary listeners */
	struct evio_service services[IPROTO_LISTEN_SOCKET_MAX];
	/** Count of currently used services; */
	int service_count;
};

static int
iproto_fill_bound_address(const struct evio_service *service, char *buf)
{
	const struct sockaddr *sockaddr = (struct sockaddr *)
		&service->addrstorage;
	return sio_addr_snprintf(buf, SERVICE_NAME_MAXLEN, sockaddr,
				 service->addr_len);
}

struct iproto_service_array *
iproto_service_array_new(void)
{
	return (struct iproto_service_array *)
		xcalloc(1, sizeof(struct iproto_service_array));
}

void
iproto_service_array_delete(struct iproto_service_array *array)
{
	free(array);
}

void
iproto_service_array_init(struct iproto_service_array *array,
			  evio_accept_f on_accept, void *on_accept_param)
{
	for (int i = 0; i < IPROTO_LISTEN_SOCKET_MAX; i++) {
		evio_service_init(loop(), &array->services[i],
				  "service", on_accept, on_accept_param);
	}
	array->service_count = 0;
}

const char *
iproto_service_array_fill_listen_info(struct iproto_service_array *array,
				      char *buf)
{
	if (array->service_count == 0)
		return NULL;
	int cnt = 0;
	char *p = buf;
	const unsigned max = IPROTO_LISTEN_INFO_MAXLEN;
	for (int i = 0; i < array->service_count; i++) {
		/*
		 * We write the listening addresses to the buffer,
		 * separated by commas. After each write operation,
		 * we shift the pointer by the number of bytes written.
		 */
		cnt += iproto_fill_bound_address(&array->services[i], p + cnt);
		if (i != array->service_count - 1)
			cnt += snprintf(p + cnt, max - cnt, ", ");
	}
	return buf;
}

void
iproto_service_array_attach(struct iproto_service_array *dst,
			    const struct iproto_service_array *src)
{
	for (int i = 0; i < src->service_count; i++) {
		strcpy(dst->services[i].host, src->services[i].host);
		strcpy(dst->services[i].serv, src->services[i].serv);
		dst->services[i].addrstorage = src->services[i].addrstorage;
		dst->services[i].addr_len = src->services[i].addr_len;
		ev_io_set(&dst->services[i].ev, src->services[i].ev.fd,
			  EV_READ);
	}
	dst->service_count = src->service_count;
}

void
iproto_service_array_detach(struct iproto_service_array *array)
{
	for (int i = 0; i < array->service_count; i++)
		evio_service_detach(&array->services[i]);
	array->service_count = 0;
}

void
iproto_service_array_check_listen(struct iproto_service_array *array)
{
	for (int i = 0; i < array->service_count; i++) {
		if (evio_service_is_active(&array->services[i])) {
			tnt_raise(ClientError, ER_UNSUPPORTED, "Iproto",
				  "listen if service already active");
		}
	}
}

void
iproto_service_array_start_listen(struct iproto_service_array *array)
{
	for (int i = 0; i < array->service_count; i++) {
		if (evio_service_listen(&array->services[i]) != 0)
			diag_raise();
	}
}

void
iproto_service_array_stop_listen(struct iproto_service_array *array)
{
	for (int i = 0; i < array->service_count; i++)
		evio_service_stop(&array->services[i]);
	array->service_count = 0;
}

int
iproto_service_array_bind(struct iproto_service_array *array,
			  const struct cfg_uri_array *uri_array)
{
	int count = cfg_uri_array_size(uri_array);
	assert(count < IPROTO_LISTEN_SOCKET_MAX);
	for (array->service_count = 0;
	     array->service_count < count;
	     array->service_count++) {
		int i = array->service_count;
		const char *uri = cfg_uri_array_get_uri(uri_array, i);
		if (evio_service_bind(&array->services[i], uri) != 0)
			return -1;
	}
	return 0;
}
