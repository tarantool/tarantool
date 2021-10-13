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
#include "tarantool_ee.h"
#include "tt_static.h"
#include "lua/utils.h"
#include "diag.h"
#include "box/error.h"
#include "box/errcode.h"
#include "trivia/util.h"
#include "fiber.h"

static struct evio_service *
iproto_service_array_new_impl(void)
{
	return evio_service_alloc(1);
}
iproto_service_array_new_ptr iproto_service_array_new =
	iproto_service_array_new_impl;

static void
iproto_service_array_delete_impl(struct evio_service *array)
{
	free(array);
}
iproto_service_array_delete_ptr iproto_service_array_delete =
	iproto_service_array_delete_impl;

static void
iproto_service_array_init_impl(struct evio_service *array, size_t *size,
			       struct ev_loop *loop, evio_accept_f on_accept,
			       void *on_accept_param)
{
	evio_service_init(loop, array, "service", on_accept, on_accept_param);
	*size = 0;
}
iproto_service_array_init_ptr iproto_service_array_init =
	iproto_service_array_init_impl;

static const char *
iproto_service_array_fill_listen_info_impl(struct evio_service *array,
					   size_t size, char *buf)
{
	(void)size;
	if (array->addr_len == 0)
		return NULL;
	evio_service_bound_address(buf, array);
	return buf;
}
iproto_service_array_fill_listen_info_ptr
	iproto_service_array_fill_listen_info =
	iproto_service_array_fill_listen_info_impl;

static void
iproto_service_array_attach_impl(struct evio_service *dst, size_t *dst_size,
				 const struct evio_service *src,
				 size_t src_size)
{
	assert(src_size == 1);
	evio_service_attach(dst, src);
	*dst_size = src_size;
}
iproto_service_array_attach_ptr iproto_service_array_attach =
	iproto_service_array_attach_impl;

static void
iproto_service_array_detach_impl(struct evio_service *array, size_t *size)
{
	if (*size != 0) {
		evio_service_detach(array);
		*size = 0;
	}
}
iproto_service_array_detach_ptr iproto_service_array_detach =
	iproto_service_array_detach_impl;

static int
iproto_service_array_check_listen_impl(struct evio_service *array, size_t size)
{
	if (size != 0 && evio_service_is_active(array))
		return -1;
	return 0;
}
iproto_service_array_check_listen_ptr iproto_service_array_check_listen =
	iproto_service_array_check_listen_impl;

static int
iproto_service_array_start_listen_impl(struct evio_service *array, size_t size)
{
	if (size != 0 && evio_service_listen(array) != 0)
		return -1;
	return 0;
}
iproto_service_array_start_listen_ptr iproto_service_array_start_listen =
	iproto_service_array_start_listen_impl;

static void
iproto_service_array_stop_listen_impl(struct evio_service *array, size_t *size)
{
	if (*size != 0) {
		evio_service_stop(array);
		*size = 0;
	}
}
iproto_service_array_stop_listen_ptr iproto_service_array_stop_listen =
	iproto_service_array_stop_listen_impl;

static int
iproto_service_array_bind_impl(struct evio_service *array, size_t *size,
			       const struct cfg_uri_array *uri_array)
{
	assert(cfg_uri_array_size(uri_array) == 1);
	*size = cfg_uri_array_size(uri_array);
	return evio_service_bind(array, cfg_uri_array_get_uri(uri_array, 0));
}
iproto_service_array_bind_ptr iproto_service_array_bind =
	iproto_service_array_bind_impl;